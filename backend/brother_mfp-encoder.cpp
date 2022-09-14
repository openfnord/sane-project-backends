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

  if (scan_mode > (sizeof(scan_mode_text) / sizeof(scan_mode_text[0])))
    {
      return nullptr;
    }

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
  const char *mode_text = ScanModeToText (scan_params.param_scan_mode);
  if (nullptr == mode_text)
    {
      DBG (DBG_SERIOUS,
           "BrotherEncoderFamily4::EncodeBasicParameterBlock: failed to get scan mode text for mode %d\n",
           scan_params.param_scan_mode);
      return SANE_STATUS_INVAL;
    }

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
                          "C=JPEG\nJ=MID\n" : "C=RLENGTH\n",
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

          DBG (DBG_IMPORTANT, "BrotherEncoderFamily4::DecodeScanData: decoded new block header: 0x%2.2x\n",
               current_header.block_type);

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
              jfif_decoder.Reset ();
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
              gray_decoder.Reset ();
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
              gray_raw_decoder.Reset ();
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
       * If we haven't consumed anything, we cannot have made any progress.
       *
       */
      if (bytes_consumed == 0)
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

void BrotherGrayRLengthDecoder::Reset ()
{
  decode_state = BROTHER_DECODE_RLEN_INIT;
  decode_expand_char = 0;
  block_bytes_left = 0;
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

  while (in_buffer_len && out_buffer_len)
    {
      /*
       * Check the current state to see what we should do.
       *
       */
      if (decode_state == BROTHER_DECODE_RLEN_INIT)
        {
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
    }

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

void BrotherJFIFDecoder::Reset ()
{
}

DecodeStatus BrotherJFIFDecoder::DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                                 size_t *src_data_consumed, SANE_Byte *dest_data,
                                                 size_t dest_data_len, size_t *dest_data_written)
{
  (void)src_data;
  (void)src_data_len;
  (void)src_data_consumed;
  (void)dest_data;
  (void)dest_data_len;
  (void)dest_data_written;

  // TODO: finish me.
  return DECODE_STATUS_ERROR;

}


void BrotherGrayRawDecoder::Reset ()
{
  // Nothing to do.
}

DecodeStatus BrotherGrayRawDecoder::DecodeScanData (const SANE_Byte *src_data, size_t src_data_len,
                                                    size_t *src_data_consumed, SANE_Byte *dest_data,
                                                    size_t dest_data_len, size_t *dest_data_written)
{
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
