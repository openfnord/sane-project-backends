#undef BACKEND_NAME
#define BACKEND_NAME axis

#include  "../../include/sane/config.h"
#include  "../../include/sane/sane.h"

/*
 * Standard types etc
 */
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <unistd.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/*
 * networking stuff
 */
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif
#ifdef HAVE_SYS_SELSECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include <errno.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "pixma_axis_private.h"
#include "pixma_axis.h"
#include "pixma.h"
#include "pixma_common.h"

#define MAX_PACKET_DATA_SIZE	65535
#define RECEIVE_TIMEOUT		2

/* static data */
static axis_device_t device[AXIS_NO_DEVICES];
static int axis_no_devices = 0;

static void
dbg_hexdump(int level, const SANE_Byte *data, size_t len)
{
#define HEXDUMP_LINE 16
  char line[HEXDUMP_LINE * 3 + 1], *lp = line;

  if (level > DBG_LEVEL)
    return;

  for (size_t i = 0; i < len; i++)
    {
      lp += sprintf(lp, "%02hhx ", data[i]);
      if (i > 0 && i % (HEXDUMP_LINE - 1) == 0)
          DBG(level, "%s\n", lp = line);
    }
  if (lp > line)
    DBG(level, "%s\n", line);
}

extern void
sanei_axis_init (void)
{
  DBG_INIT();
  axis_no_devices = 0;
}

static char *getusername(void) {
  static char noname[] = "sane_pixma";
  struct passwd *pwdent;

#ifdef HAVE_PWD_H
  if (((pwdent = getpwuid(geteuid())) != NULL) && (pwdent->pw_name != NULL))
    return pwdent->pw_name;
#endif
  return noname;
}

static ssize_t receive_packet(int socket, void *packet, size_t len, struct sockaddr_in *from) {
  fd_set rfds;
  struct timeval tv;
  ssize_t received;
  socklen_t from_len = sizeof(struct sockaddr_in);

  tv.tv_sec = RECEIVE_TIMEOUT;
  tv.tv_usec = 0;
  /* Watch socket to see when it has input. */
  FD_ZERO(&rfds);
  FD_SET(socket, &rfds);

  switch (select(socket + 1, &rfds, NULL, NULL, &tv)) {
  case 0:
    return 0;
  case -1:
    DBG(LOG_CRIT, "select() failed\n");
    return 0;
  default:
    received = recvfrom(socket, packet, len, 0, (struct sockaddr *)from, &from_len);
    if (received < 0) {
      DBG(LOG_NOTICE, "Error receiving packet\n");
      return 0;
    }
    return received;
  }
}

static ssize_t axis_send_wimp(int udp_socket, struct sockaddr_in *addr, uint8_t cmd, void *data, uint16_t len) {
  uint8_t packet[MAX_PACKET_DATA_SIZE];
  struct axis_wimp_header *header = (void *)packet;
  ssize_t ret;

  header->type = cmd;
  header->magic = 0x03;
  header->zero = 0x00;
  memcpy(packet + sizeof(struct axis_wimp_header), data, len);
  ret = sendto(udp_socket, packet, sizeof(struct axis_wimp_header) + len, 0, addr, sizeof(struct sockaddr_in));
  if (ret != (int)sizeof(struct axis_wimp_header) + len) {
    DBG(LOG_NOTICE, "Unable to send UDP packet\n");
    return ret;
  }

  return 0;
}

static ssize_t axis_wimp_get(int udp_socket, struct sockaddr_in *addr, uint8_t cmd, uint8_t idx, char *data_out, uint16_t len_out) {
  ssize_t ret;
  uint16_t len;
  struct axis_wimp_get wimp_get;
  struct axis_wimp_header *reply = (void *)data_out;
  struct axis_wimp_get_reply *str = (void *)(data_out + sizeof(struct axis_wimp_header));

  wimp_get.port = addr->sin_port,
  wimp_get.magic = 0x02,
  wimp_get.zero = 0;
  wimp_get.cmd = cmd,
  wimp_get.idx = idx,
  ret = axis_send_wimp(udp_socket, addr, WIMP_SERVER_STATUS, &wimp_get, sizeof(wimp_get));
  if (ret)
    return ret;

  ret = receive_packet(udp_socket, data_out, len_out, addr);
  if (ret < (int)sizeof(struct axis_wimp_header)) {
    DBG(LOG_NOTICE, "Received packet is too short\n");
    return -1;
  }
  if (reply->type != (WIMP_SERVER_STATUS | WIMP_REPLY)) {
    DBG(LOG_NOTICE, "Received invalid reply\n");
    return -1;
  }
  len = le16_to_cpu(str->len) - 2;
  memmove(data_out, data_out + sizeof(struct axis_wimp_header) + sizeof(struct axis_wimp_get_reply), len);
  data_out[len] = '\0';

  return 0;
}

static int create_udp_socket(uint32_t addr, uint16_t *source_port) {
  int udp_socket;
  int enable = 1;
  struct sockaddr_in address;
  socklen_t sock_len;

  udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_socket < 0) {
    DBG(LOG_NOTICE, "Unable to create UDP socket\n");
    return -1;
  }

  if (setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable))) {
    DBG(LOG_NOTICE, "Unable to enable broadcast\n");
    return -1;
  }

  address.sin_family = AF_INET;
  address.sin_port = 0; /* random */
  address.sin_addr.s_addr = addr;
  if (bind(udp_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
    DBG(LOG_NOTICE, "Unable to bind UDP socket\n");
    return -1;
  }

  /* get assigned source port */
  sock_len = sizeof(address);
  getsockname(udp_socket, (struct sockaddr *)&address, &sock_len);
  *source_port = ntohs(address.sin_port);

  return udp_socket;
}

static int get_server_status(int udp_socket, struct sockaddr_in *addr) {
  char buf[MAX_PACKET_DATA_SIZE];

  /* get device status (IDLE/BUSY) */
  if (axis_wimp_get(udp_socket, addr, WIMP_GET_STATUS, 1, buf, sizeof(buf)))
    DBG(LOG_NOTICE, "Error getting device status\n");
  DBG(LOG_INFO, "device status=%s\n", buf);

  /* get username if BUSY */
  if (!strcmp((char *)buf, "BUSY_TXT")) {
    if (axis_wimp_get(udp_socket, addr, WIMP_GET_STATUS, 2, buf, sizeof(buf)))
      DBG(LOG_NOTICE, "Error getting user name\n");
    DBG(LOG_INFO, "username=%s\n", buf);
    return 1;
  }

  return 0;
}

static int get_device_name(int udp_socket, struct sockaddr_in *addr, char *devname, int devname_len) {
  char buf[MAX_PACKET_DATA_SIZE];

  buf[0] = '\0';
  /* get device name */
  if (axis_wimp_get(udp_socket, addr, WIMP_GET_NAME, 1, buf, sizeof(buf)))
    {
      DBG(LOG_NOTICE, "Error getting device name\n");
      return -1;
    }
  DBG(LOG_INFO, "name=%s\n", buf);

  strncpy(devname, buf, devname_len);
  devname[devname_len - 1] = '\0';

  return 0;
}

static int send_discover(int udp_socket, uint32_t addr, uint16_t source_port) {
  int ret;
  struct sockaddr_in address;
  uint8_t get_info[2];

  address.sin_family = AF_INET;
  address.sin_port = htons(AXIS_WIMP_PORT);
  address.sin_addr.s_addr = addr;

  get_info[0] = source_port & 0xff;
  get_info[1] = source_port >> 8;

  ret = axis_send_wimp(udp_socket, &address, WIMP_SERVER_INFO, get_info, sizeof(get_info));
  if (ret)
    DBG(LOG_NOTICE, "Unable to send discover packet\n");

  return ret;
}

static int send_broadcasts(int udp_socket, uint16_t source_port) {
  struct ifaddrs *ifaddr, *ifa;
  int num_sent = 0;

  if (getifaddrs(&ifaddr) == -1) {
    DBG(LOG_NOTICE, "Unable to obtain network interface list\n");
    return -1;
  }

  /* Walk through all interfaces */
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    /* we're interested only in broadcast-capable IPv4 interfaces */
    if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET && ifa->ifa_flags & IFF_BROADCAST) {
      struct sockaddr_in *bcast = (struct sockaddr_in *)ifa->ifa_ifu.ifu_broadaddr;
      DBG(LOG_INFO, "%s: %s\n", ifa->ifa_name, inet_ntoa(bcast->sin_addr));
      if (send_discover(udp_socket, bcast->sin_addr.s_addr, source_port) == 0)
        num_sent++;
    }
  }

  freeifaddrs(ifaddr);

  return num_sent;
}

int axis_send_cmd(int tcp_socket, uint8_t cmd, void *data, uint16_t len) {
  uint8_t packet[MAX_PACKET_DATA_SIZE];
  struct axis_header *header = (void *)packet;
  int ret;
  DBG(LOG_INFO, "%s(0x%02x, %d)\n", __func__, cmd, len);

  header->type = AXIS_HDR_REQUEST;
  header->len = cpu_to_le16(len + sizeof(struct axis_cmd));
  ret = send(tcp_socket, packet, sizeof(struct axis_header), 0);
  dbg_hexdump(LOG_DEBUG2, packet, ret);
  if (ret < 0) {
    DBG(LOG_NOTICE, "Error sending packet\n");
    return ret;
  }

  struct axis_cmd *command = (void *)packet;
  command->cmd = cmd;
  command->len = cpu_to_le16(len);
  memcpy(packet + sizeof(struct axis_cmd), data, len);
  ret = send(tcp_socket, packet, sizeof(struct axis_cmd) + len, 0);
  dbg_hexdump(LOG_DEBUG2, packet, ret);
  if (ret < 0) {
    DBG(LOG_NOTICE, "Error sending packet\n");
    return ret;
  }

  return 0;
}

int axis_recv(SANE_Int dn, SANE_Byte *data, size_t *len) {
  uint8_t packet[MAX_PACKET_DATA_SIZE];
  uint8_t *data_pos;
  struct axis_header *header = (void *)packet;
  struct axis_reply *reply = (void *)packet;
  ssize_t ret, remaining;

retry:
  /* AXIS sends 0x24 byte 15 seconds after end of scan, then repeats 3 more times each 5 seconds - the purpose is unknown, get rid of that */
  ret = recv(device[dn].tcp_socket, packet, 4, MSG_PEEK);
  if (ret < 0)
    return -1;
  int count = 0;
  for (int i = 0; i < ret; i++)
    if (packet[i] == 0x24)
      count++;
    else
      break;
  if (count) {
    DBG(LOG_DEBUG, "discarded %d 0x24 bytes\n", count);
    recv(device[dn].tcp_socket, packet, count, 0);
  }

  ret = recv(device[dn].tcp_socket, packet, sizeof(struct axis_header), 0);
  if (ret < (ssize_t) sizeof(struct axis_header)) {
    DBG(LOG_NOTICE, "recv error\n");
    return -1;
  }

  DBG(LOG_DEBUG2, "got1:\n");
  dbg_hexdump(LOG_DEBUG2, packet, sizeof(struct axis_header));

  if (header->type != AXIS_HDR_REPLY) {
    DBG(LOG_CRIT, "not a reply!:");
    dbg_hexdump(LOG_CRIT, packet, ret);
    return -1;
  }
  *len = le16_to_cpu(header->len);
  DBG(LOG_DEBUG, "len=0x%lx\n", *len);
  ret = recv(device[dn].tcp_socket, packet, *len, 0);
  if (ret < 512) {
    DBG(LOG_DEBUG2, "got2:\n");
    dbg_hexdump(LOG_DEBUG2, packet, ret);
  }
  *len = le16_to_cpu(reply->len);
  if (reply->cmd == AXIS_CMD_UNKNOWN2) {
    DBG(LOG_DEBUG, "interrupt\n");
    memcpy(device[dn].int_data, packet + sizeof(struct axis_reply), *len);
    device[dn].int_size = *len;
    goto retry;
  }
  memcpy(data, packet + sizeof(struct axis_reply), ret - sizeof(struct axis_reply));
  if (reply->status != 0) {
    DBG(LOG_CRIT, "status=0x%x\n", le16_to_cpu(reply->status));
    return SANE_STATUS_IO_ERROR;
  }

  remaining = *len - (ret - sizeof(struct axis_reply));
  data_pos = data + ret - sizeof(struct axis_reply);
  while (remaining > 0) {
    DBG(LOG_DEBUG, "remaining bytes: %ld\n", remaining);
    ret = recv(device[dn].tcp_socket, data_pos, remaining, 0);
    remaining -= ret;
    data_pos += ret;
  }

  return 0;
}

static int
parse_uri(const char *uri, struct sockaddr_in *address)
{
  const char *uri_host, *uri_port;
  char host[256];
  size_t host_len;
  int port;

  if (strncmp(uri, "axis://", 7))
    {
      DBG(LOG_INFO, "Invalid protocol in uri\n");
      return -1;
    }
  uri_host = uri + 7;

  port = AXIS_SCAN_PORT;
  uri_port = strchr(uri_host, ':');
  if (uri_port)
    {
      sscanf(uri_port, ":%d", &port);
      host_len = uri_port - uri_host;
    }
  else
    host_len = strlen(uri_host);
  if (host_len > 255)
    host_len = 255;
  strncpy(host, uri_host, host_len);
  host[host_len] = '\0';

  /* resolve host */
  struct addrinfo *result;
  struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM, .ai_protocol = IPPROTO_TCP };
  int err = getaddrinfo(host, NULL, &hints, &result);
  if (err)
    {
      DBG(LOG_CRIT, "cannot resolve %s: %s\n", host, gai_strerror(err));
      return -1;
    }
  struct sockaddr_in *addr = (struct sockaddr_in *) result->ai_addr;
  DBG(LOG_DEBUG, "host=%s, ip=%s, port=%d\n", host, inet_ntoa(addr->sin_addr), port);
  address->sin_family = AF_INET;
  address->sin_port = htons(port);
  address->sin_addr = addr->sin_addr;

  freeaddrinfo(result);

  return 0;
}


/**
 * Find AXIS printservers with Canon support
 *
 * The function attach is called for every device which has been found.
 *
 * @param attach attach function
 *
 * @return SANE_STATUS_GOOD - on success (even if no scanner was found)
 */
extern SANE_Status
sanei_axis_find_devices (const char **conf_devices,
			 SANE_Status (*attach_axis)
			 (SANE_String_Const devname,
			  SANE_String_Const makemodel,
			  SANE_String_Const serial,
			  const struct pixma_config_t *
			  const pixma_devices[]),
			 const struct pixma_config_t *const pixma_devices[])
{
  char devname[256];
  char uri[256];
  uint8_t packet[MAX_PACKET_DATA_SIZE];
  struct sockaddr_in from;
  uint16_t source_port;
  int udp_socket, num_ifaces;
  int auto_detect = 1, i;

  udp_socket = create_udp_socket(htonl(INADDR_ANY), &source_port);
  if (udp_socket < 0)
    return SANE_STATUS_IO_ERROR;
  DBG(LOG_DEBUG, "source port=%d\n", source_port);

  /* parse config file */
  if (conf_devices[0] != NULL)
    {
      if (strcmp(conf_devices[0], "networking=no") == 0)
        {
          /* networking=no may only occur on the first non-commented line */
          DBG(LOG_DEBUG, "%s: networking disabled in configuration file\n", __func__);
          close(udp_socket);
          return SANE_STATUS_GOOD;
        }
      for (i = 0; conf_devices[i] != NULL; i++)
        {
	  if (!strcmp(conf_devices[i], "auto_detection=no"))
            {
              auto_detect = 0;
	      DBG(LOG_DEBUG, "%s: auto detection of network scanners disabled in configuration file\n", __func__);
	      continue;
	    }
	  else
            {
              DBG(LOG_DEBUG, "%s: Adding scanner from pixma.conf: %s\n", __func__, conf_devices[i]);
              strncpy(uri, conf_devices[i], sizeof(uri) - 1);
              uri[sizeof(uri) - 1] = '\0';
              if (axis_no_devices >= AXIS_NO_DEVICES)
                {
                  DBG(LOG_INFO, "%s: device limit %d reached\n", __func__, AXIS_NO_DEVICES);
                  close(udp_socket);
                  return SANE_STATUS_NO_MEM;
                }

              struct sockaddr_in addr;
              if (parse_uri(uri, &addr))
                continue;
              addr.sin_port = htons(AXIS_WIMP_PORT);
              if (get_device_name(udp_socket, &addr, devname, sizeof(devname)) == 0)
                {
                  device[axis_no_devices++].addr = addr.sin_addr;
                  attach_axis(uri, devname, inet_ntoa(addr.sin_addr), pixma_devices);
                }
	    }
        }
    }

  if (!auto_detect)
    {
      close(udp_socket);
      return SANE_STATUS_GOOD;
    }

  /* send broadcast discover packets to all interfaces */
  num_ifaces = send_broadcasts(udp_socket, source_port);
  DBG(LOG_INFO, "sent broadcasts to %d interfaces\n", num_ifaces);

  /* wait for response packets */
  while (receive_packet(udp_socket, packet, sizeof(packet), &from) != 0) {
    struct axis_wimp_header *header = (void *)packet;
//    struct axis_wimp_server_info *s_info = (void *)(packet + sizeof(struct axis_wimp_header));

    DBG(LOG_INFO, "got reply from %s\n", inet_ntoa(from.sin_addr));
    if (header->type != (WIMP_SERVER_INFO | WIMP_REPLY)) {
      DBG(LOG_NOTICE, "Received invalid reply\n");
      continue;
    }

    get_device_name(udp_socket, &from, devname, sizeof(devname));
    sprintf(uri, "axis://%s", inet_ntoa(from.sin_addr));

    device[axis_no_devices++].addr = from.sin_addr;
    attach_axis(uri, devname, inet_ntoa(from.sin_addr), pixma_devices);
  }
  close(udp_socket);

  return SANE_STATUS_GOOD;
}

extern SANE_Status
sanei_axis_open (SANE_String_Const devname, SANE_Int * dn)
{
  int i;
  char *username;
  struct sockaddr_in address;

  DBG(LOG_INFO, "%s(%s, %d)\n", __func__, devname, *dn);
  if (parse_uri(devname, &address))
    return SANE_STATUS_INVAL;

  for (i = 0; i < axis_no_devices; i++)
    if (device[i].addr.s_addr == address.sin_addr.s_addr) {
      DBG(LOG_INFO, "found device at position %d\n", i);
      *dn = i;
      /* connect */
      int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
      if (tcp_socket < 0) {
        DBG(LOG_NOTICE, "Unable to create TCP socket: %s\n", strerror(errno));
        return SANE_STATUS_IO_ERROR;
      }
      if (connect(tcp_socket, (struct sockaddr *) &address, sizeof(address)) < 0) {
        DBG(LOG_NOTICE, "Unable to connect: %s\n", strerror(errno));
        return SANE_STATUS_IO_ERROR;
      }
      DBG(LOG_INFO, "connected\n");

      device[i].tcp_socket = tcp_socket;

      username = getusername();
      axis_send_cmd(tcp_socket, AXIS_CMD_CONNECT, username, strlen(username) + 1);
      SANE_Byte dummy_buf[MAX_PACKET_DATA_SIZE];
      size_t dummy_len;
      axis_recv(i, dummy_buf, &dummy_len);

      uint8_t timeout[] = { 0x0e, 0x01, 0x00, 0x00 };
      axis_send_cmd(tcp_socket, AXIS_CMD_UNKNOWN3, timeout, sizeof(timeout));
      axis_recv(i, dummy_buf, &dummy_len);

      axis_send_cmd(tcp_socket, AXIS_CMD_UNKNOWN, NULL, 0);
      axis_recv(i, dummy_buf, &dummy_len);

      return SANE_STATUS_GOOD;
    }

  return SANE_STATUS_INVAL;
}

void
sanei_axis_close (SANE_Int dn)
{
  DBG(LOG_INFO, "%s(%d)\n", __func__, dn);
}

extern void
sanei_axis_set_timeout (SANE_Int dn, SANE_Int timeout)
{
  DBG(LOG_INFO, "%s(%d, %d)\n", __func__, dn, timeout);
  struct timeval tv;
  tv.tv_sec = timeout / 1000;
  tv.tv_usec = timeout % 1000;
  setsockopt(device[dn].tcp_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,  sizeof(tv));
}

extern SANE_Status
sanei_axis_read_bulk (SANE_Int dn, SANE_Byte * buffer, size_t * size)
{
  DBG(LOG_INFO, "%s(%d, %p, %ld)\n", __func__, dn, buffer, *size);
  uint16_t read_size = cpu_to_le16(*size);
  axis_send_cmd(device[dn].tcp_socket, AXIS_CMD_READ, &read_size, sizeof(read_size));
  axis_recv(dn, buffer, size);  ////FIXME
  DBG(LOG_DEBUG2, "sanei_axis_read_bulk:\n");
  if (*size < 512)
    dbg_hexdump(LOG_DEBUG2, buffer, *size);
  return SANE_STATUS_GOOD;
}

extern SANE_Status
sanei_axis_write_bulk (SANE_Int dn, const SANE_Byte * buffer, size_t * size)
{
  SANE_Byte dummy_buf[MAX_PACKET_DATA_SIZE];
  size_t dummy_len;
  DBG(LOG_INFO, "%s(%d, %p, %ld)\n", __func__, dn, buffer, *size);
  axis_send_cmd(device[dn].tcp_socket, AXIS_CMD_WRITE, (void *) buffer, *size);
  axis_recv(dn, dummy_buf, &dummy_len);	////FIXME
  return SANE_STATUS_GOOD;
}

extern SANE_Status
sanei_axis_read_int (SANE_Int dn, SANE_Byte * buffer, size_t * size)
{
  DBG(LOG_INFO, "%s(%d, %p, %ld)\n", __func__, dn, buffer, *size);
  if (!device[dn].int_size)
    return SANE_STATUS_EOF;
  memcpy(buffer, device[dn].int_data, device[dn].int_size);
  *size = device[dn].int_size;
  device[dn].int_size = 0;
  return SANE_STATUS_GOOD;
}
