/* sane - Scanner Access Now Easy.

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
*/

#define DEBUG_DECLARE_ONLY

#include "../../include/sane/sanei_debug.h"
#include "brother_mfp_tests.h"
#include "../../backend/brother_mfp/brother_mfp-encoder.h"
#include "../genesys/minigtest.h"

/*
 * TEST: BrotherInterleavedRGBColourDecoder()
 *
 */
static void test_colour_int_rgb_enc_rgb()
{
  BrotherInterleavedRGBColourDecoder decoder(CAP_MODE_COLOUR);
  const SANE_Byte *src_data;
  SANE_Byte dest_data[1024];
  DecodeStatus res_code;
  size_t src_data_consumed;
  size_t dest_data_written;

  /*
   * Normal decode.
   *
   */
  BrotherParameters params;
  params.param_pixel_x_width = 6;

  decoder.NewPage(params);
  src_data = (const SANE_Byte *)"123456";

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)6);
  ASSERT_EQ(dest_data_written, (size_t)0);

  decoder.NewBlock();
  src_data = (const SANE_Byte *)"ABCDEF";

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)6);
  ASSERT_EQ(dest_data_written, (size_t)0);

  decoder.NewBlock();
  src_data = (const SANE_Byte *)"MNOPQR";

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)6);
  ASSERT_EQ(dest_data_written, (size_t)18);

  ASSERT_EQ(memcmp(dest_data, "1AM2BN3CO4DP5EQ6FR", 18), 0);
}



static void test_colour_int_rgb_enc_ycrcb()
{
  BrotherInterleavedRGBColourDecoder decoder(CAP_MODE_COLOUR | CAP_ENCODING_RAW_IS_CrYCb);
  const SANE_Byte *src_data;
  SANE_Byte dest_data[1024];
  DecodeStatus res_code;
  size_t src_data_consumed;
  size_t dest_data_written;

  /*
   * Normal decode.
   *
   */
  BrotherParameters params;
  params.param_pixel_x_width = 7;

  decoder.NewPage(params);
  decoder.NewBlock();
  src_data = (const SANE_Byte *)"\x5B" "\x63" "\x60" "\x5D" "\x5D" "\x62" "\x6A";

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)7);
  ASSERT_EQ(dest_data_written, (size_t)0);

  decoder.NewBlock();
  src_data = (const SANE_Byte *)"\xDD" "\xE9" "\xEE" "\xEF" "\xED" "\xED" "\xEB";

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)7);
  ASSERT_EQ(dest_data_written, (size_t)0);

  decoder.NewBlock();
  src_data = (const SANE_Byte *)"\x70" "\x6E" "\x6D" "\x6E" "\x6E" "\x6E" "\x6D";

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)7);
  ASSERT_EQ(dest_data_written, (size_t)21);

  // YCbCr data converted to RGB:
  ASSERT_EQ(memcmp (dest_data,
                    "\xa9" "\xfc" "\xc0" "\xc0" "\xff" "\xc9" "\xc1" "\xff"
                    "\xcc" "\xbd" "\xff" "\xcf" "\xbb" "\xff" "\xcd" "\xc2"
                    "\xff" "\xcd" "\xcc" "\xff" "\xc9",
                    21),
            0);
}
/*
 * Run all the tests.
 *
 */
void test_colour_int_rgb()
{
  test_colour_int_rgb_enc_rgb();
  test_colour_int_rgb_enc_ycrcb();
}
