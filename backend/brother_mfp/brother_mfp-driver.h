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
#pragma once

#define BUILD 0

#include "../include/sane/config.h"

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "brother_mfp-common.h"

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_usb.h"

#include "brother_mfp-encoder.h"

/*-----------------------------------------------------------------*/

/*
 * Handy timeout macros for use with usecond_t-based functions.
 *
 */
#define TIMEOUT_SECS(x)         ((x) * 1000000)
#define TIMEOUT_MILLISECS(x)    ((x) * 1000)
#define TIMEOUT_MICROSECS(x)    (x)


/*
 * Transport base and implementations.
 *
 * We have a base which lays the groundwork for our
 * scanner communications and the interface for the SANE
 * frontend to talk with the device.
 *
 * We have implementations for both USB and Ethernet.
 *
 */
class BrotherDriver
{
public:
  explicit BrotherDriver (BrotherFamily family, SANE_Word capabilities);

  BrotherDriver(const BrotherDriver &) = delete;
  BrotherDriver &operator=(const BrotherDriver &) = delete;

  virtual ~BrotherDriver();

  /*
   *  Will include the INIT sequence.
   *
   */
  virtual SANE_Status Connect () = 0;

  /*
   * Will ensure that we stop a session if active.
   *
   */
  virtual SANE_Status Disconnect () = 0;

  /*
   * Will open a session.
   *
   */
  virtual SANE_Status StartScan () = 0;

  virtual SANE_Status CancelScan () = 0;

  virtual SANE_Status CheckSensor(BrotherSensor &status) = 0;

  virtual SANE_Status ReadScanData (SANE_Byte *data, size_t data_len,
                                    size_t *bytes_read) = 0;

  /*
   * Parameter setting.
   *
   */
  SANE_Status SetSource (BrotherSource source)
  {
    return encoder->DecodeStatusToSaneStatus (encoder->SetSource (source));
  }

  SANE_Status SetScanMode (BrotherScanMode scan_mode)
  {
    return encoder->DecodeStatusToSaneStatus (encoder->SetScanMode (scan_mode));
  }

  SANE_Status SetRes (SANE_Int x, SANE_Int y)
  {
    return encoder->DecodeStatusToSaneStatus (encoder->SetRes (x, y));
  }

  SANE_Status SetContrast (SANE_Int contrast)
  {
    return encoder->DecodeStatusToSaneStatus (encoder->SetContrast (contrast));
  }

  SANE_Status SetBrightness (SANE_Int brightness)
  {
    return encoder->DecodeStatusToSaneStatus (encoder->SetBrightness (brightness));
  }

  SANE_Status SetCompression (SANE_Bool compression)
  {
    return encoder->DecodeStatusToSaneStatus (encoder->SetCompression (compression));
  }

  SANE_Status SetScanDimensions (SANE_Int pixel_x_offset, SANE_Int pixel_x_width,
                                 SANE_Int pixel_y_offset, SANE_Int pixel_y_height)
  {
    return encoder->DecodeStatusToSaneStatus (encoder->SetScanDimensions (pixel_x_offset,
                                                                          pixel_x_width,
                                                                          pixel_y_offset,
                                                                          pixel_y_height));
  }

protected:
  BrotherFamily family;
  SANE_Word capabilities;
  BrotherEncoder *encoder;
};

class BrotherUSBDriver : public BrotherDriver
{
public:
  BrotherUSBDriver (const char *devicename, BrotherFamily family, SANE_Word capabilities);

  ~BrotherUSBDriver ();

  SANE_Status Connect () override;

  SANE_Status Disconnect () override;

  SANE_Status StartScan () override;

  SANE_Status CancelScan () override;

  SANE_Status CheckSensor(BrotherSensor &status) override;

  SANE_Status ReadScanData (SANE_Byte *data, size_t data_len,
                            size_t *bytes_read) override;

private:
  SANE_Status StartSession ();
  SANE_Status StopSession ();
  SANE_Status ExecStartSession ();
  SANE_Status ExecStopSession ();
  SANE_Status Init ();

  SANE_Status PollForReadFlush (useconds_t max_time);
  SANE_Status PollForRead (SANE_Byte *buffer, size_t *buf_len,
                           useconds_t *max_time);
  bool is_open;
  bool in_session;
  bool is_scanning;
  bool was_cancelled;
  char *devicename;
  SANE_Int next_frame_number;
  SANE_Int fd;

  SANE_Byte small_buffer[1024];
  SANE_Byte *data_buffer;
  size_t data_buffer_bytes;

  bool out_of_docs;

};

class BrotherNetworkDriver : public BrotherDriver
{
public:
  BrotherNetworkDriver (const char *devicename, BrotherFamily family, SANE_Word capabilities);

  ~BrotherNetworkDriver ();

  SANE_Status Connect () override;

  SANE_Status Disconnect () override;

  SANE_Status StartScan () override;

  SANE_Status CancelScan () override;

  SANE_Status CheckSensor(BrotherSensor &status) override;

  SANE_Status ReadScanData (SANE_Byte *data, size_t data_len,
                            size_t *bytes_read) override;

private:
  SANE_Status Init ();

  SANE_Status SNMPRegisterButtons ();

//  SANE_Status PollForReadFlush (useconds_t max_time);
//  SANE_Status PollForRead (SANE_Byte *buffer, size_t *buf_len,
//                           useconds_t *max_time);
  bool is_open;
  bool in_session;
  bool is_scanning;
  bool was_cancelled;
  char *devicename;
  SANE_Int next_frame_number;

  SANE_Byte small_buffer[1024];
  SANE_Byte *data_buffer;
  size_t data_buffer_bytes;

  bool out_of_docs;

  int sockfd;

};
