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

#include "../include/sane/config.h"

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <jpeglib.h>
#include <jerror.h>

#include "brother_mfp-common.h"

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_usb.h"


/*
 * Functionality sets.
 *
 * For the moment, I am going to assume that the various proprietary
 * drivers follow the broad protocol versions and I will reflect this
 * until I see different.
 *
 * So these will reflect the brscan2, brscan3, etc that Brother use.
 *
 * We will have a encoder/decoder for each family.
 * From what I have seen, pretty much all the Brother families implement
 * the same basic protocol sequence, just with variations in technology.
 * So we don't need to abstract out the entire sequence, just the encoding
 * and decoding of each packet. The biggest aspect by far is the difference
 * in scan data formats.
 *
 */
typedef enum
{
  BROTHER_FAMILY_NONE,
//  BROTHER_FAMILY_1,
//  BROTHER_FAMILY_2,
//  BROTHER_FAMILY_3,
  BROTHER_FAMILY_4,
//  BROTHER_FAMILY_5
} BrotherFamily;



typedef enum
{
  BROTHER_SCAN_MODE_COLOR,
  BROTHER_SCAN_MODE_GRAY,
  BROTHER_SCAN_MODE_GRAY_DITHERED,
  BROTHER_SCAN_MODE_TEXT
} BrotherScanMode;


struct BrotherSessionResponse
{
  SANE_Bool ready;
};

struct BrotherParameters
{
  BrotherParameters():
    param_brightness (0),
    param_contrast (0),
    param_pixel_x_offset (0),
    param_pixel_x_width (0),
    param_pixel_y_offset (0),
    param_pixel_y_height (0),
    param_scan_mode (BROTHER_SCAN_MODE_COLOR),
    param_x_res(100),
    param_y_res(100)
  {

  }
  SANE_Int param_brightness;
  SANE_Int param_contrast;

  SANE_Int param_pixel_x_offset;
  SANE_Int param_pixel_x_width;
  SANE_Int param_pixel_y_offset;
  SANE_Int param_pixel_y_height;

  BrotherScanMode param_scan_mode;

  SANE_Int param_x_res;
  SANE_Int param_y_res;

};

struct BrotherBasicParamResponse
{
  int dummy;
  // fill me
};

struct BrotherADFResponse
{
  SANE_Byte resp_code;
};



/*
 * DecodeStatus
 *
 * DECODE_STATUS_GOOD           - decode finished. Nothing more to do.
 * DECODE_STATUS_TRUNCATED      - starved of input. Need more src data.
 * DECODE_STATUS_MORE           - starved of output. Have more output available.
 * DECODE_STATUS_ERROR          - format of the data is incorrect.
 *
 */
enum DecodeStatus
{
  DECODE_STATUS_GOOD,
  DECODE_STATUS_TRUNCATED,
  DECODE_STATUS_ENDOFDATA,
  DECODE_STATUS_ENDOFFRAME,
  DECODE_STATUS_ERROR
};

struct ScanDataHeader
{
  ScanDataHeader():
    block_type(0),
    block_len(0)
  {
  }
  SANE_Byte block_type;
  size_t block_len;
};

class BrotherEncoder
{
public:
  BrotherEncoder ()
  {
  }

  virtual ~BrotherEncoder ()
  {
  }

  virtual void NewPage() = 0;

  SANE_Status SetScanMode (BrotherScanMode scan_mode);
  SANE_Status SetRes (SANE_Int x, SANE_Int y);
  SANE_Status SetContrast (SANE_Int contrast);
  SANE_Status SetBrightness (SANE_Int brightness);

  SANE_Status SetScanDimensions (SANE_Int pixel_x_offset, SANE_Int pixel_x_width, SANE_Int pixel_y_offset,
                                 SANE_Int pixel_y_height);

  static const char* ScanModeToText (BrotherScanMode scan_mode);

  virtual SANE_Status DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                         BrotherSessionResponse &response) = 0;

  virtual SANE_Status EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len,
                                                 size_t *length) = 0;

  virtual SANE_Status DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                                     BrotherBasicParamResponse &response) = 0;

  virtual SANE_Status EncodeADFBlock (SANE_Byte *data, size_t data_len, size_t *length) = 0;

  virtual SANE_Status DecodeADFBlockResp (const SANE_Byte *data, size_t data_len,
                                          BrotherADFResponse &response) = 0;

  virtual SANE_Status EncodeParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) = 0;

  virtual SANE_Status DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                      size_t *src_data_consumed, SANE_Byte *dst_data,
                                      size_t dest_data_len, size_t *dest_data_written) = 0;

protected:
  BrotherParameters scan_params;
};

#include <setjmp.h>


class BrotherJFIFDecoder
{
public:
  BrotherJFIFDecoder():
    is_running(false)
  {
    /*
     * Not sure if this is a safe way to get the cinfo into
     * a consistent state but at least all pointers will be NULL.
     *
     */
    (void)memset(&state.cinfo, 0, sizeof(state.cinfo));

    // TODO: override error stuff to avoid exit on error.
    // Also provide a mechanism to check for errors.
    state.cinfo.err = jpeg_std_error(&state.jerr);
    state.cinfo.err->error_exit = ErrorExitManager;

    jpeg_create_decompress (&state.cinfo);

    /*
     * Set up source manager.
     *
     */
    state.src_mgr.init_source = InitSource;
    state.src_mgr.fill_input_buffer = FillInputBuffer;
    state.src_mgr.skip_input_data = SkipInputData;
    state.src_mgr.resync_to_restart = jpeg_resync_to_restart;
    state.src_mgr.term_source = TermSource;

    state.cinfo.src = &state.src_mgr;
  }

  static void ErrorExitManager(j_common_ptr cinfo)
  {
    (void)cinfo;
    longjmp(my_env, 1);
  }

  ~BrotherJFIFDecoder()
  {
    jpeg_destroy_decompress(&state.cinfo);
  }

  void NewBlock();
  void NewPage();

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written);

private:
  static void InitSource (j_decompress_ptr cinfo);
  static boolean FillInputBuffer(j_decompress_ptr cinfo);
  static void SkipInputData(j_decompress_ptr cinfo, long num_bytes);
  static void TermSource(j_decompress_ptr cinfo);

  struct CompressionState
  {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct jpeg_source_mgr src_mgr;
    bool have_read_header;
  } state;

  bool is_running;

// TODO: Move me to the state.
  static jmp_buf my_env;

};

class BrotherGrayRLengthDecoder
{
public:
  BrotherGrayRLengthDecoder():
    decode_state(BROTHER_DECODE_RLEN_INIT),
    decode_expand_char(0),
    block_bytes_left(0)
  {
  }
  void NewBlock();
  void NewPage();

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written);

private:
  enum BrotherDecodeState
  {
    BROTHER_DECODE_RLEN_INIT,
    BROTHER_DECODE_RLEN_IN_EXPAND,
    BROTHER_DECODE_RLEN_IN_BYTES
  };

  // Block decoding state
  BrotherDecodeState decode_state;
  SANE_Byte decode_expand_char;
  size_t block_bytes_left;
};


class BrotherGrayRawDecoder
{
public:
  BrotherGrayRawDecoder()
  {
  }
  void NewBlock();
  void NewPage();

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written);

private:
};

class BrotherEncoderFamily4 : public BrotherEncoder
{
public:
  ~BrotherEncoderFamily4 ()
  {
  }

  void NewPage() override
  {
    current_header.block_type = 0;
  }

  SANE_Status DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                 BrotherSessionResponse &response) override;

  SANE_Status EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  SANE_Status DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                             BrotherBasicParamResponse &response) override;

  SANE_Status EncodeADFBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  SANE_Status DecodeADFBlockResp (const SANE_Byte *data, size_t data_len,
                                  BrotherADFResponse &response) override;

  SANE_Status EncodeParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  SANE_Status DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                      size_t *src_data_consumed, SANE_Byte *dst_data,
                                      size_t dest_data_len, size_t *dest_data_written) override;

private:
  DecodeStatus DecodeScanDataHeader (const SANE_Byte *src_data, size_t src_data_len,
				     size_t *src_data_consumed, ScanDataHeader &header);

  ScanDataHeader current_header;

  BrotherJFIFDecoder jfif_decoder;
  BrotherGrayRLengthDecoder gray_decoder;
  BrotherGrayRawDecoder gray_raw_decoder;
};
