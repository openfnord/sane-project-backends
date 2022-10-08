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

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_usb.h"
#include "../include/sane/sanei_debug.h"

#include "brother_mfp-common.h"
#include "brother_mfp-encoder.h"

/*
 * Block header identifiers.
 *
 */
#define BROTHER_DATA_BLOCK_NO_DATA              0x80
#define BROTHER_DATA_BLOCK_END_OF_FRAME         0x82
#define BROTHER_DATA_BLOCK_JPEG                 0x64
#define BROTHER_DATA_BLOCK_GRAY_RLENGTH         0x42
#define BROTHER_DATA_BLOCK_GRAY_RAW             0x40

const char* BrotherEncoder::ScanModeToText (BrotherScanMode scan_mode)
{
  static const char *scan_mode_text[] =
    { "CGRAY", "GRAY64", "ERRDIF", "TEXT" };

  return scan_mode_text[scan_mode];
}


SANE_Status BrotherEncoder::SetScanMode (BrotherScanMode scan_mode)
{

  /*
   * TODO: Perhaps check the validity of the parameter?
   */
  scan_params.param_scan_mode = scan_mode;

  return SANE_STATUS_GOOD;
}

SANE_Status BrotherEncoder::SetRes (SANE_Int x, SANE_Int y)
{

  /*
   * TODO: Perhaps check the validity of the parameter?
   */
  scan_params.param_x_res = x;
  scan_params.param_y_res = y;

  return SANE_STATUS_GOOD;
}

SANE_Status BrotherEncoder::SetContrast (SANE_Int contrast)
{

  /*
   * TODO: Perhaps check the validity of the parameter?
   */
  scan_params.param_contrast = contrast;

  return SANE_STATUS_GOOD;
}

SANE_Status BrotherEncoder::SetBrightness (SANE_Int brightness)
{

  /*
   * TODO: Perhaps check the validity of the parameter?
   */
  scan_params.param_brightness = brightness;

  return SANE_STATUS_GOOD;
}

SANE_Status
BrotherEncoder::SetScanDimensions (SANE_Int pixel_x_offset,
                                  SANE_Int pixel_x_width,
                                  SANE_Int pixel_y_offset,
                                  SANE_Int pixel_y_height)
{

  /*
   * TODO: Perhaps check the validity of the parameter?
   */
  scan_params.param_pixel_x_offset = pixel_x_offset;
  scan_params.param_pixel_x_width = pixel_x_width;
  scan_params.param_pixel_y_offset = pixel_y_offset;
  scan_params.param_pixel_y_height = pixel_y_height;

  DBG (DBG_SERIOUS,
       "BrotherEncoder::SetScanDimensions: scan dimensions: x:%d,%d; y:%d,%d\n",
       pixel_x_offset,
       pixel_x_width,
       pixel_y_offset,
       pixel_y_height);

  return SANE_STATUS_GOOD;
}

/*
 * --------------------------------------------
 * Family 4 Encoding
 * --------------------------------------------
 *
 */
SANE_Status BrotherEncoderFamily4::DecodeSessionResp (const SANE_Byte *data, size_t data_len,
                                                      BrotherSessionResponse &response)
{
  /*
   * Formatting and content checks.
   *
   *
   */
  if ((data_len != 5)
      || ((memcmp (data, "\x05" "\x10" "\x01" "\x02", 4) != 0)
          && (memcmp (data, "\x05" "\x10" "\x02" "\x02", 4) != 0)))

    {
      DBG (DBG_SERIOUS,
           "BrotherEncoderFamily4::DecodeSessionResp: invalid session response block: len = %zu\n",
           data_len);

      return SANE_STATUS_IO_ERROR;
    }

  /*
   * TODO: Check the valid status values are 0x00 or 0x20
   *
   */
  if ((data[4] != 0x00) && (data[4] != 0x20))

    {
      DBG (DBG_SERIOUS,
           "BrotherEncoderFamily4::DecodeSessionResp: invalid session response: data[4]=%u\n",
           (unsigned int) data[4]);

      return SANE_STATUS_IO_ERROR;
    }

  /*
   * TODO: figure out what the rest of the packet is supposed to be.
   *
   */
  response.ready = data[4] == 0x00? SANE_TRUE: SANE_FALSE;

  return SANE_STATUS_GOOD;
}

/*
 * NOTE:
 * Supply a buffer that is at least 1 char longer than is necessary
 * as snprintf() will require space for a terminating NUL.
 *
 */
SANE_Status BrotherEncoderFamily4::EncodeBasicParameterBlock (SANE_Byte *data, size_t data_len,
                                                              size_t *length)
{
  const char *mode_text = ScanModeToText (scan_params.param_scan_mode);
  if (nullptr == mode_text)
    {
      DBG (DBG_SERIOUS,
           "BrotherEncoderFamily4::EncodeBasicParameterBlock: failed to get scan mode text for mode %d\n",
           scan_params.param_scan_mode);
      return SANE_STATUS_INVAL;
    }

  *length = snprintf ((char*) data,
                      data_len,
                      "\x1b" "I\nR=%u,%u\nM=%s\n" "\x80",
                      (unsigned int) scan_params.param_x_res,
                      (unsigned int) scan_params.param_y_res,
                      mode_text);

  if (*length > data_len)
    {
      DBG (DBG_SERIOUS,
           "BrotherEncoderFamily4::EncodeBasicParameterBlock: parameter block too long for buffer: %zu\n",
           *length);
      return SANE_STATUS_INVAL;
    }

  return SANE_STATUS_GOOD;
}

SANE_Status BrotherEncoderFamily4::DecodeBasicParameterBlockResp (const SANE_Byte *data, size_t data_len,
                                                                  BrotherBasicParamResponse &response)
{
  /*
   * TODO: Decode this block.
   * Might contain some useful stuff, like limits for selected parameters.
   *
   */
  (void)data;
  (void)data_len;
  (void)response;

  return SANE_STATUS_GOOD;
}

SANE_Status BrotherEncoderFamily4::EncodeADFBlock (SANE_Byte *data, size_t data_len, size_t *length)
{
  *length = snprintf ((char*) data, data_len, "\x1b" "D\nADF\n" "\x80");

  if (*length > data_len)
    {
      DBG (DBG_SERIOUS,
           "BrotherEncoderFamily4::EncodeBasicParameterBlock: parameter block too long for buffer: %zu\n",
           *length);
      return SANE_STATUS_INVAL;
    }

  return SANE_STATUS_GOOD;
}

SANE_Status BrotherEncoderFamily4::DecodeADFBlockResp (const SANE_Byte *data, size_t data_len,
                                                       BrotherADFResponse &response)
{
  if ((data_len != 1))
    {
      return SANE_STATUS_IO_ERROR;
    }

  response.resp_code = data[0];

  return SANE_STATUS_GOOD;
}

SANE_Status BrotherEncoderFamily4::EncodeParameterBlock (SANE_Byte *data, size_t data_len,
                                                         size_t *length)
{
  const char *mode_text = ScanModeToText (scan_params.param_scan_mode);
  if (nullptr == mode_text)
    {
      DBG (DBG_SERIOUS,
           "BrotherEncoderFamily4::EncodeBasicParameterBlock: failed to get scan mode text for mode %d\n",
           scan_params.param_scan_mode);
      return SANE_STATUS_INVAL;
    }

  *length = snprintf ((char*) data,
                      data_len,
                      "\x1b" "X\nR=%u,%u\nM=%s\n"
                      "%s\n"
                      "B=%u\nN=%u\n"
                      "A=%u,%u,%u,%u\n"
                      "S=NORMAL_SCAN\n"
                      "P=0\nG=0\nL=0\n"
                      "\x80",
                      (unsigned int) scan_params.param_x_res,
                      (unsigned int) scan_params.param_y_res,
                      mode_text,
                      (scan_params.param_scan_mode == BROTHER_SCAN_MODE_COLOR) ?
                          "C=JPEG\nJ=MID" : "C=RLENGTH",
                      (unsigned int) (scan_params.param_brightness + 50),
                      (unsigned int) (scan_params.param_contrast + 50),
                      (unsigned int) (scan_params.param_pixel_x_offset),
                      (unsigned int) (scan_params.param_pixel_y_offset),
                      (unsigned int) (scan_params.param_pixel_x_offset
                          + scan_params.param_pixel_x_width),
                      (unsigned int) (scan_params.param_pixel_y_offset
                          + scan_params.param_pixel_y_height));

  if (*length > data_len)
    {
      DBG (DBG_SERIOUS,
           "BrotherEncoderFamily4::EncodeBasicParameterBlock: parameter block too long for buffer: %zu\n",
           *length);
      return SANE_STATUS_INVAL;
    }

  return SANE_STATUS_GOOD;
}

SANE_Status BrotherEncoderFamily4::DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                                   size_t *src_data_consumed, SANE_Byte *dest_data,
                                                   size_t dest_data_len, size_t *dest_data_written)
{
  DBG (DBG_EVENT, "BrotherEncoderFamily4::DecodeScanData: \n");
  SANE_Status ret_status = SANE_STATUS_GOOD;

  *src_data_consumed = 0;
  *dest_data_written = 0;

  /*
   * We will use this to compute the bytes consumed at the end.
   *
   */
  size_t orig_src_data_len = src_data_len;
  size_t orig_dest_data_len = dest_data_len;

  /*
   * Now take note here that we do not *necessarily* need src_data to be able to decode.
   * We might have terminated the previous call not because we ran out of input, but because
   * we ran out of output space. Some blocks are just repetitions that are unrolled and require
   * no additional src data. So we enter this loop *even if there is nothing in the src_data buffer*.
   *
   * All of the functions that we are about to call must be able to deal with src_data_len == 0
   * intelligently.
   *
   */
  for (;;)
    {
      /*
       * If we are not in a block, then decode the next block.
       *
       * We might not have enough bytes to fully decode the header.
       * In which case, we wait for some more data.
       *
       */
      bool new_block = false;

      if (current_header.block_type == 0)
	{
	  size_t header_consumed = 0;
	  DecodeStatus res = DecodeScanDataHeader(src_data, src_data_len, &header_consumed, current_header);
	  if (res == DECODE_STATUS_TRUNCATED)
	    {
	      /*
	       * This means we don't have enough data to decode a header yet.
	       * Try again next time if we have read more data.
	       *
	       */
	      break;
	    }

	  /*
           * Detect special case situations.
           *
           * TODO: We need to be able to alert the difference between these
           * two things to the caller somehow. They are not the same!!
           *
           */
          if (res == DECODE_STATUS_ENDOFDATA)
            {
              DBG (DBG_IMPORTANT, "BrotherEncoderFamily4::DecodeScanData: end of data detected\n");
              current_header.block_type = 0;

              /*
               * If we have data in hand, then we will send this up first.
               * Otherwise, send SANE_STATUS_EOF to trigger stop of scan.
               *
               * We don't consume this header so we will see it the next time around.
               *
               */
              ret_status = orig_dest_data_len > dest_data_len? SANE_STATUS_GOOD: SANE_STATUS_EOF;
              break;
            }
          if (res == DECODE_STATUS_ENDOFFRAME)
            {
              DBG (DBG_IMPORTANT, "BrotherEncoderFamily4::DecodeScanData: end of frame detected\n");
              current_header.block_type = 0;
              ret_status = orig_dest_data_len > dest_data_len? SANE_STATUS_GOOD: SANE_STATUS_EOF;
              break;
            }
          if (res != DECODE_STATUS_GOOD)
            {
              DBG (DBG_IMPORTANT, "BrotherEncoderFamily4::DecodeScanData: failed to decode header\n");
              current_header.block_type = 0;
              ret_status = SANE_STATUS_IO_ERROR;
              break;
            }

          DBG (DBG_IMPORTANT,
               "BrotherEncoderFamily4::DecodeScanData: decoded new block header: 0x%2.2x, length=%zu\n",
               current_header.block_type,
               current_header.block_len);

          src_data += header_consumed;
          src_data_len -= header_consumed;

          new_block = true;
	}

      /*
       * Attempt to to decode the data block.
       * We will exit the function if we can only partially
       * decode it, hoping to do more next time.
       *
       */
      DecodeStatus res;
      size_t bytes_consumed = 0;
      size_t bytes_written = 0;
      size_t in_len = MIN(src_data_len, current_header.block_len);

      if (current_header.block_type == BROTHER_DATA_BLOCK_JPEG)
        {
          if (new_block)
            {
              jfif_decoder.NewBlock ();
            }

          res = jfif_decoder.DecodeScanData (src_data,
                                             in_len,
                                             &bytes_consumed,
                                             dest_data,
                                             dest_data_len,
                                             &bytes_written);
        }
      else if (current_header.block_type == BROTHER_DATA_BLOCK_GRAY_RLENGTH)
        {
          if (new_block)
            {
              gray_decoder.NewBlock ();
            }

          res = gray_decoder.DecodeScanData (src_data,
                                             in_len,
                                             &bytes_consumed,
                                             dest_data,
                                             dest_data_len,
                                             &bytes_written);
        }
      else if (current_header.block_type == BROTHER_DATA_BLOCK_GRAY_RAW)
        {
          if (new_block)
            {
              gray_raw_decoder.NewBlock ();
            }

          res = gray_raw_decoder.DecodeScanData (src_data,
                                             in_len,
                                             &bytes_consumed,
                                             dest_data,
                                             dest_data_len,
                                             &bytes_written);
        }
      else
        {
          DBG (DBG_IMPORTANT,
               "BrotherEncoderFamily4::DecodeScanData: unknown block encountered: 0x%2.2x\n",
               (unsigned int) current_header.block_type);
          ret_status = SANE_STATUS_IO_ERROR;
          break;
        }

      src_data += bytes_consumed;
      src_data_len -= bytes_consumed;
      current_header.block_len -= bytes_consumed;

      DBG (DBG_IMPORTANT,
           "BrotherEncoderFamily4::DecodeScanData: bytes remaining after decode=%zu\n",
           current_header.block_len);

      dest_data += bytes_written;
      dest_data_len -= bytes_written;

      if (res != DECODE_STATUS_GOOD)
        {
          current_header.block_type = 0;
          ret_status = SANE_STATUS_IO_ERROR;
          break;
        }

      /*
       * Perhaps we have completed the block.
       *
       */
      if (current_header.block_len == 0)
        {
          current_header.block_type = 0;
        }

      /*
       * Deal with special situation whereby we have input
       * and output buffer space, but we couldn't decode anything.
       * It might be that there is data in the input buffer
       * but we need more to proceed.
       *
       * If we haven't written anything, we cannot have made any progress.
       *
       */
      if (bytes_written == 0)
        {
          break;
        }
    }

  *src_data_consumed = orig_src_data_len - src_data_len;
  *dest_data_written = orig_dest_data_len - dest_data_len;

  return ret_status;
}


/*
 * EndOfFrame (0x82) is 10 bytes long because it doesn't have the
 * 2-byte packet length that the others do, so we could assume that
 * the packet length is not part of the header really.
 * However, it is convenient in coding terms to assume that it does.
 *
 * 0x80 - single byte end of data.
 * 0x82 - end of frame, with 10 bytes
 * Others - 12 bytes, including little endian length at the end.
 *
 */
DecodeStatus BrotherEncoderFamily4::DecodeScanDataHeader (const SANE_Byte *src_data,
                                                          size_t src_data_len,
                                                          size_t *src_data_consumed,
                                                          ScanDataHeader &header)
{
  if (src_data_len == 0)
    {
      return DECODE_STATUS_TRUNCATED;
    }

  header.block_type = src_data[0];

  header.block_len = 0;

  if (header.block_type == BROTHER_DATA_BLOCK_NO_DATA)
    {
      *src_data_consumed = 1;
      return DECODE_STATUS_ENDOFDATA;
    }

  if (header.block_type == BROTHER_DATA_BLOCK_END_OF_FRAME)
    {
      if (src_data_len < 10)
        {
          return DECODE_STATUS_TRUNCATED;
        }

      *src_data_consumed = 10;
      return DECODE_STATUS_ENDOFFRAME;
    }

  /*
   * Other data-carrying packets.
   *
   */
  if (src_data_len < 12)
    {
      return DECODE_STATUS_TRUNCATED;
    }

  *src_data_consumed = 12;

  header.block_len = src_data[10] + (src_data[11] << 8);

  return DECODE_STATUS_GOOD;
}

void BrotherGrayRLengthDecoder::NewBlock ()
{
  decode_state = BROTHER_DECODE_RLEN_INIT;
  decode_expand_char = 0;
  block_bytes_left = 0;
}

void BrotherGrayRLengthDecoder::NewPage ()
{
  NewBlock();
}

/*
 * Returns:
 *
 * DECODE_STATUS_GOOD - block all decoded.
 * DECODE_STATUS_ERROR - error detected in data.
 *
 */
DecodeStatus BrotherGrayRLengthDecoder::DecodeScanData (const SANE_Byte *in_buffer,
                                                        size_t in_buffer_len,
                                                        size_t *bytes_consumed,
                                                        SANE_Byte *out_buffer,
                                                        size_t out_buffer_len,
                                                        size_t *bytes_written)
{
  *bytes_consumed = 0;
  *bytes_written = 0;

  size_t in_buffer_len_start = in_buffer_len;
  size_t out_buffer_len_start = out_buffer_len;

  /*
   * Notice that we do the check for in and out space at the bottom, not here:
   * We might not have any additional src, but we might still be able to progress the decode,
   * especially in the case of an in-progress decompression.
   *
   */
  do
    {
      /*
       * Check the current state to see what we should do.
       *
       */
      if (decode_state == BROTHER_DECODE_RLEN_INIT)
        {
          /*
           * We need src to be able to decode.
           *
           */
          if (in_buffer_len == 0)
            {
              break;
            }

          /*
           * INIT state: we are ready to start to process another sub-block.
           *
           * If there is data then we should see a block length.
           *
           * If byte & 0x80 then it is a compressed byte block.
           * Else it is a sequence of uncompressed bytes.
           *
           */
          if (in_buffer[0] & 0x80)
            {
              /*
               * To move into the BROTHER_DECODE_RLEN_IN_EXPAND, we have to see the next byte.
               * We might not have it yet. We will just have to wait for more bytes to come in.
               *
               */
              if (in_buffer_len < 2)
                {
                  DBG (DBG_IMPORTANT, "Brother_decode_gray_rlength: in_buffer_len < 2\n");
                  break;
                }

              decode_state = BROTHER_DECODE_RLEN_IN_EXPAND;
              block_bytes_left = 0xff - in_buffer[0] + 2;
              decode_expand_char = in_buffer[1];

              in_buffer += 2;
              in_buffer_len -= 2;

              DBG (DBG_IMPORTANT,
                   "Brother_decode_gray_rlength: expand block: %zu bytes of 0x%2.2x\n",
                   block_bytes_left,
                   (int) decode_expand_char);

            }
          else
            {
              decode_state = BROTHER_DECODE_RLEN_IN_BYTES;
              block_bytes_left = in_buffer[0] + 1;

              in_buffer += 1;
              in_buffer_len -= 1;

              DBG (DBG_IMPORTANT,
                   "Brother_decode_gray_rlength: bytes block: %zu bytes\n",
                   block_bytes_left);
            }
        }

      /*
       * Extraction phase, where we try to decode data.
       * We should be in a data extraction mode now.
       *
       */
      size_t bytes_to_copy = MIN(out_buffer_len, block_bytes_left);

      if (bytes_to_copy)
        {
          size_t consumed = 0;

          if (decode_state == BROTHER_DECODE_RLEN_IN_BYTES)
            {
              bytes_to_copy = MIN(bytes_to_copy, in_buffer_len);
              (void) memcpy (out_buffer, in_buffer, bytes_to_copy);
              consumed = bytes_to_copy;
            }
          else if (decode_state == BROTHER_DECODE_RLEN_IN_EXPAND)
            {
              (void) memset (out_buffer, decode_expand_char, bytes_to_copy);
            }

          in_buffer += consumed;
          in_buffer_len -= consumed;
          out_buffer += bytes_to_copy;
          out_buffer_len -= bytes_to_copy;
          block_bytes_left -= bytes_to_copy;
          if (block_bytes_left == 0)
            {
              decode_state = BROTHER_DECODE_RLEN_INIT;
            }
        }
    } while (in_buffer_len && out_buffer_len);

  if (!in_buffer_len || !out_buffer_len)
    {
      DBG (DBG_IMPORTANT,
           "Brother_decode_gray_rlength: ran out of buffer: in %zu out %zu\n",
           in_buffer_len,
           out_buffer_len);

    }
  *bytes_consumed = in_buffer_len_start - in_buffer_len;
  *bytes_written = out_buffer_len_start - out_buffer_len;

  return DECODE_STATUS_GOOD;
}


#include <jpeglib.h>
#include <jerror.h>

void BrotherJFIFDecoder::NewBlock()
{
  // Nothing to do.
}

void BrotherJFIFDecoder::NewPage ()
{
  /*
   * Aborting a non-running state should be OK.
   *
   */
  jpeg_abort_decompress(&state.cinfo);
  state.have_read_header = false;
}

void BrotherJFIFDecoder::InitSource (j_decompress_ptr cinfo)
{
  DBG (DBG_EVENT, "BrotherJFIFDecoder::InitSource.\n");

  (void)cinfo;

  /*
   * We will have already setup all the available data.
   *
   */
  return;
}

boolean BrotherJFIFDecoder::FillInputBuffer(j_decompress_ptr cinfo)
{
  (void)cinfo;

  /*
   * We can not supply more data.
   * We setup the data buffer already with everything we have.
   *
   */
  DBG (DBG_EVENT, "BrotherJFIFDecoder::FillInputBuffer.\n");

  return FALSE;
}

void BrotherJFIFDecoder::SkipInputData(j_decompress_ptr cinfo, long num_bytes)
{
  (void)cinfo;
  (void)num_bytes;

  /*
   * TODO: Note the special attention required here.
   * num_bytes might exceed the number of bytes in the src_data so we must remember
   * the excess so that we can discard it when we add more data later!!!!!!!
   *
   */
  DBG (DBG_EVENT, "BrotherJFIFDecoder::SkipInputData.\n");

}

void BrotherJFIFDecoder::TermSource(j_decompress_ptr cinfo)
{
  (void)cinfo;
  DBG (DBG_EVENT, "BrotherJFIFDecoder::TermSource.\n");

}


jmp_buf BrotherJFIFDecoder::my_env;

DecodeStatus BrotherJFIFDecoder::DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                                 size_t *src_data_consumed, SANE_Byte *dest_data,
                                                 size_t dest_data_len, size_t *dest_data_written)
{
  DBG (DBG_EVENT, "BrotherJFIFDecoder::DecodeScanData\n");


  *src_data_consumed = 0;
  *dest_data_written = 0;


  /*
   * Initialise read buffer.
   *
   */
  state.src_mgr.bytes_in_buffer = src_data_len;
  state.src_mgr.next_input_byte = src_data;

  if (setjmp(my_env) != 0)
    {
      DBG (DBG_EVENT, "BrotherJFIFDecoder::DecodeScanData: setjmp error return\n");
      return DECODE_STATUS_ERROR;
    }

  /*
   * Read header if we haven't yet.
   *
   */
  if (!state.have_read_header)
    {
      DBG (DBG_EVENT, "BrotherJFIFDecoder::DecodeScanData: Trying to read header.\n");

      int read_status = jpeg_read_header(&state.cinfo, TRUE);
      if (read_status == JPEG_SUSPENDED)
	{
	  return DECODE_STATUS_GOOD;
	}
      else if (read_status != JPEG_HEADER_OK)
	{
	  return DECODE_STATUS_ERROR;
	}
      state.have_read_header = true;

      DBG (DBG_EVENT, "BrotherJFIFDecoder::DecodeScanData: Starting decompress.\n");

      if (!jpeg_start_decompress(&state.cinfo))
	{
	  /*
	   * Need more data.
	   *
	   */
	  return DECODE_STATUS_GOOD;
	}

      /*
       * Make sure that we will be getting the data that we expect.
       * This is for colour only, so we expect 3 components.
       *
       */
      if (state.cinfo.out_color_components != 3)
	{
	  return DECODE_STATUS_ERROR;
	}

      /*
       * TODO: Perhaps also check the dimensions are what we are expecting.
       * Depending on the issues related to width alignment, we might
       * have to do some trimming on the scan lines if we had to round up the
       * the width and/or round down the left margin.
       *
       */
    }

  /*
   * Let's try to decompress some JPEG!
   *
   */
  DBG (DBG_EVENT, "BrotherJFIFDecoder::DecodeScanData: Converting data.\n");

  SANE_Byte *output_ptr = dest_data;

  const size_t line_size = sizeof(JSAMPLE) * state.cinfo.output_width * state.cinfo.out_color_components;

  size_t output_size = dest_data_len;

  while (output_size >= line_size)
    {
      /*
       * Estimate the number of lines we can generate from
       * the available output space.
       *
       */
//      JDIMENSION max_lines = dest_data_len / (state.cinfo.output_width * 3);

      JDIMENSION converted = jpeg_read_scanlines (&state.cinfo, &output_ptr, 1);

      DBG (DBG_IMPORTANT,
           "BrotherJFIFDecoder::DecodeScanData: Convert %u scanlines. Remaining scanlines: %u\n",
           (unsigned int) converted, (unsigned int)(state.cinfo.output_height - state.cinfo.output_scanline));

      output_size -= line_size;
      output_ptr += converted * line_size;

      if (converted == 0)
        {
          break;
        }
    }

  /*
   * Account for what we have processed.
   *
   * Note that libjpeg will have altered state.src_mgr.bytes_in_buffer
   * to a backtracked position for it to restart again.
   * This is why it is CRUCIAL that we do not try to guess.
   *
   */
  *dest_data_written = output_ptr - dest_data;
  *src_data_consumed = src_data_len - state.src_mgr.bytes_in_buffer;

  DBG (DBG_IMPORTANT,
       "BrotherJFIFDecoder::DecodeScanData: Finished: consumed %zu bytes, written %zu bytes\n",
       *src_data_consumed,
       *dest_data_written);

  /*
   * If we have output all the scanlines, then we need to finish up.
   *
   */
  if (state.cinfo.output_scanline >= state.cinfo.output_height)
    {
      DBG (DBG_EVENT, "BrotherJFIFDecoder::DecodeScanData: Converted all scanlines. Finishing.\n");

      if (!jpeg_finish_decompress(&state.cinfo))
        {
          return DECODE_STATUS_ERROR;
        }
    }

  return DECODE_STATUS_GOOD;
}


void BrotherGrayRawDecoder::NewPage ()
{
  // Nothing to do.
}

void BrotherGrayRawDecoder::NewBlock()
{
  // Nothing to do.
}

DecodeStatus BrotherGrayRawDecoder::DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                                    size_t *src_data_consumed, SANE_Byte *dest_data,
                                                    size_t dest_data_len, size_t *dest_data_written)
{
  /*
   * Quick check. This isn't strictly necessary because the following code copes
   * properly with src_data_len == 0, but we might change this code in the future
   * and we might not realise that it must.
   *
   * At least we make it clear here that we need src_data to do something useful.
   *
   */
  if (src_data_len == 0)
    {
      return DECODE_STATUS_GOOD;
    }

  /*
   * Just copy to the output what we can from the input.
   * It's just raw data.
   *
   */
  size_t copy_len = MIN(src_data_len, dest_data_len);

  (void)memcpy(dest_data, src_data, copy_len);
  *dest_data_written = copy_len;
  *src_data_consumed = copy_len;

  return DECODE_STATUS_GOOD;
}
