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

#include "brother_mfp_tests.h"
#include "../../backend/brother_mfp/brother_mfp-encoder.h"
#include "../genesys/minigtest.h"

/*
 * TEST: DecodeSessionResp()
 *
 */
static void test_family3_decode_session_resp()
{
  DecodeStatus decode_resp;
  BrotherSessionResponse sess_resp;

  BrotherEncoderFamily3 encoder(0);

  // SUCCESS status
  const SANE_Byte *data = (const SANE_Byte *)"\x05" "\x10" "\x01" "\x02" "\x00";

  decode_resp = encoder.DecodeSessionResp (data, 5, sess_resp);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);
  ASSERT_TRUE(sess_resp.ready);

  // BUSY status
  data = (const SANE_Byte *)"\x05" "\x10" "\x01" "\x02" "\x80";

  decode_resp = encoder.DecodeSessionResp (data, 5, sess_resp);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);
  ASSERT_FALSE(sess_resp.ready);

  // Length issues
  data = (const SANE_Byte *)"\x05" "\x10" "\x01" "\x02" "\x20" "\x32";

  decode_resp = encoder.DecodeSessionResp (data, 6, sess_resp);
  ASSERT_EQ(decode_resp, DECODE_STATUS_ERROR);

  decode_resp = encoder.DecodeSessionResp (data, 4, sess_resp);
  ASSERT_EQ(decode_resp, DECODE_STATUS_ERROR);

  decode_resp = encoder.DecodeSessionResp (data, 0, sess_resp);
  ASSERT_EQ(decode_resp, DECODE_STATUS_ERROR);

  // Content problems.
  const char *content_problems[] = {
      "\x32" "\x10" "\x01" "\x02" "\x20",
      "\x06" "\x32" "\x01" "\x02" "\x20",
      "\x06" "\x10" "\x32" "\x02" "\x20",
      "\x06" "\x10" "\x01" "\x32" "\x20",
      "\x06" "\x10" "\x01" "\x02" "\x32",
      nullptr
  };

  for (size_t test = 0; test; test++)
    {
      decode_resp = encoder.DecodeSessionResp ((const SANE_Byte *)content_problems[test], 5, sess_resp);
      ASSERT_EQ(decode_resp, DECODE_STATUS_ERROR);
    }
}

/*
 * TEST: DecodeBasicParameterBlockResp()
 *
 */
static void test_family3_decode_basic_param_resp()
{
  /*
   * TODO: Nothing to do here yet.
   * We don't decode anything from this block.
   * Watch this space.
   *
   */
}

/*
 * TEST: DecodeADFBlockResp()
 *
 */
static void test_family3_decode_sourcestatus_resp()
{
  DecodeStatus decode_resp;
  BrotherSourceStatusResponse adf_resp;

  BrotherEncoderFamily3 encoder(0);

  // SUCCESS status
  const SANE_Byte *data = (const SANE_Byte *)"\xc2";
  decode_resp = encoder.DecodeSourceStatusBlockResp (data, 1, adf_resp);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);
  ASSERT_FALSE(adf_resp.source_ready);

  data = (const SANE_Byte *)"\x80";
  decode_resp = encoder.DecodeSourceStatusBlockResp (data, 1, adf_resp);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);
  ASSERT_TRUE(adf_resp.source_ready);

  // Wrong length.
  decode_resp = encoder.DecodeSourceStatusBlockResp (data, 0, adf_resp);
  ASSERT_EQ(decode_resp, DECODE_STATUS_ERROR);

  decode_resp = encoder.DecodeSourceStatusBlockResp (data, 20, adf_resp);
  ASSERT_EQ(decode_resp, DECODE_STATUS_ERROR);
}

/*
 * TEST: EncodeBasicParameterBlock()
 *
 */
static void test_family3_encode_basic_param()
{
  DecodeStatus decode_resp;
  SANE_Byte data_buffer[1024];
  size_t ret_length;

  BrotherEncoderFamily3 encoder(0);

  // All defaults.
  decode_resp = encoder.EncodeBasicParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "I\nR=100,100\nM=CGRAY\n" "\x80";
    ASSERT_EQ(ret_length, (size_t)22);
    ASSERT_EQ(memcmp(test_ret, data_buffer, sizeof(test_ret)), 0);
  }

  // Different resolutions.
  encoder.SetRes(200,300);

  decode_resp = encoder.EncodeBasicParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "I\nR=200,300\nM=CGRAY\n" "\x80";
    ASSERT_EQ(ret_length, (size_t)22);
    ASSERT_EQ(memcmp(test_ret, data_buffer, sizeof(test_ret)), 0);
  }

  // GRAY mode.
  encoder.SetScanMode (BROTHER_SCAN_MODE_GRAY);
  decode_resp = encoder.EncodeBasicParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

    {
      const char test_ret[] = "\x1b" "I\nR=200,300\nM=GRAY64\n" "\x80";
      ASSERT_EQ(ret_length, (size_t )23);
      ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
    }

  // COLOR mode
  encoder.SetScanMode (BROTHER_SCAN_MODE_COLOR);
  decode_resp = encoder.EncodeBasicParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

    {
      const char test_ret[] = "\x1b" "I\nR=200,300\nM=CGRAY\n" "\x80";
      ASSERT_EQ(ret_length, (size_t )22);
      ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
    }

  // GRAY DITHERED mode
  encoder.SetScanMode (BROTHER_SCAN_MODE_GRAY_DITHERED);
  decode_resp = encoder.EncodeBasicParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

    {
      const char test_ret[] = "\x1b" "I\nR=200,300\nM=ERRDIF\n" "\x80";
      ASSERT_EQ(ret_length, (size_t )23);
      ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
    }

    // TEXT mode
    encoder.SetScanMode (BROTHER_SCAN_MODE_TEXT);
    decode_resp = encoder.EncodeBasicParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

    ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

      {
        const char test_ret[] = "\x1b" "I\nR=200,300\nM=TEXT\n" "\x80";
        ASSERT_EQ(ret_length, (size_t )21);
        ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
      }

      // Buffer too short.
      encoder.SetScanMode (BROTHER_SCAN_MODE_TEXT);
      decode_resp = encoder.EncodeBasicParameterBlock (data_buffer, 15, &ret_length);

      ASSERT_EQ(decode_resp, DECODE_STATUS_INVAL);
}

/*
 * TEST: EncodeParameterBlock()
 *
 */
static void test_family3_encode_param()
{
  DecodeStatus decode_resp;
  SANE_Byte data_buffer[1024];
  size_t ret_length;

  BrotherEncoderFamily3 encoder(0);

  // All defaults.
  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=100,100\nM=CGRAY\nC=JPEG\nJ=MID\n"
                            "B=50\nN=50\nA=0,0,0,0\nD=SIN\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp(test_ret, data_buffer, sizeof(test_ret)), 0);
  }

  // Different resolutions.
  encoder.SetRes(200,300);

  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=200,300\nM=CGRAY\nC=JPEG\nJ=MID\n"
                            "B=50\nN=50\nA=0,0,0,0\nD=SIN\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp(test_ret, data_buffer, sizeof(test_ret)), 0);
  }

  // Different modes:
  // GRAY mode.
  encoder.SetScanMode (BROTHER_SCAN_MODE_GRAY);
  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=200,300\nM=GRAY64\nC=RLENGTH\n"
                            "B=50\nN=50\nA=0,0,0,0\nD=SIN\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
  }

  // COLOR mode
  encoder.SetScanMode (BROTHER_SCAN_MODE_COLOR);
  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=200,300\nM=CGRAY\nC=JPEG\nJ=MID\n"
                            "B=50\nN=50\nA=0,0,0,0\nD=SIN\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
  }

  // GRAY DITHERED mode
  encoder.SetScanMode (BROTHER_SCAN_MODE_GRAY_DITHERED);
  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=200,300\nM=ERRDIF\nC=RLENGTH\n"
                            "B=50\nN=50\nA=0,0,0,0\nD=SIN\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
  }

  // TEXT mode
  encoder.SetScanMode (BROTHER_SCAN_MODE_TEXT);
  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=200,300\nM=TEXT\nC=RLENGTH\n"
                            "B=50\nN=50\nA=0,0,0,0\nD=SIN\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
  }

  // Different brightness and contrast, positive and negative.
  encoder.SetBrightness (-20);
  encoder.SetContrast (-30);
  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=200,300\nM=TEXT\nC=RLENGTH\n"
                            "B=30\nN=20\nA=0,0,0,0\nD=SIN\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
  }

  encoder.SetBrightness (50);
  encoder.SetContrast (40);
  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=200,300\nM=TEXT\nC=RLENGTH\n"
                            "B=100\nN=90\nA=0,0,0,0\nD=SIN\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
  }

  // Different dimensions
  encoder.SetScanDimensions (0, 10, 50, 100);
  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=200,300\nM=TEXT\nC=RLENGTH\n"
                            "B=100\nN=90\nA=0,50,10,150\nD=SIN\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
  }

  // ADF duplex
  encoder.SetScanDimensions (0, 10, 50, 100);
  encoder.SetSource(BROTHER_SOURCE_ADF_DUPLEX);
  decode_resp = encoder.EncodeParameterBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "X\nR=200,300\nM=TEXT\nC=RLENGTH\n"
                            "B=100\nN=90\nA=0,50,10,150\nD=DUP\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp (test_ret, data_buffer, sizeof(test_ret) - 1), 0);
  }

  // Buffer too short.
  decode_resp = encoder.EncodeParameterBlock (data_buffer, 15, &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_INVAL);
}

/*
 * TEST: EncodeADFBlock()
 *
 */
static void test_family3_encode_adf()
{
  DecodeStatus decode_resp;
  SANE_Byte data_buffer[1024];
  size_t ret_length;

  BrotherEncoderFamily3 encoder(0);

  // Standard call.
  decode_resp = encoder.EncodeSourceStatusBlock (data_buffer, sizeof(data_buffer), &ret_length);

  ASSERT_EQ(decode_resp, DECODE_STATUS_GOOD);

  {
    const char test_ret[] = "\x1b" "D\nADF\n" "\x80";
    ASSERT_EQ(ret_length, sizeof(test_ret) - 1);
    ASSERT_EQ(memcmp(test_ret, data_buffer, sizeof(test_ret)), 0);
  }

  // Buffer too short.
  decode_resp = encoder.EncodeSourceStatusBlock (data_buffer, 5, &ret_length);
  ASSERT_EQ(decode_resp, DECODE_STATUS_INVAL);
}

/*
 * Run all the tests.
 *
 */
void test_family3 ()
{
  // Decodes.
  test_family3_decode_session_resp();
  test_family3_decode_basic_param_resp();
  test_family3_decode_sourcestatus_resp();

  // Encodes.
  test_family3_encode_basic_param();
  test_family3_encode_param();
  test_family3_encode_adf();
}
