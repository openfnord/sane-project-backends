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
 * TEST: BrotherGrayRLengthDecoder()
 *
 */
static void test_gray_rlength_no_compress()
{
  BrotherGrayRLengthDecoder decoder;
  const SANE_Byte *src_data;
  SANE_Byte dest_data[1024];
  DecodeStatus res_code;
  size_t src_data_consumed;
  size_t dest_data_written;

  /*
   * Normal decode.
   *
   */
  src_data = (const SANE_Byte *)"\x04" "ABCDE";

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)6);
  ASSERT_EQ(dest_data_written, (size_t)5);

  ASSERT_EQ(memcmp(dest_data, "ABCDE", 5), 0);

  // Boundary condition: 1 character only.
  src_data = (const SANE_Byte *)"\x00" "X";

  res_code = decoder.DecodeScanData (src_data,
                                     2,
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)2);
  ASSERT_EQ(dest_data_written, (size_t)1);

  ASSERT_EQ(memcmp(dest_data, "X", 1), 0);

  /*
   * Decodes broken through src exhaustion (fragmentation of input).
   *
   */
  // Break after length: can't decode: need full miniblock.
  src_data = (const SANE_Byte *)"\x04";

  res_code = decoder.DecodeScanData (src_data,
                                     1,
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)0);
  ASSERT_EQ(dest_data_written, (size_t)0);

  // BREAK part way through the data portion: can't decode: need full miniblock.
  src_data = (const SANE_Byte *)"\x05" "ABC";

  res_code = decoder.DecodeScanData (src_data,
                                     4,
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)0);
  ASSERT_EQ(dest_data_written, (size_t)0);

  /*
   * Decodes broken by exhausted output buffer.
   *
   */
  // BREAK part way through the data portion: can't decode: need full miniblock.
  src_data = (const SANE_Byte *)"\x05" "MNOPQR";
  memset(dest_data, 0, sizeof(dest_data));

  res_code = decoder.DecodeScanData (src_data,
                                     7,
                                     &src_data_consumed,
                                     dest_data,
                                     1,
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)0);
  ASSERT_EQ(dest_data_written, (size_t)0);
}

static void test_gray_rlength_compression()
{
  BrotherGrayRLengthDecoder decoder;
  const SANE_Byte *src_data;
  SANE_Byte dest_data[1024];
  DecodeStatus res_code;
  size_t src_data_consumed;
  size_t dest_data_written;

  /*
   * Normal decode.
   *
   */
  src_data = (const SANE_Byte *)"\xfd" "A";
  memset(dest_data, 0, sizeof(dest_data));

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)2);
  ASSERT_EQ(dest_data_written, (size_t)4);

  ASSERT_EQ(memcmp(dest_data, "AAAA", 4), 0);

  // Boundary check: minimal compression: 2 chars
  src_data = (const SANE_Byte *)"\xff" "X";
  memset(dest_data, 0, sizeof(dest_data));

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)2);
  ASSERT_EQ(dest_data_written, (size_t)2);

  ASSERT_EQ(memcmp(dest_data, "XX", 2), 0);

  // Upper limit of compression: 0x80 (gives 129 bytes of output)
  src_data = (const SANE_Byte *)"\x80" "Y";
  memset(dest_data, 0, sizeof(dest_data));

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)2);
  ASSERT_EQ(dest_data_written, (size_t)129);

  ASSERT_EQ(strrchr((const char *)dest_data, 'Y'), (const char *)&dest_data[128]);

  /*
   * Fragmentation due to dest exhaustion. Can't decode: need full miniblock.
   *
   */
  src_data = (const SANE_Byte *)"\xfd" "Z";
  memset(dest_data, 0, sizeof(dest_data));

  res_code = decoder.DecodeScanData (src_data,
                                     strlen((const char *)src_data),
                                     &src_data_consumed,
                                     dest_data,
                                     1,
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)0);
  ASSERT_EQ(dest_data_written, (size_t)0);

  /*
   * Fragmentation due to src exhaustion.
   * The only case here is that we see the length, but not the
   * following character so we cannot actually do anything.
   *
   */
  src_data = (const SANE_Byte *)"\xfd" "A";
  memset(dest_data, 0, sizeof(dest_data));

  res_code = decoder.DecodeScanData (src_data,
                                     1,
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)0);
  ASSERT_EQ(dest_data_written, (size_t)0);

  res_code = decoder.DecodeScanData (src_data,
                                     2,
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)2);
  ASSERT_EQ(dest_data_written, (size_t)4);

  ASSERT_EQ(memcmp(dest_data, "AAAA", 4), 0);
}

/*
 * Run all the tests.
 *
 */
void test_gray_rlength()
{
  test_gray_rlength_no_compress();
  test_gray_rlength_compression();
}
