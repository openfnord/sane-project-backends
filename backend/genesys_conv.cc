/* sane - Scanner Access Now Easy.

   Copyright (C) 2005, 2006 Pierre Willenbrock <pierre@pirsoft.dnsalias.org>
   Copyright (C) 2010-2013 Stéphane Voltz <stef.dev@free.fr>

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.
*/

/*
 * Conversion filters for genesys backend
 */


/*8 bit*/
#define SINGLE_BYTE
#define BYTES_PER_COMPONENT 1
#define COMPONENT_TYPE uint8_t

#define FUNC_NAME(f) f ## _8

#include "genesys_conv_hlp.cc"

#undef FUNC_NAME

#undef COMPONENT_TYPE
#undef BYTES_PER_COMPONENT
#undef SINGLE_BYTE

/*16 bit*/
#define DOUBLE_BYTE
#define BYTES_PER_COMPONENT 2
#define COMPONENT_TYPE uint16_t

#define FUNC_NAME(f) f ## _16

#include "genesys_conv_hlp.cc"

#undef FUNC_NAME

#undef COMPONENT_TYPE
#undef BYTES_PER_COMPONENT
#undef DOUBLE_BYTE

static void genesys_reverse_bits(uint8_t* src_data, uint8_t* dst_data, size_t bytes)
{
    DBG_HELPER(dbg);
    size_t i;
    for(i = 0; i < bytes; i++) {
	*dst_data++ = ~ *src_data++;
    }
}

/**
 * uses the threshold/threshold_curve to control software binarization
 * This code was taken from the epjistsu backend by m. allan noah
 * @param dev device set up for the scan
 * @param src pointer to raw data
 * @param dst pointer where to store result
 * @param width width of the processed line
 * */
static void binarize_line(Genesys_Device* dev, uint8_t* src, uint8_t* dst, int width)
{
    DBG_HELPER(dbg);
  int j, windowX, sum = 0;
  int thresh;
  int offset, addCol, dropCol;
  unsigned char mask;

  int x;
  uint8_t min, max;

  /* normalize line */
  min = 255;
  max = 0;
    for (x = 0; x < width; x++)
      {
	if (src[x] > max)
	  {
	    max = src[x];
	  }
	if (src[x] < min)
	  {
	    min = src[x];
	  }
      }

    /* safeguard against dark or white areas */
    if(min>80)
	    min=0;
    if(max<80)
	    max=255;
    for (x = 0; x < width; x++)
      {
	src[x] = ((src[x] - min) * 255) / (max - min);
      }

  /* ~1mm works best, but the window needs to have odd # of pixels */
  windowX = (6 * dev->settings.xres) / 150;
  if (!(windowX % 2))
    windowX++;

  /* second, prefill the sliding sum */
  for (j = 0; j < windowX; j++)
    sum += src[j];

  /* third, walk the input buffer, update the sliding sum, */
  /* determine threshold, output bits */
  for (j = 0; j < width; j++)
    {
      /* output image location */
      offset = j % 8;
      mask = 0x80 >> offset;
      thresh = dev->settings.threshold;

      /* move sum/update threshold only if there is a curve */
      if (dev->settings.threshold_curve)
	{
	  addCol = j + windowX / 2;
	  dropCol = addCol - windowX;

	  if (dropCol >= 0 && addCol < width)
	    {
	      sum -= src[dropCol];
	      sum += src[addCol];
	    }
	  thresh = dev->lineart_lut[sum / windowX];
	}

      /* use average to lookup threshold */
      if (src[j] > thresh)
	*dst &= ~mask;		/* white */
      else
	*dst |= mask;		/* black */

      if (offset == 7)
	dst++;
    }
}

/**
 * software lineart using data from a 8 bit gray scan. We assume true gray
 * or monochrome scan as input.
 */
static void genesys_gray_lineart(Genesys_Device* dev,
                                 uint8_t* src_data, uint8_t* dst_data,
                                 size_t pixels, size_t lines, uint8_t threshold)
{
    DBG_HELPER(dbg);
  size_t y;

  DBG(DBG_io2, "%s: converting %lu lines of %lu pixels\n", __func__, (unsigned long)lines,
      (unsigned long)pixels);
  DBG(DBG_io2, "%s: threshold=%d\n", __func__, threshold);

  for (y = 0; y < lines; y++)
    {
      binarize_line (dev, src_data + y * pixels, dst_data, pixels);
      dst_data += pixels / 8;
    }
}

/** @brief shrink or grow scanned data to fit the final scan size
 * This function shrinks the scanned data it the required resolution is lower than the hardware one,
 * or grows it in case it is the opposite like when motor resolution is higher than
 * sensor's one.
 */
static void genesys_shrink_lines_1(uint8_t* src_data, uint8_t* dst_data,
                                   unsigned int lines,
                                   unsigned int src_pixels, unsigned int dst_pixels,
                                   unsigned int channels)
{
    DBG_HELPER(dbg);
  unsigned int dst_x, src_x, y, c, cnt;
  unsigned int avg[3], val;
  uint8_t *src = (uint8_t *) src_data;
  uint8_t *dst = (uint8_t *) dst_data;

  /* choose between case where me must reduce or grow the scanned data */
  if (src_pixels > dst_pixels)
    {
      /* shrink data */
      /* TODO action must be taken at bit level, no bytes */
      src_pixels /= 8;
      dst_pixels /= 8;
      /*take first _byte_ */
      for (y = 0; y < lines; y++)
	{
	  cnt = src_pixels / 2;
	  src_x = 0;
	  for (dst_x = 0; dst_x < dst_pixels; dst_x++)
	    {
	      while (cnt < src_pixels && src_x < src_pixels)
		{
		  cnt += dst_pixels;

		  for (c = 0; c < channels; c++)
		    avg[c] = *src++;
		  src_x++;
		}
	      cnt -= src_pixels;

	      for (c = 0; c < channels; c++)
		*dst++ = avg[c];
	    }
	}
    }
  else
    {
      /* common case where y res is double x res */
      for (y = 0; y < lines; y++)
	{
	  if (2 * src_pixels == dst_pixels)
	    {
	      /* double and interleave on line */
	      for (c = 0; c < src_pixels/8; c++)
		{
		  /* first 4 bits */
		  val = 0;
		  val |= (*src & 0x80) >> 0;	/* X___ ____ --> X___ ____ */
		  val |= (*src & 0x80) >> 1;	/* X___ ____ --> _X__ ____ */
		  val |= (*src & 0x40) >> 1;	/* _X__ ____ --> __X_ ____ */
		  val |= (*src & 0x40) >> 2;	/* _X__ ____ --> ___X ____ */
		  val |= (*src & 0x20) >> 2;	/* __X_ ____ --> ____ X___ */
		  val |= (*src & 0x20) >> 3;	/* __X_ ____ --> ____ _X__ */
		  val |= (*src & 0x10) >> 3;	/* ___X ____ --> ____ __X_ */
		  val |= (*src & 0x10) >> 4;	/* ___X ____ --> ____ ___X */
		  *dst = val;
		  dst++;

		  /* last for bits */
		  val = 0;
		  val |= (*src & 0x08) << 4;	/* ____ X___ --> X___ ____ */
		  val |= (*src & 0x08) << 3;	/* ____ X___ --> _X__ ____ */
		  val |= (*src & 0x04) << 3;	/* ____ _X__ --> __X_ ____ */
		  val |= (*src & 0x04) << 2;	/* ____ _X__ --> ___X ____ */
		  val |= (*src & 0x02) << 2;	/* ____ __X_ --> ____ X___ */
		  val |= (*src & 0x02) << 1;	/* ____ __X_ --> ____ _X__ */
		  val |= (*src & 0x01) << 1;	/* ____ ___X --> ____ __X_ */
		  val |= (*src & 0x01) << 0;	/* ____ ___X --> ____ ___X */
		  *dst = val;
		  dst++;
		  src++;
		}
	    }
	  else
	    {
	      /* TODO: since depth is 1, we must interpolate bit within bytes */
	      DBG (DBG_warn, "%s: inaccurate bit expansion!\n", __func__);
	      cnt = dst_pixels / 2;
	      dst_x = 0;
	      for (src_x = 0; src_x < src_pixels; src_x++)
		{
		  for (c = 0; c < channels; c++)
		    avg[c] = *src++;
		  while (cnt < dst_pixels && dst_x < dst_pixels)
		    {
		      cnt += src_pixels;
		      for (c = 0; c < channels; c++)
			*dst++ = avg[c];
		      dst_x++;
		    }
		  cnt -= dst_pixels;
		}
	    }
	}
    }
}


/** Look in image for likely left/right/bottom paper edges, then crop image.
 */
static void genesys_crop(Genesys_Scanner* s)
{
    DBG_HELPER(dbg);
  Genesys_Device *dev = s->dev;
  int top = 0;
  int bottom = 0;
  int left = 0;
  int right = 0;

    // first find edges if any
    TIE(sanei_magic_findEdges(&s->params, dev->img_buffer.data(),
                              dev->settings.xres, dev->settings.yres,
                              &top, &bottom, &left, &right));

  DBG (DBG_io, "%s: t:%d b:%d l:%d r:%d\n", __func__, top, bottom, left,
       right);

    // now crop the image
    TIE(sanei_magic_crop (&(s->params), dev->img_buffer.data(), top, bottom, left, right));

  /* update counters to new image size */
  dev->total_bytes_to_read = s->params.bytes_per_line * s->params.lines;
}

/** Look in image for likely upper and left paper edges, then rotate
 * image so that upper left corner of paper is upper left of image.
 */
static void genesys_deskew(Genesys_Scanner *s, const Genesys_Sensor& sensor)
{
    DBG_HELPER(dbg);
  Genesys_Device *dev = s->dev;

  int x = 0, y = 0, bg;
  double slope = 0;

  bg=0;
  if(s->params.format==SANE_FRAME_GRAY && s->params.depth == 1)
    {
      bg=0xff;
    }
    TIE(sanei_magic_findSkew(&s->params, dev->img_buffer.data(),
                             sensor.optical_res, sensor.optical_res,
                             &x, &y, &slope));

  DBG(DBG_info, "%s: slope=%f => %f\n",__func__,slope, (slope/M_PI_2)*90);

    // rotate image slope is in [-PI/2,PI/2]. Positive values rotate trigonometric direction wise
    TIE(sanei_magic_rotate(&s->params, dev->img_buffer.data(),
                           x, y, slope, bg));
}

/** remove lone dots
 */
static void genesys_despeck(Genesys_Scanner* s)
{
    DBG_HELPER(dbg);
    TIE(sanei_magic_despeck(&s->params, s->dev->img_buffer.data(), s->despeck));
}

/** Look if image needs rotation and apply it
 * */
static void genesys_derotate (Genesys_Scanner * s)
{
    DBG_HELPER(dbg);
  int angle = 0;

    TIE(sanei_magic_findTurn(&s->params, s->dev->img_buffer.data(),
                             s->resolution, s->resolution, &angle));

    // apply rotation angle found
    TIE(sanei_magic_turn(&s->params, s->dev->img_buffer.data(), angle));

    // update counters to new image size
    s->dev->total_bytes_to_read = s->params.bytes_per_line * s->params.lines;
}