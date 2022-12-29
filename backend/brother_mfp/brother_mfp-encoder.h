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
#include <setjmp.h>

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
  BROTHER_FAMILY_1,
  BROTHER_FAMILY_2,
  BROTHER_FAMILY_3,
  BROTHER_FAMILY_4,
  BROTHER_FAMILY_5
} BrotherFamily;



typedef enum
{
  BROTHER_SCAN_MODE_COLOR,
  BROTHER_SCAN_MODE_GRAY,
  BROTHER_SCAN_MODE_GRAY_DITHERED,
  BROTHER_SCAN_MODE_TEXT
} BrotherScanMode;


typedef enum
{
  BROTHER_SOURCE_NONE,
  BROTHER_SOURCE_AUTO,
  BROTHER_SOURCE_FLATBED,
  BROTHER_SOURCE_ADF,
  BROTHER_SOURCE_ADF_DUPLEX,
} BrotherSource;

typedef SANE_Int BrotherSensor;

#define BROTHER_SENSOR_NONE     0
#define BROTHER_SENSOR_EMAIL    (1 << 0)
#define BROTHER_SENSOR_FILE     (1 << 1)
#define BROTHER_SENSOR_OCR      (1 << 2)
#define BROTHER_SENSOR_IMAGE    (1 << 3)

struct BrotherSessionResponse
{
  SANE_Bool ready;
};

struct BrotherButtonQueryResponse
{
  SANE_Bool has_button_press;
};

struct BrotherButtonStateResponse
{
  BrotherSensor button_value;   // raw value we get from the packet
};

struct BrotherParameters
{
  BrotherParameters():
    param_brightness (0),
    param_contrast (0),
    param_compression (SANE_TRUE),
    param_pixel_x_offset (0),
    param_pixel_x_width (0),
    param_pixel_y_offset (0),
    param_pixel_y_height (0),
    param_scan_mode (BROTHER_SCAN_MODE_COLOR),
    param_source(BROTHER_SOURCE_FLATBED),
    param_x_res(100),
    param_y_res(100)
  {
  }
  SANE_Int param_brightness;
  SANE_Int param_contrast;
  SANE_Bool param_compression;

  SANE_Int param_pixel_x_offset;
  SANE_Int param_pixel_x_width;
  SANE_Int param_pixel_y_offset;
  SANE_Int param_pixel_y_height;

  BrotherScanMode param_scan_mode;
  BrotherSource param_source;

  SANE_Int param_x_res;
  SANE_Int param_y_res;

};

struct BrotherBasicParamResponse
{
  int dummy;
  // fill me
};

struct BrotherQueryResponse
{
  int dummy;
  // fill me
};

struct BrotherSourceStatusResponse
{
  SANE_Bool source_ready;
};


/*
 * DecodeStatus
 *
 */
enum DecodeStatus
{
  DECODE_STATUS_GOOD,
  DECODE_STATUS_TRUNCATED,
  DECODE_STATUS_ENDOFDATA,
  DECODE_STATUS_ENDOFFRAME_NO_MORE,
  DECODE_STATUS_ENDOFFRAME_WITH_MORE,
  DECODE_STATUS_CANCEL,
  DECODE_STATUS_ERROR,
  DECODE_STATUS_MEMORY,
  DECODE_STATUS_INVAL,
  DECODE_STATUS_UNSUPPORTED,
  DECODE_STATUS_PAPER_JAM,
  DECODE_STATUS_COVER_OPEN,
  DECODE_STATUS_NO_DOCS,
};

struct ScanDataHeader
{
  ScanDataHeader():
    block_type(0),
    block_len(0),
    image_number(0)
  {
  }
  SANE_Byte block_type;
  size_t block_len;
  SANE_Int image_number;
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

  virtual void NewPage () = 0;

  DecodeStatus SetSource (BrotherSource source);
  DecodeStatus SetScanMode (BrotherScanMode scan_mode);
  DecodeStatus SetRes (SANE_Int x, SANE_Int y);
  DecodeStatus SetContrast (SANE_Int contrast);
  DecodeStatus SetBrightness (SANE_Int brightness);
  DecodeStatus SetCompression (SANE_Bool compression);

  DecodeStatus SetScanDimensions (SANE_Int pixel_x_offset, SANE_Int pixel_x_width,
                                  SANE_Int pixel_y_offset, SANE_Int pixel_y_height);

  SANE_Status DecodeStatusToSaneStatus(DecodeStatus dec_ret)
  {
    static SANE_Status status_lookup[] =
        {
            SANE_STATUS_GOOD,
            SANE_STATUS_GOOD,
            SANE_STATUS_EOF,
            SANE_STATUS_EOF,
            SANE_STATUS_EOF,
            SANE_STATUS_CANCELLED,
            SANE_STATUS_IO_ERROR,
            SANE_STATUS_NO_MEM,
            SANE_STATUS_INVAL,
            SANE_STATUS_UNSUPPORTED,
            SANE_STATUS_JAMMED,
            SANE_STATUS_COVER_OPEN,
            SANE_STATUS_NO_DOCS,
      };

    return status_lookup[dec_ret];
  }

  const char *DecodeStatusToString(DecodeStatus dec_ret)
  {
    static const char * status_lookup[] =
        {
            "DECODE_STATUS_GOOD",
            "DECODE_STATUS_TRUNCATED",
            "DECODE_STATUS_ENDOFDATA",
            "DECODE_STATUS_ENDOFFRAME_NO_MORE",
            "DECODE_STATUS_ENDOFFRAME_WITH_MORE",
            "DECODE_STATUS_CANCEL",
            "DECODE_STATUS_ERROR",
            "DECODE_STATUS_MEMORY",
            "DECODE_STATUS_INVAL",
            "DECODE_STATUS_UNSUPPORTED",
            "DECODE_STATUS_PAPER_JAM",
            "DECODE_STATUS_COVER_OPEN",
            "DECODE_STATUS_NO_DOCS",
      };

    return status_lookup[dec_ret];
  }


  virtual const char* ScanModeToText (BrotherScanMode scan_mode);

  // Buttons.
  virtual DecodeStatus DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                          BrotherSessionResponse &response) = 0;

  virtual DecodeStatus DecodeButtonQueryResp (const SANE_Byte *data, size_t data_len,
                                              BrotherButtonQueryResponse &response) = 0;

  virtual DecodeStatus DecodeButtonStateResp (const SANE_Byte *data, size_t data_len,
                                              BrotherButtonStateResponse &response) = 0;

  // Requests and responses.
  virtual DecodeStatus EncodeQueryBlock (SANE_Byte *data, size_t data_len, size_t *length) = 0;

  virtual DecodeStatus DecodeQueryBlockResp (const SANE_Byte *data, size_t data_len,
                                             BrotherQueryResponse &response) = 0;

  virtual DecodeStatus EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len,
                                                  size_t *length) = 0;

  virtual DecodeStatus DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                                      BrotherBasicParamResponse &response) = 0;

  virtual DecodeStatus EncodeSourceStatusBlock (SANE_Byte *data, size_t data_len, size_t *length) = 0;

  virtual DecodeStatus DecodeSourceStatusBlockResp (const SANE_Byte *data, size_t data_len,
                                                    BrotherSourceStatusResponse &response) = 0;

  virtual DecodeStatus EncodeSourceSelectBlock (SANE_Byte *data, size_t data_len, size_t *length) = 0;

  virtual DecodeStatus DecodeSourceSelectBlockResp (const SANE_Byte *data, size_t data_len,
                                                    BrotherSourceStatusResponse &response) = 0;

  virtual DecodeStatus EncodeParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) = 0;

  virtual DecodeStatus EncodeParameterBlockBlank (SANE_Byte *data, size_t data_len,
                                                  size_t *length) = 0;

  // Scan data decoding.
  virtual DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                       size_t *src_data_consumed, SANE_Byte *dst_data,
                                       size_t dest_data_len, size_t *dest_data_written) = 0;

protected:
  BrotherParameters scan_params;
};

class BrotherJFIFDecoder
{
public:
  BrotherJFIFDecoder():
    decompress_bytes(0)
  {
    /*
     * Not sure if this is a safe way to get the cinfo into
     * a consistent state but at least all pointers will be NULL.
     *
     */
    (void)memset(&state.cinfo, 0, sizeof(state.cinfo));

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

  static void ErrorExitManager(j_common_ptr cinfo);

  ~BrotherJFIFDecoder()
  {
    jpeg_destroy_decompress(&state.cinfo);
  }

  void NewBlock();
  void NewPage(const BrotherParameters &params);

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written);

private:
  static void InitSource (j_decompress_ptr cinfo);
  static boolean FillInputBuffer(j_decompress_ptr cinfo);
  static void SkipInputData(j_decompress_ptr cinfo, long num_bytes);
  static void TermSource(j_decompress_ptr cinfo);

  DecodeStatus DecodeScanData_CompressBuffer (const SANE_Byte *src_data, size_t src_data_len,
                                              size_t *src_data_consumed, SANE_Byte *dst_data,
                                              size_t dest_data_len, size_t *dest_data_written);

  struct CompressionState
  {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct jpeg_source_mgr src_mgr;
    bool have_read_header;
    jmp_buf my_env;
  } state;

  SANE_Byte decompress_buffer[1024 * 16];
  size_t decompress_bytes;

  BrotherParameters decode_params;
};

class BrotherGrayRLengthDecoder
{
public:
  BrotherGrayRLengthDecoder()
  {
  }
  void NewBlock();

  void NewPage(const BrotherParameters &params);

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written);

private:
  BrotherParameters decode_params;
};


class BrotherInterleavedRGBColourDecoder
{
public:
  explicit BrotherInterleavedRGBColourDecoder(SANE_Word capabilities):
    capabilities(capabilities),
    scanline_buffer(nullptr),
    scanline_buffer_size(0),
    scanline_length(0),
    scanline_buffer_data(0)
  {
  }

  ~BrotherInterleavedRGBColourDecoder()
  {
    free(scanline_buffer);
  }
  void NewBlock();

  void NewPage(const BrotherParameters &params);

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written);

  enum ChannelEncoding
  {
    CHANNELS_RGB,       // RGB
    CHANNELS_CrYCb      // YCrCb
  };
private:
  static void ConvertYCbCrToRGB (SANE_Byte y, SANE_Byte cb, SANE_Byte cr, SANE_Byte *red,
                                 SANE_Byte *green, SANE_Byte *blue);

  BrotherParameters decode_params;
  SANE_Word capabilities;

  SANE_Byte *scanline_buffer;
  size_t scanline_buffer_size;
  size_t scanline_length;
  size_t scanline_buffer_data;
};


class BrotherGrayRawDecoder
{
public:
  BrotherGrayRawDecoder()
  {
  }
  void NewBlock();

  void NewPage(const BrotherParameters &params);

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written);

private:
  BrotherParameters decode_params;
};

class BrotherEncoderFamily2 : public BrotherEncoder
{
public:
  explicit BrotherEncoderFamily2(SANE_Word capabilities):
    colour_decoder(capabilities)
  {
  }

  ~BrotherEncoderFamily2 ()
  {
  }

  void NewPage () override
  {
    current_header.block_type = 0;

    jfif_decoder.NewPage (scan_params);
    gray_decoder.NewPage (scan_params);
    gray_raw_decoder.NewPage (scan_params);
    colour_decoder.NewPage (scan_params);
  }

  DecodeStatus EncodeQueryBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeQueryBlockResp (const SANE_Byte *data, size_t data_len,
                                     BrotherQueryResponse &response) override;

  DecodeStatus DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                  BrotherSessionResponse &response) override;

  DecodeStatus EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len, size_t *length)
      override;

  DecodeStatus DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                              BrotherBasicParamResponse &response) override;

  DecodeStatus EncodeSourceStatusBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeSourceStatusBlockResp (const SANE_Byte *data, size_t data_len,
                                            BrotherSourceStatusResponse &response) override;

  DecodeStatus EncodeSourceSelectBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeSourceSelectBlockResp (const SANE_Byte *data, size_t data_len,
                                            BrotherSourceStatusResponse &response) override;

  DecodeStatus EncodeParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus EncodeParameterBlockBlank (SANE_Byte *data, size_t data_len, size_t *length)
      override;

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written) override;

  DecodeStatus DecodeButtonQueryResp (const SANE_Byte *data, size_t data_len,
                                      BrotherButtonQueryResponse &response) override;

  DecodeStatus DecodeButtonStateResp (const SANE_Byte *data, size_t data_len,
                                      BrotherButtonStateResponse &response) override;

private:
  DecodeStatus DecodeScanDataHeader (const SANE_Byte *src_data, size_t src_data_len,
                                     size_t *src_data_consumed, ScanDataHeader &header);

  ScanDataHeader current_header;

  BrotherJFIFDecoder jfif_decoder;
  BrotherGrayRLengthDecoder gray_decoder;
  BrotherGrayRawDecoder gray_raw_decoder;
  BrotherInterleavedRGBColourDecoder colour_decoder;
};

class BrotherEncoderFamily3 : public BrotherEncoder
{
public:
  explicit BrotherEncoderFamily3(SANE_Word capabilities):
    colour_decoder(capabilities)
  {
  }

  ~BrotherEncoderFamily3 ()
  {
  }

  void NewPage() override
  {
    current_header.block_type = 0;

    gray_raw_decoder.NewPage(scan_params);
    gray_decoder.NewPage(scan_params);
    colour_decoder.NewPage(scan_params);
    jfif_decoder.NewPage(scan_params);
  }

  DecodeStatus EncodeQueryBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeQueryBlockResp (const SANE_Byte *data, size_t data_len,
                                     BrotherQueryResponse &response) override;

  DecodeStatus DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                  BrotherSessionResponse &response) override;

  DecodeStatus EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len, size_t *length)
      override;

  DecodeStatus DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                              BrotherBasicParamResponse &response) override;

  DecodeStatus EncodeSourceStatusBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeSourceStatusBlockResp (const SANE_Byte *data, size_t data_len,
                                            BrotherSourceStatusResponse &response) override;

  DecodeStatus EncodeSourceSelectBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeSourceSelectBlockResp (const SANE_Byte *data, size_t data_len,
                                            BrotherSourceStatusResponse &response) override;

  DecodeStatus EncodeParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus EncodeParameterBlockBlank (SANE_Byte *data, size_t data_len, size_t *length)
      override;

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written) override;

  DecodeStatus DecodeButtonQueryResp (const SANE_Byte *data, size_t data_len,
                                      BrotherButtonQueryResponse &response) override;

  DecodeStatus DecodeButtonStateResp (const SANE_Byte *data, size_t data_len,
                                      BrotherButtonStateResponse &response) override;

private:
  DecodeStatus DecodeScanDataHeader (const SANE_Byte *src_data, size_t src_data_len,
                                     size_t *src_data_consumed, ScanDataHeader &header);

  ScanDataHeader current_header;

  BrotherGrayRawDecoder gray_raw_decoder;
  BrotherGrayRLengthDecoder gray_decoder;
  BrotherInterleavedRGBColourDecoder colour_decoder;
  BrotherJFIFDecoder jfif_decoder;
};

class BrotherEncoderFamily4 : public BrotherEncoder
{
public:
  explicit BrotherEncoderFamily4(SANE_Word capabilities)
  {
    (void)capabilities;
  }

  ~BrotherEncoderFamily4 ()
  {
  }

  void NewPage() override
  {
    current_header.block_type = 0;

    jfif_decoder.NewPage(scan_params);
    gray_decoder.NewPage (scan_params);
    gray_raw_decoder.NewPage (scan_params);
  }

  DecodeStatus EncodeQueryBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeQueryBlockResp (const SANE_Byte *data, size_t data_len,
                                     BrotherQueryResponse &response) override;

  DecodeStatus DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                  BrotherSessionResponse &response) override;

  DecodeStatus EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len, size_t *length)
      override;

  DecodeStatus DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                              BrotherBasicParamResponse &response) override;

  DecodeStatus EncodeSourceStatusBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeSourceStatusBlockResp (const SANE_Byte *data, size_t data_len,
                                            BrotherSourceStatusResponse &response) override;

  DecodeStatus EncodeSourceSelectBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeSourceSelectBlockResp (const SANE_Byte *data, size_t data_len,
                                            BrotherSourceStatusResponse &response) override;

  DecodeStatus EncodeParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus EncodeParameterBlockBlank (SANE_Byte *data, size_t data_len, size_t *length)
      override;

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written) override;

  DecodeStatus DecodeButtonQueryResp (const SANE_Byte *data, size_t data_len,
                                      BrotherButtonQueryResponse &response) override;

  DecodeStatus DecodeButtonStateResp (const SANE_Byte *data, size_t data_len,
                                      BrotherButtonStateResponse &response) override;

private:
  DecodeStatus DecodeScanDataHeader (const SANE_Byte *src_data, size_t src_data_len,
                                     size_t *src_data_consumed, ScanDataHeader &header);

  ScanDataHeader current_header;

  BrotherJFIFDecoder jfif_decoder;
  BrotherGrayRLengthDecoder gray_decoder;
  BrotherGrayRawDecoder gray_raw_decoder;
};

class BrotherEncoderFamily5 : public BrotherEncoder
{
public:
  explicit BrotherEncoderFamily5(SANE_Word capabilities)
  {
    (void)capabilities;
  }

  ~BrotherEncoderFamily5 ()
  {
  }

  void NewPage() override
  {
    current_header.block_type = 0;

    jfif_decoder.NewPage(scan_params);
    gray_decoder.NewPage (scan_params);
  }

  const char* ScanModeToText (BrotherScanMode scan_mode) override;

  DecodeStatus EncodeQueryBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeQueryBlockResp (const SANE_Byte *data, size_t data_len,
                                     BrotherQueryResponse &response) override;

  DecodeStatus DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                  BrotherSessionResponse &response) override;

  DecodeStatus EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len, size_t *length)
      override;

  DecodeStatus DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                              BrotherBasicParamResponse &response) override;

  DecodeStatus EncodeSourceStatusBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeSourceStatusBlockResp (const SANE_Byte *data, size_t data_len,
                                            BrotherSourceStatusResponse &response) override;

  DecodeStatus EncodeSourceSelectBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus DecodeSourceSelectBlockResp (const SANE_Byte *data, size_t data_len,
                                            BrotherSourceStatusResponse &response) override;

  DecodeStatus EncodeParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  DecodeStatus EncodeParameterBlockBlank (SANE_Byte *data, size_t data_len, size_t *length)
      override;

  DecodeStatus DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written) override;

  DecodeStatus DecodeButtonQueryResp (const SANE_Byte *data, size_t data_len,
                                      BrotherButtonQueryResponse &response) override;

  DecodeStatus DecodeButtonStateResp (const SANE_Byte *data, size_t data_len,
                                      BrotherButtonStateResponse &response) override;

private:
  DecodeStatus DecodeScanDataHeader (const SANE_Byte *src_data, size_t src_data_len,
                                     size_t *src_data_consumed, ScanDataHeader &header);

  ScanDataHeader current_header;

  BrotherJFIFDecoder jfif_decoder;
  BrotherGrayRLengthDecoder gray_decoder;
};
