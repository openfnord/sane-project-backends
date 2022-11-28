/* sane - Scanner Access Now Easy.

   BACKEND brother_mfp

   Copyright (C) 2022 Ralph Little <skelband@gmail.com>

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.

   This file implements a SANE backend for Brother Multifunction Devices.
   */

#define DEBUG_DECLARE_ONLY

#include "../include/sane/config.h"

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <algorithm>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>



#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_debug.h"

#include "brother_mfp-common.h"
#include "brother_mfp-driver.h"

/*
 * Protocol defines.
 *
 */
#define BROTHER_USB_REQ_STARTSESSION       1
#define BROTHER_USB_REQ_STOPSESSION        2
#define BROTHER_USB_REQ_BUTTONSTATE        3

#define BROTHER_READ_BUFFER_LEN (16 * 1024)


/*-----------------------------------------------------------------*/

union BrotherSocketAddr
{
  struct sockaddr_storage storage;
  struct sockaddr addr;
  struct sockaddr_in ipv4;
  struct sockaddr_in6 ipv6;
};

SANE_Status BrotherNetworkDriver::Connect ()
{
  int val;

  if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
      return SANE_STATUS_IO_ERROR;
    }

  val = 1;
  setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  /*
   * Using TCP_NODELAY improves responsiveness, especially on systems
   * with a slow loopback interface...
   */

  val = 1;
  setsockopt (sockfd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));

  /*
   * Close this socket when starting another process...
   */

  fcntl (sockfd, F_SETFD, FD_CLOEXEC);

  BrotherSocketAddr adr_inet;
  memset (&adr_inet, 0, sizeof adr_inet);

  adr_inet.ipv4.sin_family = AF_INET;
  adr_inet.ipv4.sin_port = htons (54921);

  if (!inet_aton ("192.168.1.19", &adr_inet.ipv4.sin_addr))
    {
      return SANE_STATUS_IO_ERROR;
    }

  if (connect (sockfd, (struct sockaddr*) &adr_inet, sizeof(adr_inet.ipv4)) < 0)
    {
      return SANE_STATUS_IO_ERROR;
    }

  SANE_Status init_res = Init ();

  if (init_res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "BrotherUSBDriver::Connect: failed to send init session: %d\n", init_res);
      Disconnect ();
    }

  /*
   * Register buttons.
   *
   */
  (void)SNMPRegisterButtons();

  return SANE_STATUS_NO_MEM;
}


#if HAVE_LIBSNMP
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#endif

SANE_Status BrotherNetworkDriver::SNMPRegisterButtons ()
{
#if HAVE_LIBSNMP
  init_snmp ("brother_mfp");

  /*
   * Initialize a "session" that defines who we're going to talk to
   */
  struct snmp_session session;

  oid anOID[MAX_OID_LEN];
  size_t anOID_len = MAX_OID_LEN;

  snmp_sess_init (&session); /* set up defaults */
  session.peername = (char *)"192.168.1.19";

  session.version = SNMP_VERSION_1;

  /* set the SNMPv1 community name used for authentication */
  session.community = (u_char *)"internal";
  session.community_len = strlen ("internal");

  SOCK_STARTUP;

  /*
   * Open the session
   */
  struct snmp_session *ss = snmp_open (&session);

  if (!ss) {
      snmp_perror("ack");
      snmp_log(LOG_ERR, "something horrible happened!!!\n");
      return SANE_STATUS_IO_ERROR;
  }

  netsnmp_pdu *pdu;
  oid the_oid[MAX_OID_LEN];
  size_t oid_len;
  const char *oid_str = ".1.3.6.1.4.1.2435.2.3.9.2.11.1.1.0";

  pdu = snmp_pdu_create (SNMP_MSG_SET);
  oid_len = MAX_OID_LEN;

  // Parse the OID
  if (snmp_parse_oid (oid_str, the_oid, &oid_len) == 0)
    {
      snmp_perror (oid_str);
      return SANE_STATUS_IO_ERROR;
    }

  struct ButtonApp
  {
    const char *function;
    unsigned int app_num;
  } button_apps[] =
      {
          {"IMAGE", 1} ,
          {"OCR", 3} ,
          {"FILE", 5} ,
          {"EMAIL", 2} ,
          { nullptr, 0 }
      };

  const char *backend_host_addr = "192.168.1.12";
  unsigned int backend_host_port = 54925;
  const char *backend_host_name = "BACKEND_HOST";
  unsigned int reg_lifetime = 360;

  for (const ButtonApp *app = button_apps; app->function; app++)
    {
      (void) snprintf ((char*) small_buffer,
                       sizeof(small_buffer),
                       "TYPE=BR;BUTTON=SCAN;USER=\"%s\";FUNC=%s;HOST=%s:%u;APPNUM=%u;DURATION=%u;",
                       backend_host_name,
                       app->function,
                       backend_host_addr,
                       backend_host_port,
                       app->app_num,
                       reg_lifetime);

      //  const char *value = "TYPE=BR;BUTTON=SCAN;USER=\"MYTESTSVR\";FUNC=OCR;HOST=192.168.1.12:54925;APPNUM=3;DURATION=360;";
      if (snmp_pdu_add_variable (pdu,
                                 the_oid,
                                 oid_len,
                                 ASN_OCTET_STR,
                                 (const char*) small_buffer,
                                 strlen ((const char*) small_buffer)) == nullptr)
        {
          snmp_perror ("failed");
          return SANE_STATUS_IO_ERROR;
        }
    }

  // Send the request
  if (snmp_send (ss, pdu) == 0)
    {
      snmp_perror ("SNMP: Error while sending!");
      snmp_free_pdu (pdu);
      return SANE_STATUS_IO_ERROR;
    }

  return SANE_STATUS_NO_MEM;
#else
  return SANE_STATUS_GOOD;
#endif
}

SANE_Status BrotherNetworkDriver::Disconnect ()
{
  DBG (DBG_EVENT, "BrotherNetworkDriver::Disconnect: `%s'\n", devicename);

  if (sockfd)
    {
      close(sockfd);
      sockfd = 0;
    }

  return SANE_STATUS_GOOD;
}

SANE_Status BrotherNetworkDriver::Init ()
{
  return SANE_STATUS_UNSUPPORTED;
}

SANE_Status BrotherNetworkDriver::CancelScan ()
{
  return SANE_STATUS_UNSUPPORTED;
}

SANE_Status BrotherNetworkDriver::StartScan ()
{
  return SANE_STATUS_UNSUPPORTED;
}

SANE_Status BrotherNetworkDriver::CheckSensor (BrotherSensor &status)
{
  return SANE_STATUS_UNSUPPORTED;
}

SANE_Status BrotherNetworkDriver::ReadScanData (SANE_Byte *data, size_t max_length, size_t *length)
{
  return SANE_STATUS_UNSUPPORTED;
}

BrotherNetworkDriver::BrotherNetworkDriver (const char *devicename, BrotherFamily family,
                                            SANE_Word capabilities) :
    BrotherDriver (family, capabilities),
    is_open (false),
    in_session (false),
    is_scanning (false),
    was_cancelled (false),
    devicename (nullptr),
    next_frame_number (0),
    small_buffer { 0 },
    data_buffer (nullptr),
    data_buffer_bytes (0),
    out_of_docs (false),
    sockfd (0)
 {
   this->devicename = strdup(devicename);
 }

BrotherNetworkDriver::~BrotherNetworkDriver ()
{
  delete[] data_buffer;
  free (devicename);
}
