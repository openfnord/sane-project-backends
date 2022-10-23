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
  DECODE_STATUS_CANCEL,
  DECODE_STATUS_ERROR,
  DECODE_STATUS_MEMORY
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
  SANE_Status SetCompression (SANE_Bool compression);

  SANE_Status SetScanDimensions (SANE_Int pixel_x_offset, SANE_Int pixel_x_width, SANE_Int pixel_y_offset,
                                 SANE_Int pixel_y_height);

  static const char* ScanModeToText (BrotherScanMode scan_mode);

  virtual SANE_Status DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                         BrotherSessionResponse &response) = 0;

  virtual SANE_Status DecodeButtonQueryResp (const SANE_Byte *data, size_t data_len,
                                             BrotherButtonQueryResponse &response) = 0;

  virtual SANE_Status DecodeButtonStateResp (const SANE_Byte *data, size_t data_len,
                                             BrotherButtonStateResponse &response) = 0;

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

//  virtual SANE_Status CheckSensor(BrotherSensor &status) = 0;

protected:
  BrotherParameters scan_params;
};

#include <setjmp.h>


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
                               size_t *src_data_consumed, SANE_Byte *dst_data, size_t dest_data_len,
                               size_t *dest_data_written);


  struct CompressionState
  {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct jpeg_source_mgr src_mgr;
    bool have_read_header;
  } state;

  SANE_Byte decompress_buffer[1024 * 16];
  size_t decompress_bytes;

// TODO: Move me to the state.
  static jmp_buf my_env;

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
  BrotherInterleavedRGBColourDecoder(SANE_Word capabilities):
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
  void ConvertYCbCrToRGB (SANE_Byte y, SANE_Byte cb, SANE_Byte cr, SANE_Byte *red, SANE_Byte *green,
                          SANE_Byte *blue);

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
  BrotherEncoderFamily2(SANE_Word capabilities):
    capabilities(capabilities),
    colour_decoder(capabilities)
  {
  }

  ~BrotherEncoderFamily2 ()
  {
  }

  void NewPage() override
  {
    current_header.block_type = 0;

    jfif_decoder.NewPage(scan_params);
    gray_decoder.NewPage(scan_params);
    gray_raw_decoder.NewPage(scan_params);
    colour_decoder.NewPage(scan_params);
  }

  SANE_Status DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                 BrotherSessionResponse &response) override;

  SANE_Status EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  SANE_Status DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                             BrotherBasicParamResponse &response) override;

  SANE_Status EncodeADFBlock (SANE_Byte *data, size_t data_len, size_t *length) override
  {
    (void)data;
    (void)data_len;
    (void)length;
    return SANE_STATUS_UNSUPPORTED;
  }

  SANE_Status DecodeADFBlockResp (const SANE_Byte *data, size_t data_len,
                                  BrotherADFResponse &response) override
  {
    (void)data;
    (void)data_len;
    (void)response;

    return SANE_STATUS_UNSUPPORTED;
  }

  SANE_Status EncodeParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  SANE_Status DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                      size_t *src_data_consumed, SANE_Byte *dst_data,
                                      size_t dest_data_len, size_t *dest_data_written) override;

  SANE_Status DecodeButtonQueryResp (const SANE_Byte *data, size_t data_len,
                                     BrotherButtonQueryResponse &response) override;

  SANE_Status DecodeButtonStateResp (const SANE_Byte *data, size_t data_len,
                                     BrotherButtonStateResponse &response) override;

private:
  DecodeStatus DecodeScanDataHeader (const SANE_Byte *src_data, size_t src_data_len,
                                     size_t *src_data_consumed, ScanDataHeader &header);

  ScanDataHeader current_header;
  SANE_Word capabilities;

  BrotherJFIFDecoder jfif_decoder;
  BrotherGrayRLengthDecoder gray_decoder;
  BrotherGrayRawDecoder gray_raw_decoder;
  BrotherInterleavedRGBColourDecoder colour_decoder;
};

class BrotherEncoderFamily3 : public BrotherEncoder
{
public:
  BrotherEncoderFamily3(SANE_Word capabilities):
    capabilities(capabilities),
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

  SANE_Status DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                 BrotherSessionResponse &response) override;

  SANE_Status EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  SANE_Status DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                             BrotherBasicParamResponse &response) override;

  SANE_Status EncodeADFBlock (SANE_Byte *data, size_t data_len, size_t *length) override
  {
    (void)data;
    (void)data_len;
    (void)length;
    return SANE_STATUS_UNSUPPORTED;
  }

  SANE_Status DecodeADFBlockResp (const SANE_Byte *data, size_t data_len,
                                  BrotherADFResponse &response) override
  {
    (void)data;
    (void)data_len;
    (void)response;

    return SANE_STATUS_UNSUPPORTED;
  }

  SANE_Status EncodeParameterBlock (SANE_Byte *data, size_t data_len, size_t *length) override;

  SANE_Status DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                      size_t *src_data_consumed, SANE_Byte *dst_data,
                                      size_t dest_data_len, size_t *dest_data_written) override;

  SANE_Status DecodeButtonQueryResp (const SANE_Byte *data, size_t data_len,
                                     BrotherButtonQueryResponse &response) override;

  SANE_Status DecodeButtonStateResp (const SANE_Byte *data, size_t data_len,
                                     BrotherButtonStateResponse &response) override;

private:
  DecodeStatus DecodeScanDataHeader (const SANE_Byte *src_data, size_t src_data_len,
                                     size_t *src_data_consumed, ScanDataHeader &header);

  ScanDataHeader current_header;
  SANE_Word capabilities;

  BrotherGrayRawDecoder gray_raw_decoder;
  BrotherGrayRLengthDecoder gray_decoder;
  BrotherInterleavedRGBColourDecoder colour_decoder;
  BrotherJFIFDecoder jfif_decoder;
};

class BrotherEncoderFamily4 : public BrotherEncoder
{
public:
  BrotherEncoderFamily4(SANE_Word capabilities):
    capabilities(capabilities)
  {
  }

  ~BrotherEncoderFamily4 ()
  {
  }

  void NewPage() override
  {
    current_header.block_type = 0;

    jfif_decoder.NewPage(scan_params);
    gray_decoder.NewPage(scan_params);
    gray_raw_decoder.NewPage(scan_params);
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

  SANE_Status DecodeButtonQueryResp (const SANE_Byte *data, size_t data_len,
                                             BrotherButtonQueryResponse &response) override;

  SANE_Status DecodeButtonStateResp (const SANE_Byte *data, size_t data_len,
                                             BrotherButtonStateResponse &response) override;



//  SANE_Status CheckSensor(BrotherSensor &status) override;

private:
  DecodeStatus DecodeScanDataHeader (const SANE_Byte *src_data, size_t src_data_len,
                                     size_t *src_data_consumed, ScanDataHeader &header);

  ScanDataHeader current_header;
  SANE_Word capabilities;

  BrotherJFIFDecoder jfif_decoder;
  BrotherGrayRLengthDecoder gray_decoder;
  BrotherGrayRawDecoder gray_raw_decoder;
};
