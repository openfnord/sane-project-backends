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
 * TEST: BrotherJFIFDecoder()
 *
 */

static void test_colour_jpeg_normal()
{
  BrotherJFIFDecoder decoder;
  const SANE_Byte *src_data;
  SANE_Byte dest_data[1024];
  DecodeStatus res_code;
  size_t src_data_consumed;
  size_t dest_data_written;

  /*
   * Normal decode.
   *
   * Test image is 7x14 pixels.
   *
   */

#define HEX2DEC(ptr)    ((ptr) >= 'a' && (ptr) <= 'f'? (ptr) - 'a' + 10: (ptr) - '0')

  src_data = (const SANE_Byte *)"ffd8ffdb008400"
      "010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010"
      "101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101"
      "010101010101010101010101010101010101010101010101ffdd00040000ffc0001108000e000703012200021101031101ffc401a"
      "20000010501010101010100000000000000000102030405060708090a0b100002010303020403050504040000017d010203000411"
      "05122131410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a25262728292a3435363738393a434"
      "445464748494a535455565758595a636465666768696a737475767778797a838485868788898a92939495969798999aa2a3a4a5a6"
      "a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7e8e9eaf1f2f3f4f5f6f7f8f9fa010"
      "0030101010101010101010000000000000102030405060708090a0b11000201020404030407050404000102770001020311040521"
      "31061241510761711322328108144291a1b1c109233352f0156272d10a162434e125f11718191a262728292a35363738393a43444"
      "5464748494a535455565758595a636465666768696a737475767778797a82838485868788898a92939495969798999aa2a3a4a5a6"
      "a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae2e3e4e5e6e7e8e9eaf2f3f4f5f6f7f8f9faffda000"
      "c03010002110311003f00fe929ffe0a19adf807c37f1447ed15fb3378c7e0bfc56f87be1ed37c5ba47c32b3f17e8fe3b87c79e1ed"
      "5af534bb2bad03c5ba3585ae991bc7a93ada5f5bcd687ece4131b4ce92431f51a7fede07c33e1cf8a371f1f7e0c6bff03fc77f0d3"
      "40d13c5f1f810f88f4ff18cde2ef0b78866b6b5d36f741d4f4fb0b088ddc77575059ea1a6cd6919b59dc0599d63b816ff006df8bf"
      "f626fd9abc7fac6b9e20f1b780f57f14eb5e23d134ef0e6a9a8eb7f137e2bdfdc1d0f4abd4d4ac74dd38cfe3765d12de2bf8d6e9f"
      "f00b1174e7b89b2d72d36e605fe32fd8aff0066cf887ab6b7aef8e3c03a9f8a356f10f87f4af0b6ab7bac7c47f8a375249a068b73"
      "05e69ba6da2b78d045a5c30dddbc57323e951d94d7738796f249de594bfec74f8a3c2ea90a0b11c279861e55ead0a9984b2f963ff"
      "d8e341e50a587c8e38de2dc4c6385c5468e70b131cd96618b8bc5615e0f1b8454d470ff009d4b23e35929a867186a0aa29b8db170"
      "aff535cb5a30a54e3538720b1897b5a32856adec151960e8a9e1b150ad8a856fffd9";

  SANE_Byte *in_buffer = new SANE_Byte[strlen((const char *)src_data) / 2];
  SANE_Byte *in_ptr = in_buffer;

  for (const SANE_Byte *src_ptr = src_data; *src_ptr; src_ptr += 2)
    {
      *in_ptr++ = HEX2DEC(src_ptr[0]) << 4 | HEX2DEC(src_ptr[1]);
    }

  /*
   * Test JPEG image has expected width.
   *
   */
  BrotherParameters params;
  params.param_pixel_x_width = 7;

  decoder.NewPage(params);
  decoder.NewBlock();

  res_code = decoder.DecodeScanData (in_buffer,
                                     in_ptr - in_buffer,
                                     &src_data_consumed,
                                     dest_data,
                                     sizeof(dest_data),
                                     &dest_data_written);

  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
  ASSERT_EQ(src_data_consumed, (size_t)(in_ptr - in_buffer));
  ASSERT_EQ(dest_data_written, (size_t)294);

  const SANE_Byte expected_dest[] = "\xe4" "\xf0" "\xf0" "\xe8" "\xf2" "\xf4" "\xea" "\xed" "\xf6" "\xeb"
      "\xee" "\xf7" "\xeb" "\xee" "\xf7" "\xeb" "\xf0" "\xf6" "\xed" "\xf0" "\xf9" "\xea" "\xf5" "\xf7"
      "\xe7" "\xf2" "\xf6" "\xea" "\xee" "\xf7" "\xea" "\xee" "\xf7" "\xe9" "\xf0" "\xf8" "\xe8" "\xef"
      "\xf5" "\xea" "\xee" "\xf7" "\xe9" "\xf6" "\xfc" "\xe9" "\xf6" "\xfc" "\xea" "\xf5" "\xfb" "\xea"
      "\xf5" "\xfb" "\xe9" "\xf4" "\xfa" "\xe8" "\xf3" "\xf9" "\xe9" "\xf2" "\xf7" "\xdc" "\xe9" "\xff"
      "\xd9" "\xe8" "\xff" "\xdd" "\xee" "\xff" "\xe0" "\xf5" "\xff" "\xe3" "\xf7" "\xff" "\xe3" "\xf7"
      "\xff" "\xe3" "\xf6" "\xff" "\x4e" "\x59" "\xa9" "\x4c" "\x5e" "\xaa" "\x61" "\x7e" "\xc4" "\xa9"
      "\xca" "\xfd" "\xd8" "\xf8" "\xff" "\xd5" "\xf9" "\xff" "\xb2" "\xda" "\xfe" "\x55" "\x5e" "\xad"
      "\x4e" "\x5e" "\xac" "\x3f" "\x5c" "\xa8" "\x5b" "\x7f" "\xbd" "\xb2" "\xd6" "\xf6" "\xc2" "\xe9"
      "\xff" "\x76" "\xa1" "\xd6" "\xd8" "\xdf" "\xf9" "\xce" "\xdb" "\xfd" "\x90" "\xa4" "\xd6" "\x5e"
      "\x77" "\xad" "\x7b" "\x9a" "\xc6" "\xa3" "\xc3" "\xf2" "\x69" "\x88" "\xc8" "\xe9" "\xfe" "\xff"
      "\xe7" "\xfd" "\xff" "\xcf" "\xe5" "\xff" "\x76" "\x8e" "\xbe" "\x73" "\x8d" "\xbe" "\x9e" "\xb9"
      "\xf0" "\x74" "\x8c" "\xd2" "\xd1" "\xff" "\xff" "\xd5" "\xff" "\xff" "\xbd" "\xe3" "\xff" "\x6c"
      "\x8d" "\xba" "\x72" "\x8d" "\xba" "\xa0" "\xb9" "\xef" "\x76" "\x8e" "\xd4" "\xa2" "\xcf" "\xd4"
      "\x9c" "\xc6" "\xd2" "\x70" "\x92" "\xad" "\x5a" "\x74" "\x95" "\x8d" "\xa1" "\xc2" "\xb4" "\xc6"
      "\xec" "\x80" "\x91" "\xc5" "\xf3" "\xff" "\xff" "\xf5" "\xff" "\xff" "\xf7" "\xff" "\xff" "\xf8"
      "\xff" "\xff" "\xf9" "\xff" "\xff" "\xf9" "\xff" "\xff" "\xf9" "\xfe" "\xff" "\xff" "\xff" "\xff"
      "\xff" "\xff" "\xff" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xff" "\xff" "\xff"
      "\xff" "\xff" "\xff" "\xff" "\xfe" "\xfe" "\xfc" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xff" "\xff"
      "\xff" "\xff" "\xfe" "\xfe" "\xfe" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xfd"
      "\xff" "\xff" "\xfd" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff"
      "\xff" "\xff" "\xff" "\xff" ;

  ASSERT_EQ(memcmp(dest_data, expected_dest, 294), 0);

//  /*
//   * Test JPEG image has extra width: expecting 5.
//   *
//   */
//  params.param_pixel_x_width = 5;
//
//  decoder.NewPage(params);
//  decoder.NewBlock();
//
//  res_code = decoder.DecodeScanData (in_buffer,
//                                     in_ptr - in_buffer,
//                                     &src_data_consumed,
//                                     dest_data,
//                                     sizeof(dest_data),
//                                     &dest_data_written);
//
//  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
//  ASSERT_EQ(src_data_consumed, (size_t)(in_ptr - in_buffer));
//  ASSERT_EQ(dest_data_written, (size_t)210);
//
//  const SANE_Byte expected_dest2[] = "\xe4" "\xf0" "\xf0" "\xe8" "\xf2" "\xf4" "\xea" "\xed" "\xf6" "\xeb"
//      "\xee" "\xf7" "\xeb" "\xee" "\xf7" "\xea" "\xf5" "\xf7" "\xe7" "\xf2" "\xf6" "\xea" "\xee" "\xf7"
//      "\xea" "\xee" "\xf7" "\xe9" "\xf0" "\xf8" "\xe9" "\xf6" "\xfc" "\xe9" "\xf6" "\xfc" "\xea" "\xf5"
//      "\xfb" "\xea" "\xf5" "\xfb" "\xe9" "\xf4" "\xfa" "\xdc" "\xe9" "\xff" "\xd9" "\xe8" "\xff" "\xdd"
//      "\xee" "\xff" "\xe0" "\xf5" "\xff" "\xe3" "\xf7" "\xff" "\x4e" "\x59" "\xa9" "\x4c" "\x5e" "\xaa"
//      "\x61" "\x7e" "\xc4" "\xa9" "\xca" "\xfd" "\xd8" "\xf8" "\xff" "\x55" "\x5e" "\xad" "\x4e" "\x5e"
//      "\xac" "\x3f" "\x5c" "\xa8" "\x5b" "\x7f" "\xbd" "\xb2" "\xd6" "\xf6" "\xd8" "\xdf" "\xf9" "\xce"
//      "\xdb" "\xfd" "\x90" "\xa4" "\xd6" "\x5e" "\x77" "\xad" "\x7b" "\x9a" "\xc6" "\xe9" "\xfe" "\xff"
//      "\xe7" "\xfd" "\xff" "\xcf" "\xe5" "\xff" "\x76" "\x8e" "\xbe" "\x73" "\x8d" "\xbe" "\xd1" "\xff"
//      "\xff" "\xd5" "\xff" "\xff" "\xbd" "\xe3" "\xff" "\x6c" "\x8d" "\xba" "\x72" "\x8d" "\xba" "\xa2"
//      "\xcf" "\xd4" "\x9c" "\xc6" "\xd2" "\x70" "\x92" "\xad" "\x5a" "\x74" "\x95" "\x8d" "\xa1" "\xc2"
//      "\xf3" "\xff" "\xff" "\xf5" "\xff" "\xff" "\xf7" "\xff" "\xff" "\xf8" "\xff" "\xff" "\xf9" "\xff"
//      "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xfd" "\xff"
//      "\xff" "\xff" "\xfe" "\xfe" "\xfc" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff"
//      "\xfe" "\xfe" "\xfe" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xff" "\xff" "\xff"
//      "\xff" "\xff" "\xff" "\xff";
//
//  ASSERT_EQ(memcmp(dest_data, expected_dest2, 210), 0);
//
//  /*
//   * Test JPEG image has short width: expecting 9.
//   * Output will be padded with black.
//   *
//   */
//  params.param_pixel_x_width = 9;
//
//  decoder.NewPage(params);
//  decoder.NewBlock();
//
//  res_code = decoder.DecodeScanData (in_buffer,
//                                     in_ptr - in_buffer,
//                                     &src_data_consumed,
//                                     dest_data,
//                                     sizeof(dest_data),
//                                     &dest_data_written);
//
//  ASSERT_EQ(res_code, DECODE_STATUS_GOOD);
//  ASSERT_EQ(src_data_consumed, (size_t)(in_ptr - in_buffer));
//  ASSERT_EQ(dest_data_written, (size_t)378);
//
//  const SANE_Byte expected_dest3[] = "\xe4" "\xf0" "\xf0" "\xe8" "\xf2" "\xf4" "\xea" "\xed" "\xf6" "\xeb"
//      "\xee" "\xf7" "\xeb" "\xee" "\xf7" "\xeb" "\xf0" "\xf6" "\xed" "\xf0" "\xf9" "\x00" "\x00" "\x00"
//      "\x00" "\x00" "\x00" "\xea" "\xf5" "\xf7" "\xe7" "\xf2" "\xf6" "\xea" "\xee" "\xf7" "\xea" "\xee"
//      "\xf7" "\xe9" "\xf0" "\xf8" "\xe8" "\xef" "\xf5" "\xea" "\xee" "\xf7" "\x00" "\x00" "\x00" "\x00"
//      "\x00" "\x00" "\xe9" "\xf6" "\xfc" "\xe9" "\xf6" "\xfc" "\xea" "\xf5" "\xfb" "\xea" "\xf5" "\xfb"
//      "\xe9" "\xf4" "\xfa" "\xe8" "\xf3" "\xf9" "\xe9" "\xf2" "\xf7" "\x00" "\x00" "\x00" "\x00" "\x00"
//      "\x00" "\xdc" "\xe9" "\xff" "\xd9" "\xe8" "\xff" "\xdd" "\xee" "\xff" "\xe0" "\xf5" "\xff" "\xe3"
//      "\xf7" "\xff" "\xe3" "\xf7" "\xff" "\xe3" "\xf6" "\xff" "\x00" "\x00" "\x00" "\x00" "\x00" "\x00"
//      "\x4e" "\x59" "\xa9" "\x4c" "\x5e" "\xaa" "\x61" "\x7e" "\xc4" "\xa9" "\xca" "\xfd" "\xd8" "\xf8"
//      "\xff" "\xd5" "\xf9" "\xff" "\xb2" "\xda" "\xfe" "\x00" "\x00" "\x00" "\x00" "\x00" "\x00" "\x55"
//      "\x5e" "\xad" "\x4e" "\x5e" "\xac" "\x3f" "\x5c" "\xa8" "\x5b" "\x7f" "\xbd" "\xb2" "\xd6" "\xf6"
//      "\xc2" "\xe9" "\xff" "\x76" "\xa1" "\xd6" "\x00" "\x00" "\x00" "\x00" "\x00" "\x00" "\xd8" "\xdf"
//      "\xf9" "\xce" "\xdb" "\xfd" "\x90" "\xa4" "\xd6" "\x5e" "\x77" "\xad" "\x7b" "\x9a" "\xc6" "\xa3"
//      "\xc3" "\xf2" "\x69" "\x88" "\xc8" "\x00" "\x00" "\x00" "\x00" "\x00" "\x00" "\xe9" "\xfe" "\xff"
//      "\xe7" "\xfd" "\xff" "\xcf" "\xe5" "\xff" "\x76" "\x8e" "\xbe" "\x73" "\x8d" "\xbe" "\x9e" "\xb9"
//      "\xf0" "\x74" "\x8c" "\xd2" "\x00" "\x00" "\x00" "\x00" "\x00" "\x00" "\xd1" "\xff" "\xff" "\xd5"
//      "\xff" "\xff" "\xbd" "\xe3" "\xff" "\x6c" "\x8d" "\xba" "\x72" "\x8d" "\xba" "\xa0" "\xb9" "\xef"
//      "\x76" "\x8e" "\xd4" "\x00" "\x00" "\x00" "\x00" "\x00" "\x00" "\xa2" "\xcf" "\xd4" "\x9c" "\xc6"
//      "\xd2" "\x70" "\x92" "\xad" "\x5a" "\x74" "\x95" "\x8d" "\xa1" "\xc2" "\xb4" "\xc6" "\xec" "\x80"
//      "\x91" "\xc5" "\x00" "\x00" "\x00" "\x00" "\x00" "\x00" "\xf3" "\xff" "\xff" "\xf5" "\xff" "\xff"
//      "\xf7" "\xff" "\xff" "\xf8" "\xff" "\xff" "\xf9" "\xff" "\xff" "\xf9" "\xff" "\xff" "\xf9" "\xfe"
//      "\xff" "\x00" "\x00" "\x00" "\x00" "\x00" "\x00" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff"
//      "\xff" "\xfd" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff"
//      "\x00" "\x00" "\x00" "\x00" "\x00" "\x00" "\xfe" "\xfe" "\xfc" "\xff" "\xff" "\xfd" "\xff" "\xff"
//      "\xff" "\xff" "\xff" "\xff" "\xfe" "\xfe" "\xfe" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\x00"
//      "\x00" "\x00" "\x00" "\x00" "\x00" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xfd" "\xff" "\xff" "\xff"
//      "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\xff" "\x00" "\x00"
//      "\x00" "\x00" "\x00" "\x00";
//
//  ASSERT_EQ(memcmp(dest_data, expected_dest3, 378), 0);
}

/*
 * Run all the tests.
 *
 */
void test_colour_jpeg()
{
  test_colour_jpeg_normal();
}
