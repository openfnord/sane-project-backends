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

#define BUILD 0
#define MM_IN_INCH 25.4

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

#define BACKEND_NAME        brother_mfp
#define BROTHER_MFP_CONFIG_FILE "brother_mfp.conf"
#include "../include/sane/sanei_backend.h"

/*
 * Diagnostic levels.
 *
 */
#define DBG_IMPORTANT   1
#define DBG_SERIOUS     2
#define DBG_WARN        3
#define DBG_EVENT       4
#define DBG_DETAIL      5
#define DBG_DEBUG       6

#define BROTHER_USB_REQ_STARTSCAN       1
#define BROTHER_USB_REQ_STOPSCAN       2

#define BROTHER_INTERNAL_SCAN_MODE_COLOR       "CGRAY"
#define BROTHER_INTERNAL_SCAN_MODE_GRAY        "GRAY64"
#define BROTHER_INTERNAL_SCAN_MODE_GRAY_DITHER "ERRDIF"
#define BROTHER_INTERNAL_SCAN_MODE_BW          "TEXT"

#define BROTHER_READ_BUFFER_LEN (16 * 1024)

#define BROTHER_DATA_BLOCK_NO_DATA              0x80
#define BROTHER_DATA_BLOCK_END_OF_FRAME         0x82
#define BROTHER_DATA_BLOCK_JPEG                 0x64
#define BROTHER_DATA_BLOCK_GRAY_RLENGTH         0x42
#define BROTHER_DATA_BLOCK_GRAY_RAW             0x40

#define MIN(x,y)        ((x)>(y)?(y):(x))

/*-----------------------------------------------------------------*/

/*
 * Model capabilities.
 *
 */
#define CAP_MODE_COLOUR                 (1u << 0)
#define CAP_MODE_GRAY                   (1u << 1)
#define CAP_MODE_GRAY_DITHER            (1u << 2)
#define CAP_MODE_BW                     (1u << 3)

#define CAP_ENCODE_JPEG                 (1u << 4)
#define CAP_ENCODE_RLENCODE             (1u << 5)

#define SANE_VALUE_SCAN_MODE_GRAY_DITHER           SANE_I18N("Gray (Dithered)")

enum Abaton_Option
  {
    OPT_NUM_OPTS = 0,

    OPT_MODE_GROUP,
    OPT_MODE,
    OPT_X_RESOLUTION,
    OPT_Y_RESOLUTION,
//    OPT_PREVIEW,
//    OPT_ENCODING,

    OPT_GEOMETRY_GROUP,
    OPT_TL_X,                   /* top-left x */
    OPT_TL_Y,                   /* top-left y */
    OPT_BR_X,                   /* bottom-right x */
    OPT_BR_Y,                   /* bottom-right y */

    OPT_ENHANCEMENT_GROUP,
    OPT_BRIGHTNESS,
    OPT_CONTRAST,

    /* must come last: */
    NUM_OPTIONS
  };

/*
 * Supported models.
 *
 */

typedef struct Brother_Model
{
  const char *manufacturer;
  const char *model;
  SANE_Word usb_vendor;
  SANE_Word usb_product;
  SANE_Range width_range_mm;
  SANE_Range height_range_mm;
  SANE_Int x_res_list[50];
  SANE_Int y_res_list[50];
  SANE_Word capabilities;


} Brother_Model;

static Brother_Model models[] =
    {
        {"Brother", "MFC-J4320DW", 0x04f9, 0x033a,
            {0, SANE_FIX(213.9), 0},
            {0, SANE_FIX(295), 0},
            {6, 100, 150, 200, 300, 600, 1200},
            {7, 100, 150, 200, 300, 600, 1200, 2400},
             CAP_MODE_COLOUR |
             CAP_MODE_GRAY |
             CAP_MODE_GRAY_DITHER |
             CAP_MODE_BW |
             CAP_ENCODE_JPEG |
             CAP_ENCODE_RLENCODE},

         {"Brother", "MFC-465CN", 0x04f9, 0x01d7,
             {0, SANE_FIX(210), 0},
             {0, SANE_FIX(295), 0},
             {5, 100, 150, 200, 300, 600},
             {7, 100, 150, 200, 300, 600, 1200, 2400},
              CAP_MODE_COLOUR |
              CAP_MODE_GRAY |
              CAP_MODE_GRAY_DITHER |
              CAP_MODE_BW},

        {NULL, NULL, 0, 0, {0}, {0}, {0}, {0}, 0}
    };

typedef enum
{
  BROTHER_DECODE_RLEN_INIT,
  BROTHER_DECODE_RLEN_IN_EXPAND,
  BROTHER_DECODE_RLEN_IN_BYTES
} Brother_decode_state;

typedef struct
{
  Brother_decode_state decode_state;
  SANE_Byte decode_expand_char;
  size_t block_bytes_left;
} Brother_rlen_decode_state;

typedef struct
{
  size_t dummy;
} Brother_jpeg_decode_state;

typedef struct
{
  size_t dummy;
} Brother_raw_decode_state;



/* data structures and constants */
typedef struct Brother_Device
{
  struct Brother_Device *next;

  const Brother_Model *model;
  SANE_Device sane_device;
  char *name;
  SANE_Bool is_open;
  SANE_Bool is_scanning;
  SANE_Bool in_session;
  SANE_Bool is_cancelled;

  /* options */
  SANE_Option_Descriptor opt[NUM_OPTIONS];
  Option_Value val[NUM_OPTIONS];

  SANE_String_Const modes[10];
  SANE_Parameters params;
  const char *internal_scan_mode;
  SANE_Int x_res;
  SANE_Int y_res;

  SANE_Int pixel_x_offset;
  SANE_Int pixel_x_width;

  SANE_Int pixel_y_offset;
  SANE_Int pixel_y_height;

  int fd;       // sanei_usb handle.

  SANE_Byte *read_buffer;
  size_t read_buffer_bytes;     // number of image bytes in the buffer.
  size_t block_bytes_remaining;
  FILE *scan_file;

  // TODO: Temp state for conversions.
  unsigned char current_data_block;
  Brother_rlen_decode_state rlen_decode_state;
  Brother_raw_decode_state raw_decode_state;
  Brother_jpeg_decode_state jpeg_decode_state;

} Brother_Device;

static Brother_Device *first_dev = NULL;
static int num_devices = 0;
static const SANE_Device **devlist = NULL;

static const SANE_Range constraint_brightness_contrast = { -50, +50, 1 };




static SANE_Status
attach_with_ret (const char *devicename, Brother_Device **dev)
{
  Brother_Device *device;
  SANE_Status status;

  DBG (DBG_EVENT, "attach_one: %s\n", devicename);

  SANE_Word vendor;
  SANE_Word product;
  status = sanei_usb_get_vendor_product_byname (devicename, &vendor, &product);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "ERROR: attach_one: failed to get USB vendor/product for device %s\n", devicename);
      return status;
    }

  /*
   * Search for model for this device.
   * TODO: if only the sanei_* functions permitted sending a context ptr with the callback,
   * then we wouldn't have to do this as we already know what the model is. d'uh.
   *
   */
  const Brother_Model *model = NULL;
  for (model = models; model->model; model++)
    {
      if ((model->usb_vendor == vendor) && (model->usb_product == product))
        {
          break;
        }
    }

  if (NULL == model)
    {
      DBG (DBG_SERIOUS, "ERROR: attach_one: failed to determine model from usb params: %s\n", devicename);
      return status;
    }

  /*
   * See if we already know about the device.
   *
   */
  for (device = first_dev; device; device = device->next)
    {
      if (strcmp (device->sane_device.name, devicename) == 0)
        {
          if (dev)
            {
              *dev = device;
            }
          return SANE_STATUS_GOOD;
        }
    }

  /*
   * Create a new entry for this device.
   *
   */
  device = calloc (1, sizeof(*device));
  if (!device)
    {
      DBG (DBG_SERIOUS, "attach_with_ret: failed to allocate device entry %zu\n", sizeof(*device));
      return SANE_STATUS_NO_MEM;
    }

  DBG (DBG_EVENT, "attach_scanner: opening %s\n", devicename);

  device->name = strdup (devicename);
  device->sane_device.name = device->name;
  device->sane_device.vendor = "BROTHER";
  device->sane_device.model = model->model;
  device->sane_device.type = "multi-function peripheral";
  device->model = model;

  /*
   * Create the modes list.
   *
   */
  size_t num_modes = 0;
  if (model->capabilities & CAP_MODE_COLOUR)
    {
      device->modes[num_modes++] = SANE_VALUE_SCAN_MODE_COLOR;
    }
  if (model->capabilities & CAP_MODE_GRAY)
    {
      device->modes[num_modes++] = SANE_VALUE_SCAN_MODE_GRAY;
    }
  if (model->capabilities & CAP_MODE_GRAY_DITHER)
    {
      device->modes[num_modes++] = SANE_VALUE_SCAN_MODE_GRAY_DITHER;
    }
  if (model->capabilities & CAP_MODE_BW)
    {
      device->modes[num_modes++] = SANE_VALUE_SCAN_MODE_LINEART;
    }

  ++num_devices;
  device->next = first_dev;
  first_dev = device;

  if (dev)
    {
      *dev = device;
    }

  return SANE_STATUS_GOOD;
}



static SANE_Status
attach_with_no_ret (const char *devicename)
{
  return attach_with_ret(devicename, NULL);
}

static size_t
max_string_size (const SANE_String_Const strings[])
{
  size_t size, max_size = 0;
  SANE_Int i;

  for (i = 0; strings[i]; ++i)
    {
      size = strlen (strings[i]) + 1;
      if (size > max_size)
        max_size = size;
    }
  return max_size;
}


static SANE_Status
init_options (Brother_Device *device)
{
  SANE_Option_Descriptor *od;

  DBG (2, "begin init_options: device=%s\n", device->name);

  /* opt_num_opts */
  od = &device->opt[OPT_NUM_OPTS];
  od->name = "";
  od->title = SANE_TITLE_NUM_OPTIONS;
  od->desc = SANE_DESC_NUM_OPTIONS;
  od->type = SANE_TYPE_INT;
  od->unit = SANE_UNIT_NONE;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT;
  od->constraint_type = SANE_CONSTRAINT_NONE;
  od->constraint.range = 0;
  device->val[OPT_NUM_OPTS].w = NUM_OPTIONS;

  /* opt_mode_group */
  od = &device->opt[OPT_MODE_GROUP];
  od->name = "";
  od->title = SANE_I18N ("Scan Mode");
  od->desc = "";
  od->type = SANE_TYPE_GROUP;
  od->unit = SANE_UNIT_NONE;
  od->size = 0;
  od->cap = 0;
  od->constraint_type = SANE_CONSTRAINT_NONE;
  od->constraint.range = 0;
  device->val[OPT_MODE_GROUP].w = 0;

  /* opt_mode */
  od = &device->opt[OPT_MODE];
  od->name = SANE_NAME_SCAN_MODE;
  od->title = SANE_TITLE_SCAN_MODE;
  od->desc = SANE_DESC_SCAN_MODE;
  od->type = SANE_TYPE_STRING;
  od->unit = SANE_UNIT_NONE;
  od->size = max_string_size (device->modes);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  od->constraint.string_list = device->modes;
  device->val[OPT_MODE].s = malloc(od->size);
  if (NULL == device->val[OPT_MODE].s)
    {
      DBG (DBG_SERIOUS, "init_options: failed to allocate mode storage %zu\n", (size_t)od->size);
      return SANE_STATUS_NO_MEM;
    }
  (void)strcpy(device->val[OPT_MODE].s, device->modes[0]);

  /* opt_resolution */
  od = &device->opt[OPT_X_RESOLUTION];
  od->name = SANE_NAME_SCAN_X_RESOLUTION;
  od->title = SANE_TITLE_SCAN_X_RESOLUTION;
  od->desc = SANE_DESC_SCAN_X_RESOLUTION;
  od->type = SANE_TYPE_INT;
  od->unit = SANE_UNIT_DPI;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->constraint_type = SANE_CONSTRAINT_WORD_LIST;
  od->constraint.word_list = device->model->x_res_list;
  device->val[OPT_X_RESOLUTION].w = device->model->x_res_list[1];

  od = &device->opt[OPT_Y_RESOLUTION];
  od->name = SANE_NAME_SCAN_Y_RESOLUTION;
  od->title = SANE_TITLE_SCAN_Y_RESOLUTION;
  od->desc = SANE_DESC_SCAN_Y_RESOLUTION;
  od->type = SANE_TYPE_INT;
  od->unit = SANE_UNIT_DPI;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->constraint_type = SANE_CONSTRAINT_WORD_LIST;
  od->constraint.word_list = device->model->y_res_list;
  device->val[OPT_Y_RESOLUTION].w = device->model->y_res_list[1];

  /* opt_geometry_group */
  od = &device->opt[OPT_GEOMETRY_GROUP];
  od->name = "";
  od->title = SANE_I18N ("Geometry");
  od->desc = "";
  od->type = SANE_TYPE_GROUP;
  od->unit = SANE_UNIT_NONE;
  od->size = 0;
  od->cap = 0;
  od->constraint_type = SANE_CONSTRAINT_NONE;
  od->constraint.range = 0;
  device->val[OPT_GEOMETRY_GROUP].w = 0;

  /* opt_tl_x */
  od = &device->opt[OPT_TL_X];
  od->name = SANE_NAME_SCAN_TL_X;
  od->title = SANE_TITLE_SCAN_TL_X;
  od->desc = SANE_DESC_SCAN_TL_X;
  od->type = SANE_TYPE_FIXED;
  od->unit = SANE_UNIT_MM;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &device->model->width_range_mm;
  device->val[OPT_TL_X].w = 0;

  /* opt_tl_y */
  od = &device->opt[OPT_TL_Y];
  od->name = SANE_NAME_SCAN_TL_Y;
  od->title = SANE_TITLE_SCAN_TL_Y;
  od->desc = SANE_DESC_SCAN_TL_Y;
  od->type = SANE_TYPE_FIXED;
  od->unit = SANE_UNIT_MM;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &device->model->height_range_mm;
  device->val[OPT_TL_Y].w = 0;

  /* opt_br_x */
  od = &device->opt[OPT_BR_X];
  od->name = SANE_NAME_SCAN_BR_X;
  od->title = SANE_TITLE_SCAN_BR_X;
  od->desc = SANE_DESC_SCAN_BR_X;
  od->type = SANE_TYPE_FIXED;
  od->unit = SANE_UNIT_MM;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &device->model->width_range_mm;
  device->val[OPT_BR_X].w = device->model->width_range_mm.max;

  /* opt_br_y */
  od = &device->opt[OPT_BR_Y];
  od->name = SANE_NAME_SCAN_BR_Y;
  od->title = SANE_TITLE_SCAN_BR_Y;
  od->desc = SANE_DESC_SCAN_BR_Y;
  od->type = SANE_TYPE_FIXED;
  od->unit = SANE_UNIT_MM;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &device->model->height_range_mm;
  device->val[OPT_BR_Y].w = device->model->height_range_mm.max;

  /* opt_geometry_group */
  od = &device->opt[OPT_ENHANCEMENT_GROUP];
  od->name = "";
  od->title = SANE_I18N ("Enhancement");
  od->desc = "";
  od->type = SANE_TYPE_GROUP;
  od->unit = SANE_UNIT_NONE;
  od->size = 0;
  od->cap = 0;
  od->constraint_type = SANE_CONSTRAINT_NONE;
  od->constraint.range = 0;
  device->val[OPT_ENHANCEMENT_GROUP].w = 0;

  od = &device->opt[OPT_BRIGHTNESS];
  od->name = SANE_NAME_BRIGHTNESS;
  od->title = SANE_TITLE_BRIGHTNESS;
  od->type = SANE_TYPE_INT;
  od->desc = SANE_DESC_BRIGHTNESS;
  od->unit = SANE_UNIT_PERCENT;
  od->size = 1 * sizeof(SANE_Word);
  od->cap  = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT | SANE_CAP_INACTIVE;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &constraint_brightness_contrast;
  device->val[OPT_BRIGHTNESS].w = 0;

  od = &device->opt[OPT_CONTRAST];
  od->name = SANE_NAME_CONTRAST;
  od->title = SANE_TITLE_CONTRAST;
  od->type = SANE_TYPE_INT;
  od->desc = SANE_DESC_CONTRAST;
  od->unit = SANE_UNIT_PERCENT;
  od->size = 1 * sizeof(SANE_Word);
  od->cap  = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT | SANE_CAP_INACTIVE;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &constraint_brightness_contrast;
  device->val[OPT_CONTRAST].w = 0;

  DBG (2, "end init_options: dev=%s\n", device->name);

  return SANE_STATUS_GOOD;
}



static SANE_Status Brother_stop_session (Brother_Device *device);
static SANE_Status Brother_start_session (Brother_Device *device);
static SANE_Status Brother_close_device (Brother_Device *device);
static SANE_Status Brother_poll_for_read_flush(Brother_Device *device, useconds_t max_time);
static SANE_Status Brother_poll_for_read(Brother_Device *device, SANE_Byte *buffer, size_t *buf_len, useconds_t *max_time);
static SANE_Status Brother_init_session(Brother_Device *device);
static SANE_Status Brother_open_device (Brother_Device *device);
static SANE_Status Brother_stop_scan (Brother_Device *device);
static SANE_Status Brother_start_scan (Brother_Device *device);
static SANE_Status Brother_read (Brother_Device *device, SANE_Byte *data, SANE_Int max_length, SANE_Int *length);

static void Brother_decode_gray_rlength_init (Brother_rlen_decode_state *state);
static void Brother_decode_jpeg_init (Brother_jpeg_decode_state *state);
static void Brother_decode_gray_raw_init (Brother_raw_decode_state *state);

static SANE_Status Brother_decode_jpeg (Brother_jpeg_decode_state *state,
                                        const SANE_Byte *in_buffer, size_t in_buffer_len,
                                        SANE_Byte *out_buffer, size_t out_buffer_len,
                                        size_t *bytes_consumed, size_t *bytes_written);

static SANE_Status Brother_decode_gray_raw (Brother_raw_decode_state *state,
                                        const SANE_Byte *in_buffer, size_t in_buffer_len,
                                        SANE_Byte *out_buffer, size_t out_buffer_len,
                                        size_t *bytes_consumed, size_t *bytes_written);

static SANE_Status Brother_decode_gray_rlength (Brother_rlen_decode_state *state,
                                                const SANE_Byte *in_buffer, size_t in_buffer_len,
                                                SANE_Byte *out_buffer, size_t out_buffer_len,
                                                size_t *bytes_consumed, size_t *bytes_written);




static void
Brother_decode_gray_rlength_init (Brother_rlen_decode_state *state)
{
  state->decode_state = BROTHER_DECODE_RLEN_INIT;
  state->block_bytes_left = 0;
}

static void
Brother_decode_jpeg_init (Brother_jpeg_decode_state *state)
{
//  state->decode_bytes_left = 0;
}

static void
Brother_decode_gray_raw_init (Brother_raw_decode_state *state)
{
//  state->decode_bytes_left = block_size;
}


static SANE_Status
Brother_decode_jpeg (Brother_jpeg_decode_state *state,
                     const SANE_Byte *in_buffer, size_t in_buffer_len,
                     SANE_Byte *out_buffer, size_t out_buffer_len,
                     size_t *bytes_consumed, size_t *bytes_written)
{

  /*
   * TODO: replace with JPEG decoding.
   *
   */
  size_t bytes_to_copy = MIN(in_buffer_len, (size_t)out_buffer_len);
  (void) memcpy (out_buffer, in_buffer, bytes_to_copy);

  *bytes_written = bytes_to_copy;
  *bytes_consumed = bytes_to_copy;

  return SANE_STATUS_GOOD;
}

static SANE_Status
Brother_decode_gray_raw (Brother_raw_decode_state *state,
                         const SANE_Byte *in_buffer, size_t in_buffer_len,
                         SANE_Byte *out_buffer, size_t out_buffer_len,
                         size_t *bytes_consumed, size_t *bytes_written)
{
  size_t bytes_to_copy = MIN(in_buffer_len, (size_t )out_buffer_len);
  (void) memcpy (out_buffer, in_buffer, bytes_to_copy);

  *bytes_written = bytes_to_copy;
  *bytes_consumed = bytes_to_copy;

  return SANE_STATUS_GOOD;
}


static SANE_Status
Brother_decode_gray_rlength (Brother_rlen_decode_state *state,
                             const SANE_Byte *in_buffer, size_t in_buffer_len,
                             SANE_Byte *out_buffer, size_t out_buffer_len,
                             size_t *bytes_consumed, size_t *bytes_written)
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
      if (state->decode_state == BROTHER_DECODE_RLEN_INIT)
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
                DBG (DBG_IMPORTANT,
                     "Brother_decode_gray_rlength: in_buffer_len < 2\n");
                break;
              }

            state->decode_state = BROTHER_DECODE_RLEN_IN_EXPAND;
            state->block_bytes_left = 0xff - in_buffer[0] + 2;
            state->decode_expand_char = in_buffer[1];

            in_buffer += 2;
            in_buffer_len -= 2;

            DBG (DBG_IMPORTANT,
                 "Brother_decode_gray_rlength: expand block: %zu bytes of 0x%02.2x\n",
                 state->block_bytes_left, (int)state->decode_expand_char);

          }
        else
          {
            state->decode_state = BROTHER_DECODE_RLEN_IN_BYTES;
            state->block_bytes_left = in_buffer[0] + 1;

            in_buffer += 1;
            in_buffer_len -= 1;

            DBG (DBG_IMPORTANT,
                 "Brother_decode_gray_rlength: bytes block: %zu bytes\n",
                 state->block_bytes_left);
          }
      }

      /*
       * Extraction phase, where we try to decode data.
       * We should be in a data extraction mode now.
       *
       */
      size_t bytes_to_copy = MIN(out_buffer_len, state->block_bytes_left);
      size_t consumed = 0;

      if (bytes_to_copy)
        {
          if (state->decode_state == BROTHER_DECODE_RLEN_IN_BYTES)
            {
              bytes_to_copy = MIN(bytes_to_copy, in_buffer_len);
              (void)memcpy(out_buffer, in_buffer, bytes_to_copy);
              consumed = bytes_to_copy;
            }
          else if (state->decode_state == BROTHER_DECODE_RLEN_IN_EXPAND)
            {
              (void)memset(out_buffer, state->decode_expand_char, bytes_to_copy);
            }

          in_buffer += consumed;
          in_buffer_len -= consumed;
          out_buffer += bytes_to_copy;
          out_buffer_len -= bytes_to_copy;
          state->block_bytes_left -= bytes_to_copy;
          if (state->block_bytes_left == 0)
            {
              state->decode_state = BROTHER_DECODE_RLEN_INIT;
            }
        }
    }

  if (!in_buffer_len || !out_buffer_len)
    {
      DBG (DBG_IMPORTANT,
           "Brother_decode_gray_rlength: ran out of buffer: in %zu out %zu\n", in_buffer_len, out_buffer_len);

    }
  *bytes_consumed = in_buffer_len_start - in_buffer_len;
  *bytes_written =  out_buffer_len_start - out_buffer_len;

  return SANE_STATUS_GOOD;
}

static SANE_Status Brother_read (Brother_Device *device, SANE_Byte *data, SANE_Int max_length, SANE_Int *length)
{
  SANE_Status res;

  *length = 0;

  if (device->is_cancelled)
    {
      DBG (DBG_EVENT, "Brother_read: read aborted due to user cancel.\n");

      (void)Brother_stop_scan(device);
      return SANE_STATUS_CANCELLED;

    }

  /*
   * TODO: For the moment, we will just a do a synchronous read and return
   * back what we can. Eventually, we might kick off a reader thread to allow us
   * to do asynchronous reading.
   *
   */

  /*
   * First, see if we need to do a read from the device to satisfy the read.
   *
   * Note: We check also device->block_bytes_remaining because although we might have *some*
   * data in the read buffer, it might not be enough to proceed, e.g. we might be
   * waiting for the rest of a block header to be revealed.
   *
   * I will arbitrarily pick 1024 as our limit. If we have anything near to that, we *really*
   * should be able to decode *something*.
   *
   */
  if ((device->block_bytes_remaining == 0) || (device->read_buffer_bytes < 1024))
    {

      /*
       * Try to do a read from the device.
       * Timeout of zero ensures that we only try once.
       *
       * Note that we trim the buffer sothat it is a multiple of 1024. This seems to be
       * a requirement of libusb to avoid some potential buffer overflow conditions.
       * It's OK: as long as we are reading something substantial, that is fine.
       *
       */
      size_t bytes_to_read = (BROTHER_READ_BUFFER_LEN - device->read_buffer_bytes) & ~(1024 - 1);

      DBG (DBG_IMPORTANT, "Brother_read: attempting read, space for %zu bytes\n", bytes_to_read);
      useconds_t max_time = 0;
      res = Brother_poll_for_read (device,
                                   device->read_buffer + device->read_buffer_bytes,
                                   &bytes_to_read, &max_time);
      if (res != SANE_STATUS_GOOD)
        {
          DBG (DBG_EVENT, "Brother_read: read aborted due to read error: %d.\n", res);
          (void)Brother_stop_scan(device);
          return res;
        }

      device->read_buffer_bytes += bytes_to_read;
      DBG (DBG_IMPORTANT,
           "Brother_read: read %zu bytes, now buffer has %zu bytes\n",
           bytes_to_read, device->read_buffer_bytes);

      /*
       * Nothing to read: we will just try again next time.
       *
       * TODO: We need an overall timeout. We cannot wait indefinitely for
       * data that isn't coming.
       *
       */
      if (bytes_to_read == 0)
        {
          *length = 0;
          return SANE_STATUS_GOOD;
        }
    }

    /*
     * If block_bytes_remaining is 0, then we should be seeing a
     * block header. If we have enough bytes, then decode the header.
     *
     * If we are at this point, we must have at least one byte in the read buffer because
     * of the above check.
     *
     */
    do
      {
        while ((device->block_bytes_remaining == 0) && (device->read_buffer_bytes))
        {
          DBG (DBG_EVENT, "Brother_read: looking for block header. first byte=%u\n",
               (unsigned int) device->read_buffer[0]);

            switch (device->read_buffer[0])
            {
              case BROTHER_DATA_BLOCK_JPEG:      // JPEG data.
                DBG (DBG_IMPORTANT, "Brother_read: seen JPEG block\n");

                if (device->read_buffer_bytes < 12)
                  {
                    DBG (DBG_EVENT, "Brother_read: but block too short...\n");
                    *length = 0;
                    return SANE_STATUS_GOOD; // not read enough of the header yet.
                  }

                /*
                 * Compute the block size from little-endian 2 byte value in bytes 11 and 12.
                 * Includes the 12 bytes of the header itself so take that off.
                 *
                 */
                device->block_bytes_remaining = (device->read_buffer[10]
                    | (device->read_buffer[11] << 8));

                (void)memmove(device->read_buffer, device->read_buffer + 12, device->read_buffer_bytes - 12);
                device->read_buffer_bytes -= 12;
                device->current_data_block = BROTHER_DATA_BLOCK_JPEG;

                Brother_decode_jpeg_init(&device->jpeg_decode_state);
                break;

              case BROTHER_DATA_BLOCK_GRAY_RLENGTH:      // Gray data.
                DBG (DBG_IMPORTANT, "Brother_read: seen GREY RLENGTH block\n");
                if (device->read_buffer_bytes < 12)
                  {
                    DBG (DBG_EVENT, "Brother_read: but block too short...\n");
                    *length = 0;
                    return SANE_STATUS_GOOD; // not read enough of the header yet.
                  }

                device->block_bytes_remaining = (device->read_buffer[10]
                    | (device->read_buffer[11] << 8));

                (void)memmove(device->read_buffer, device->read_buffer + 12, device->read_buffer_bytes - 12);
                device->read_buffer_bytes -= 12;
                device->current_data_block = BROTHER_DATA_BLOCK_GRAY_RLENGTH;

                Brother_decode_gray_rlength_init(&device->rlen_decode_state);
                break;

              case BROTHER_DATA_BLOCK_GRAY_RAW:
                DBG (DBG_IMPORTANT, "Brother_read: seen RAW block\n");
                if (device->read_buffer_bytes < 12)
                  {
                    DBG (DBG_EVENT, "Brother_read: but block too short...\n");
                    *length = 0;
                    return SANE_STATUS_GOOD; // not read enough of the header yet.
                  }

                device->block_bytes_remaining = (device->read_buffer[10]
                    | (device->read_buffer[11] << 8));

                (void)memmove(device->read_buffer, device->read_buffer + 12, device->read_buffer_bytes - 12);
                device->read_buffer_bytes -= 12;
                device->current_data_block = BROTHER_DATA_BLOCK_GRAY_RAW;

                Brother_decode_gray_raw_init(&device->raw_decode_state);
                break;

              case BROTHER_DATA_BLOCK_NO_DATA:      // No more data.
                DBG (DBG_IMPORTANT, "Brother_read: seen end block\n");
                device->read_buffer_bytes = 0;
                *length = 0;
                res = Brother_stop_scan(device);
                if (res != SANE_STATUS_GOOD)
                  {
                    DBG (DBG_SERIOUS, "Brother_read: failed to stop scan: %d.\n", res);
                    return res;
                  }
                device->current_data_block = BROTHER_DATA_BLOCK_NO_DATA;
                return SANE_STATUS_EOF;

              case BROTHER_DATA_BLOCK_END_OF_FRAME:      // Final page block.
                DBG (DBG_IMPORTANT, "Brother_read: seen END OF FRAME block\n");
                if (device->read_buffer_bytes < 10)
                  {
                    DBG (DBG_EVENT, "Brother_read: but block too short...\n");
                    *length = 0;
                    return SANE_STATUS_GOOD; // not read enough of the header yet.
                  }

                (void)memmove(device->read_buffer, device->read_buffer + 10, device->read_buffer_bytes - 10);
                device->block_bytes_remaining = 0;
                device->read_buffer_bytes -= 10;
                DBG (DBG_IMPORTANT, "Brother_read: remaining is %zu bytes\n", device->read_buffer_bytes);
                device->current_data_block = BROTHER_DATA_BLOCK_END_OF_FRAME;
                break;

              default:
                DBG (DBG_SERIOUS, "Brother_read: unknown data block type: %u\n",
                     (unsigned) device->read_buffer[0]);
                (void)Brother_stop_scan(device);
                return SANE_STATUS_IO_ERROR;
            }
          }

      /*
       * If we have enough scan data in the read buffer to satisfy the request then we will just
       * send that back. We can tell if we are in a block because block_bytes_remaining will be
       * non-zero. Note that doesn't indicate how many bytes there are available though, just that
       * we are not waiting for a block header. For that, we will inspect read_buffer_bytes which
       * will indicate the available image data.
       *
       */
      if (device->block_bytes_remaining && device->read_buffer_bytes)
        {
          DBG (DBG_IMPORTANT,
               "Brother_read: read_section: block remaining: %zu, "
              "read buf bytes avail: %zu bytes\n",
               device->block_bytes_remaining, device->read_buffer_bytes);

          size_t bytes_consumed = 0;
          size_t bytes_written = 0;

          if (device->current_data_block == BROTHER_DATA_BLOCK_JPEG)
            {
              DBG (DBG_EVENT, "Brother_read: decoding JPEG data\n");

              res = Brother_decode_jpeg (
                  &device->jpeg_decode_state, device->read_buffer,
                  MIN(device->read_buffer_bytes, device->block_bytes_remaining),
                  data, max_length, &bytes_consumed, &bytes_written);
            }
          else if (device->current_data_block == BROTHER_DATA_BLOCK_GRAY_RLENGTH)
            {
              DBG (DBG_EVENT, "Brother_read: decoding GRAY RLENGTH data\n");

              res = Brother_decode_gray_rlength (
                  &device->rlen_decode_state, device->read_buffer,
                  MIN(device->read_buffer_bytes, device->block_bytes_remaining),
                  data, max_length, &bytes_consumed, &bytes_written);
            }
          else if (device->current_data_block == BROTHER_DATA_BLOCK_GRAY_RAW)
            {
              DBG (DBG_EVENT, "Brother_read: decoding RAW data\n");

              res = Brother_decode_gray_raw (
                  &device->raw_decode_state, device->read_buffer,
                  MIN(device->read_buffer_bytes, device->block_bytes_remaining),
                  data, max_length, &bytes_consumed, &bytes_written);
            }
          else
            {
              DBG (DBG_SERIOUS, "Brother_read: cannot decode data block: logic error\n");
              (void)Brother_stop_scan(device);
              return SANE_STATUS_IO_ERROR;
            }

          if (res != SANE_STATUS_GOOD)
            {
              DBG (DBG_SERIOUS, "Brother_read: failed to decode block data\n");
              (void)Brother_stop_scan(device);
              return SANE_STATUS_IO_ERROR;
            }

          /*
           * TODO: remove me! For test only.
           *
           */
          fwrite(data, bytes_written, 1, device->scan_file);

          (void) memmove (device->read_buffer, device->read_buffer + bytes_consumed,
                          device->read_buffer_bytes - bytes_consumed);
          device->read_buffer_bytes -= bytes_consumed;
          device->block_bytes_remaining -= bytes_consumed;
          max_length -= bytes_written;

          *length += bytes_written;

          data += bytes_written;

          DBG (DBG_IMPORTANT,
              "Brother_read: returned %zu bytes, consumed %zu bytes, "
              "block bytes left %zu, read_buffer left %zu output_remain: %zu\n",
              bytes_written, bytes_consumed, device->block_bytes_remaining,
              device->read_buffer_bytes,
              (size_t)max_length);
        }
      } while (max_length && !device->block_bytes_remaining && device->read_buffer_bytes);

  return SANE_STATUS_GOOD;
}


static SANE_Status
Brother_stop_session (Brother_Device *device)
{
  /*
   * Initialisation.
   *
   */
  if (!device->is_open)
    {
      DBG (DBG_WARN, "Brother_stop_session: device not open: %s.\n", device->name);
      return SANE_STATUS_INVAL;
    }

  if (!device->in_session)
    {
      return SANE_STATUS_GOOD;
    }

  SANE_Byte start_response[5];
  SANE_Status res;

  res = sanei_usb_control_msg (device->fd, USB_DIR_IN | USB_TYPE_VENDOR,
                               BROTHER_USB_REQ_STOPSCAN,
                               2, 0, 5, start_response);

  if (res != SANE_STATUS_GOOD)
    {

      DBG (DBG_WARN, "Brother_stop_session: failed to send stop sequence.\n");
      return res;
    }

  if (start_response[4] != 0x00)
    {
      DBG (DBG_WARN, "Brother_stop_session: stop sequence return value incorrect: 0x%02.2x.\n", start_response[4]);
      return SANE_STATUS_INVAL;
    }

  device->in_session = SANE_FALSE;

  return SANE_STATUS_GOOD;
}

static SANE_Status
Brother_start_session (Brother_Device *device)
{
  /*
   * Initialisation.
   *
   */
  if (!device->is_open)
    {
      DBG (DBG_WARN, "Brother_stop_session: device is not open: %s", device->name);
      return SANE_STATUS_INVAL;
    }

  if (device->in_session)
    {
      return SANE_STATUS_GOOD;
    }

  SANE_Byte start_response[5];
  SANE_Status res;

  res = sanei_usb_control_msg (device->fd, USB_DIR_IN | USB_TYPE_VENDOR,
                               BROTHER_USB_REQ_STARTSCAN,
                               2, 0, 5, start_response);

  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_WARN, "Brother_stop_session: failed to send stop session sequence: %s.\n", device->name);
      return res;
    }

  if (start_response[4] != 0x00)
    {
      DBG (DBG_WARN, "Brother_stop_session: response code for stop session invalid: 0x%02.2x", start_response[4]);
      return SANE_STATUS_INVAL;
    }

  device->in_session = SANE_TRUE;

  return SANE_STATUS_GOOD;
}

static SANE_Status
Brother_close_device (Brother_Device *device)
{
  DBG (DBG_EVENT, "Brother_close_device: `%s'\n", device->name);

  if (!device->is_open)
    {
      // Already open!
      DBG (DBG_WARN, "Brother_close_device: not open `%s'\n", device->name);
      return SANE_STATUS_DEVICE_BUSY;
    }

  /*
   * If we are in a session, try to close it off.
   *
   */
  if (device->in_session)
    {
      (void)Brother_stop_session(device);
    }

  device->is_open = SANE_FALSE;

  sanei_usb_close(device->fd);
  device->fd = 0;

  return SANE_STATUS_GOOD;
}

/*
 * Just suck any cached data from the read stream and discard it
 * for the specified amount of time, unless there is an error.
 *
 */
static SANE_Status
Brother_poll_for_read_flush(Brother_Device *device, useconds_t max_time)
{
  SANE_Status res;

  SANE_Byte buffer[1024];
  size_t buf_len = sizeof(buffer);

  do
    {
      res = Brother_poll_for_read(device, buffer, &buf_len, &max_time);
      if (res != SANE_STATUS_GOOD)
        {
          DBG (DBG_WARN, "Brother_stop_session: failed to flush poll: %d\n", res);
          return res;
        }

    } while (max_time);

  return SANE_STATUS_GOOD;
}

/*
 * If you only want to read once, then set *max_time to 0.
 *
 */
static SANE_Status
Brother_poll_for_read(Brother_Device *device, SANE_Byte *buffer, size_t *buf_len, useconds_t *max_time)
{
  SANE_Status res;

  DBG (DBG_EVENT, "Brother_poll_for_read: `%s'\n", device->name);

  useconds_t timeout = 0;
  size_t read_amt;
  do
    {
      read_amt = *buf_len;

      res = sanei_usb_read_bulk (device->fd, buffer, &read_amt);
      if (res == SANE_STATUS_GOOD)
        {
          break;
        }

      if (res != SANE_STATUS_EOF)
        {
          DBG (DBG_WARN, "Brother_stop_session: failed to poll for read: %d\n", res);
          *max_time -= timeout;
          return res;
        }

      usleep(50 * 1000);
      timeout += (50 * 1000);
    } while (timeout < *max_time);

  /*
   * We timed out. No data and no time left.
   *
   */
  if (*max_time && (timeout >= *max_time))
    {
      *buf_len = 0;
      *max_time = 0;
      return SANE_STATUS_GOOD;
    }

  /*
   * We read some data!
   *
   */
  *max_time -= timeout;
  *buf_len = read_amt;

  return SANE_STATUS_GOOD;
}

static SANE_Status
Brother_init_session(Brother_Device *device)
{
  SANE_Status res;

  DBG (DBG_EVENT, "Brother_init_session: `%s'\n", device->name);

  /*
   * First wait for a second, flushing stuff out.
   *
   */
  res = Brother_poll_for_read_flush(device, 1000000);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_WARN, "Brother_init_session: initial flush failure: %d\n", res);
      return res;
    }

  /*
   * First prep parameter block.
   *
   * TODO: make this buffer dynamic in size and shared across all requests.
   *
   */
  SANE_Byte bulk_buffer[1024];

  (void)memcpy(bulk_buffer, "\x1b" "\x51" "\x0a" "\x80", 4);
  size_t buf_size = 4;

  res = sanei_usb_write_bulk (device->fd, bulk_buffer, &buf_size);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_init_session: failed to send first prep block: %d\n", res);
      return res;
    }

  if (buf_size != 4)
    {
      DBG (DBG_SERIOUS, "Brother_init_session: failed to write init block\n");
      return SANE_STATUS_IO_ERROR;
    }

  /*
   * Try to read the response.
   *
   */
  buf_size = sizeof(bulk_buffer);
  useconds_t timeout = (8 * 1000000);   // 1 second.
  res = Brother_poll_for_read(device, bulk_buffer, &buf_size, &timeout);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_init_session: failed to receive first prep block response: %d\n", res);
      return res;
    }

  return SANE_STATUS_GOOD;
}

static SANE_Status
Brother_open_device (Brother_Device *device)
{
  SANE_Status res;

  DBG (DBG_EVENT, "Brother_open_device: `%s'\n", device->name);

  if (device->is_open)
    {
      // Already open!
      DBG (DBG_WARN, "Brother_open_device: already open `%s'\n", device->name);
      return SANE_STATUS_DEVICE_BUSY;
    }

  /*
   * We set this here so that Brother_close_device() can be used to tidy up the device in
   * the event of a failure during the open.
   *
   */
  device->is_open = SANE_TRUE;

  res = sanei_usb_open (device->name, &device->fd);

  if (res != SANE_STATUS_GOOD)
    {
      Brother_close_device(device);
      DBG (1, "Brother_open_device: couldn't open device `%s': %s\n", device->name, sane_strstatus (res));
      return res;
    }

  /*
   * Initialisation.
   *
   */
  res = Brother_start_session(device);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_open_device: failed to start session: %d\n", res);
      Brother_close_device(device);
      return res;
    }

  /*
   * Execute the intialisation sequence.
   *
   */
  SANE_Status init_res = Brother_init_session(device);

  res = Brother_stop_session(device);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_open_device: failed to stop session: %d\n", res);
      Brother_close_device(device);
      return res;
    }

  /*
   * If the init sequence failed, then that's not good either.
   *
   */
  if (init_res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_open_device: failed to send init session: %d\n", init_res);
      Brother_close_device(device);
    }

  return init_res;
}

static SANE_Status
Brother_stop_scan (Brother_Device *device)
{
  SANE_Status res;

  DBG (DBG_EVENT, "Brother_stop_scan: `%s'\n", device->name);

  if (!device->is_scanning)
    {
      DBG (DBG_WARN, "Brother_stop_scan: not scanning `%s'\n", device->name);
      return SANE_STATUS_GOOD;
    }


  device->is_scanning = SANE_FALSE;
  device->is_cancelled = SANE_FALSE;

  SANE_Byte stop_response[5];

  res = sanei_usb_control_msg (device->fd, USB_DIR_IN | USB_TYPE_VENDOR,
                               BROTHER_USB_REQ_STARTSCAN,
                               2, 0, 5, stop_response);

  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_stop_scan: failed to send stop session control: %d\n", res);
      return res;
    }

  /*
   * Free read buffer buffer.
   *
   */
  free (device->read_buffer);
  device->read_buffer = NULL;
  device->read_buffer_bytes = 0;

  return SANE_STATUS_GOOD;

}

static SANE_Status
Brother_start_scan (Brother_Device *device)
{
  SANE_Status res;

  DBG (DBG_EVENT, "Brother_start_scan: `%s'\n", device->name);

  if (device->is_scanning)
    {
      DBG (DBG_WARN, "Brother_start_scan: already scanning `%s'\n", device->name);
      return SANE_STATUS_DEVICE_BUSY;
    }

  device->is_scanning = SANE_TRUE;
  device->is_cancelled = SANE_FALSE;

  /*
   * Initialisation.
   *
   */
  res = Brother_start_session(device);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to start session: %d\n", res);
      Brother_stop_scan(device);
      return res;
    }

  /*
   * Begin prologue.
   *
   */
  res = Brother_poll_for_read_flush(device, 1000000);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to read flush: %d\n", res);
      (void)Brother_stop_scan(device);
      return res;
    }

  /*
   * Construct the preparatory info block.
   * This gets the scanner going for calibration, etc I think.
   *
   */
  SANE_Byte bulk_buffer[1024];
  size_t packet_len = snprintf ((char*) bulk_buffer, sizeof(bulk_buffer),
                                "\x1b" "I\nR=%u,%u\nM=%s\n" "\x80",
                                (unsigned int) device->val[OPT_X_RESOLUTION].w,
                                (unsigned int) device->val[OPT_Y_RESOLUTION].w,
                                device->internal_scan_mode);

  size_t buf_size = packet_len;
  res = sanei_usb_write_bulk (device->fd, bulk_buffer, &buf_size);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to send prep parameter block: %d\n", res);
      (void)Brother_stop_scan(device);
      return res;
    }

  if (buf_size != packet_len)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to write initial parameter block\n");
      (void)Brother_stop_scan(device);
      return SANE_STATUS_IO_ERROR;
    }

  /*
   * Try to read the response.
   *
   * TODO: For the moment, we will discard this block.
   * It seems to contain some basic information about the prospective
   * scan session, like media limits etc.... This might allow us to restrict the
   * values in the main parameter block. We should probably decode it and
   * keep the values for later validation.
   *
   */
  buf_size = sizeof(bulk_buffer);
  useconds_t timeout = (8 * 1000000);   // 1 second.
  res = Brother_poll_for_read(device, bulk_buffer, &buf_size, &timeout);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to read prep parameter block response: %d\n", res);
      (void)Brother_stop_scan(device);
      return res;
    }

  /*
   * Construct the "ADF" block.
   *
   * TODO: Not really sure what this is for. The test scanner doesn't have
   * an ADF. We will go through the motions though.
   *
   */
  packet_len = snprintf ((char*) bulk_buffer, sizeof(bulk_buffer),
                                "\x1b" "D\nADF\n" "\x80");

  buf_size = packet_len;
  res = sanei_usb_write_bulk (device->fd, bulk_buffer, &buf_size);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to send ADF block: %d\n", res);
      (void)Brother_stop_scan(device);
      return res;
    }

  if (buf_size != packet_len)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to write ADF block\n");
      (void)Brother_stop_scan(device);
      return SANE_STATUS_IO_ERROR;
    }

  /*
   * Try to read the response.
   *
   * TODO: Just c2 response. Discard for the moment.
   *
   */
  buf_size = sizeof(bulk_buffer);
  timeout = (8 * 1000000);   // 1 second.
  res = Brother_poll_for_read(device, bulk_buffer, &buf_size, &timeout);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to read ADF block response: %d\n", res);
      (void)Brother_stop_scan(device);
      return res;
    }

  /*
   * Construct the MAIN parameter block. This will precipitate the actual scan.
   *
   */
  packet_len = snprintf ((char*) bulk_buffer, sizeof(bulk_buffer),
                                "\x1b" "X\nR=%u,%u\nM=%s\n"
                                "%s\n"
                                "B=%u\nN=%u\n"
                                "A=%u,%u,%u,%u\n"
                                "S=NORMAL_SCAN\n"
                                "P=0\nG=0\nL=0\n"
                                "\x80",
                                (unsigned int) device->val[OPT_X_RESOLUTION].w,
                                (unsigned int) device->val[OPT_Y_RESOLUTION].w,
                                device->internal_scan_mode,
                                (device->internal_scan_mode == BROTHER_INTERNAL_SCAN_MODE_COLOR)?
                                    "C=JPEG\nJ=MID\n": "C=RLENGTH\n",
                                (unsigned int)((unsigned int) device->val[OPT_BRIGHTNESS].w + 50),
                                (unsigned int)((unsigned int) device->val[OPT_CONTRAST].w + 50),
                                device->pixel_x_offset,
                                device->pixel_y_offset,
                                device->pixel_x_offset + device->pixel_x_width,
                                device->pixel_y_offset + device->pixel_y_height);

  buf_size = packet_len;
  res = sanei_usb_write_bulk (device->fd, bulk_buffer, &buf_size);
  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to send parameter block: %d\n", res);
      (void)Brother_stop_scan(device);
      return res;
    }

  if (buf_size != packet_len)
    {
      DBG (DBG_SERIOUS, "Brother_start_scan: failed to write parameter block\n");
      (void)Brother_stop_scan(device);
      return SANE_STATUS_IO_ERROR;
    }

  /*
   * Allocate a read buffer.
   *
   */
  device->read_buffer = malloc(BROTHER_READ_BUFFER_LEN);
  if (NULL == device->read_buffer)
    {
      DBG (DBG_SERIOUS,
           "Brother_start_scan: failed to allocate read buffer: %zu\n",
           (size_t) BROTHER_READ_BUFFER_LEN);
      (void)Brother_stop_scan(device);
      return SANE_STATUS_NO_MEM;
    }
  device->read_buffer_bytes = 0;
  device->block_bytes_remaining = 0;

  return SANE_STATUS_GOOD;
}

SANE_Status sane_init (SANE_Int * version_code, SANE_Auth_Callback authorize)
{
  DBG_INIT();

  DBG (DBG_IMPORTANT, "sane_init: version_code %s 0, authorize %s 0\n",
       version_code == 0 ? "=" : "!=", authorize == 0 ? "=" : "!=");
  DBG (DBG_IMPORTANT, "sane_init: SANE Brother MFP backend version %d.%d.%d from %s\n",
       SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, BUILD, PACKAGE_STRING);

  if (version_code)
    {
      *version_code = SANE_VERSION_CODE(SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, BUILD);
    }
  sanei_usb_init ();


  /*
   * Probe for supported devices.
   *
   */
  for (Brother_Model *model = models; model->model; model++)
    {
      DBG (DBG_IMPORTANT, "sane_init: Checking for device: %s\n", model->model);

      sanei_usb_find_devices (model->usb_vendor, model->usb_product, attach_with_no_ret);
    }

  /*
   * TODO: We need to find network devices and IPs might be listed in the config file if they
   * are outside our subnet. We will stick to USB for the moment.
   *
   */
//  FILE *fp = sanei_config_open (BROTHER_MFP_CONFIG_FILE);
//
//  if (!fp)
//    {
//      /* no config-file: try these */
//      attach_scanner ("/dev/scanner", 0);
//      attach_scanner ("/dev/usbscanner", 0);
//      attach_scanner ("/dev/usb/scanner", 0);
//      return SANE_STATUS_GOOD;
//    }
//
//  DBG (DBG_EVENT, "reading configure file %s\n", BROTHER_MFP_CONFIG_FILE);
//
//  char config_line[PATH_MAX];
//
//  while (sanei_config_read (config_line, sizeof(config_line), fp))
//    {
//      if (config_line[0] == '#')
//        {
//          continue; /* ignore line comments */
//        }
//      size_t len = strlen (config_line);
//
//      if (!len)
//        {
//          continue; /* ignore empty lines */
//        }
//      DBG (4, "attach_matching_devices(%s)\n", config_line);
//      sanei_usb_attach_matching_devices (config_line, attach_one);
//    }
//
//  DBG (DBG_EVENT, "finished reading configure file\n");
//
//  fclose (fp);

  return SANE_STATUS_GOOD;
}


void
sane_exit (void)
{
  Brother_Device *dev, *next;

  DBG (3, "sane_exit\n");

  for (dev = first_dev; dev; dev = next)
    {
      next = dev->next;
      free (dev->name);
      free (dev);
    }

  if (devlist)
    free (devlist);

  return;
}


SANE_Status
sane_get_devices (const SANE_Device *** device_list, SANE_Bool local_only)
{
  Brother_Device *dev;
  int i;

  DBG (DBG_EVENT, "sane_get_devices(local_only = %d)\n", local_only);

  if (devlist)
    free (devlist);

  devlist = malloc ((num_devices + 1) * sizeof (devlist[0]));
  if (!devlist)
    {
      DBG (DBG_SERIOUS,
           "sane_get_devices: failed to allocate devices list: %zu.\n",
           (num_devices + 1) * sizeof(devlist[0]));
      return SANE_STATUS_NO_MEM;
    }
  i = 0;

  for (dev = first_dev; i < num_devices; dev = dev->next)
    devlist[i++] = &dev->sane_device;

  devlist[i++] = 0;

  *device_list = devlist;

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_open (SANE_String_Const devicename, SANE_Handle * handle)
{
  Brother_Device *dev;
  SANE_Status status = SANE_STATUS_GOOD;

  DBG (3, "sane_open\n");

  /*
   * Select the device.
   *
   */
  if (devicename[0]) /* search for devicename */
    {
      DBG (DBG_EVENT, "sane_open: devicename=%s\n", devicename);

      for (dev = first_dev; dev; dev = dev->next)
        {
          if (strcmp (dev->sane_device.name, devicename) == 0)
            {
              break;
            }
        }

      if (!dev)
        {
          status = attach_with_ret(devicename, &dev);

          if (status != SANE_STATUS_GOOD)
            {
              DBG (DBG_SERIOUS, "sane_open: failed to attach device: %s\n", devicename);
              return status;
            }
        }
    }
  else
    {
      DBG (2, "sane_open: no devicename, opening first device\n");
      dev = first_dev;
    }

  if (!dev)
    {
      DBG (DBG_SERIOUS, "sane_open: unknown device: %s\n", devicename);
      return SANE_STATUS_INVAL;
    }

  /*
   * Open device internally.
   *
   */
  status = Brother_open_device (dev);

  if (status != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "sane_open: failed to open device: %s\n", devicename);
      return status;
    }

  status = init_options (dev);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "sane_open: failed to initialize device: %s\n", devicename);
    }

  *handle = dev;

  return status;
}

void
sane_close (SANE_Handle handle)
{
  Brother_Device *device;
  SANE_Status status;

  DBG (DBG_EVENT, "sane_close\n");

  device = handle;

  /*
   * Check is a valid device handle by running through our list of devices.
   *
   */
  Brother_Device *check_dev;

  for (check_dev = first_dev; check_dev; check_dev = check_dev->next)
    {
      if (device == check_dev)
        {
          break;
        }
    }

  if (NULL == check_dev)
    {
      DBG (DBG_SERIOUS, "sane_close: invalid handle specified.\n");
      return;
    }

  /*
   * Shutdown the internal device.
   *
   */
  status = Brother_close_device(device);

  if (status != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "sane_close: failed to close device: %s.\n", device->name);
      return;
    }

  return;
}

const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
  Brother_Device *device = handle;

  DBG (DBG_EVENT, "sane_get_option_descriptor: device=%s, option = %d\n", device->name, option);
  if (option < 0 || option >= NUM_OPTIONS)
    {
      DBG (DBG_WARN, "sane_get_option_descriptor: option < 0 || option > num_options\n");
      return 0;
    }

  return &device->opt[option];
}

SANE_Status
sane_control_option (SANE_Handle handle, SANE_Int option, SANE_Action action,
		     void *value, SANE_Int * info)
{
  Brother_Device *device = handle;

  SANE_Int myinfo = 0;
  SANE_Status status;

  DBG (4, "sane_control_option: handle=%s, opt=%d, act=%d, val=%p, info=%p\n",
       device->name, option, action, (void *) value, (void *) info);

  if (option < 0 || option >= NUM_OPTIONS)
    {
      DBG (1, "sane_control_option: option < 0 || option > num_options\n");
      return SANE_STATUS_INVAL;
    }

  if (!SANE_OPTION_IS_ACTIVE (device->opt[option].cap))
    {
      DBG (1, "sane_control_option: option is inactive\n");
      return SANE_STATUS_INVAL;
    }

  if (device->opt[option].type == SANE_TYPE_GROUP)
    {
      DBG (1, "sane_control_option: option is a group\n");
      return SANE_STATUS_INVAL;
    }

  switch (action)
    {
    case SANE_ACTION_SET_VALUE:
      if (!SANE_OPTION_IS_SETTABLE (device->opt[option].cap))
	{
	  DBG (1, "sane_control_option: option is not setable\n");
	  return SANE_STATUS_INVAL;
	}
      status = sanei_constrain_value (&device->opt[option], value, &myinfo);
      if (status != SANE_STATUS_GOOD)
	{
	  DBG (3, "sane_control_option: sanei_constrain_value returned %s\n",
	       sane_strstatus (status));
	  return status;
	}
      switch (option)
	{
	case OPT_TL_X:		/* Fixed with parameter reloading */
	case OPT_TL_Y:
	case OPT_BR_X:
	case OPT_BR_Y:
	  if (device->val[option].w == *(SANE_Fixed *) value)
	    {
	      DBG (4, "sane_control_option: option %d (%s) not changed\n",
		   option, device->opt[option].name);
	      break;
	    }
	  device->val[option].w = *(SANE_Fixed *) value;
	  myinfo |= SANE_INFO_RELOAD_PARAMS;
	  DBG (4, "sane_control_option: set option %d (%s) to %.0f %s\n",
	       option, device->opt[option].name,
	       SANE_UNFIX (*(SANE_Fixed *) value),
	       device->opt[option].unit == SANE_UNIT_MM ? "mm" : "dpi");
	  break;

        case OPT_X_RESOLUTION:
        case OPT_Y_RESOLUTION:
          if (device->val[option].w == *(SANE_Int *) value)
            {
              DBG (4, "sane_control_option: option %d (%s) not changed\n",
                   option, device->opt[option].name);
              break;
            }
          device->val[option].w = *(SANE_Int *) value;
          myinfo |= SANE_INFO_RELOAD_PARAMS;
          myinfo |= SANE_INFO_RELOAD_OPTIONS;
          DBG (4, "sane_control_option: set option %d (%s) to %d\n",
               option, device->opt[option].name, *(SANE_Int *) value);
          break;

        case OPT_CONTRAST:
        case OPT_BRIGHTNESS:
          if (device->val[option].w == *(SANE_Int *) value)
            {
              DBG (4, "sane_control_option: option %d (%s) not changed\n",
                   option, device->opt[option].name);
              break;
            }
          device->val[option].w = *(SANE_Int *) value;
          DBG (1, "sane_control_option: set option %d (%s) to %d\n",
               option, device->opt[option].name, *(SANE_Int *) value);
          break;

	case OPT_MODE:
	  if (strcmp (device->val[option].s, value) == 0)
	    {
	      DBG (4, "sane_control_option: option %d (%s) not changed\n",
		   option, device->opt[option].name);
	      break;
	    }
	  strcpy (device->val[option].s, (SANE_String) value);

	  myinfo |= SANE_INFO_RELOAD_PARAMS;
	  myinfo |= SANE_INFO_RELOAD_OPTIONS;
	  DBG (4, "sane_control_option: set option %d (%s) to %s\n",
	       option, device->opt[option].name, (SANE_String) value);

	  /*
	   * Mode affects other options:
	   *
	   * OPT_CONTRAST
	   * OPT_BRIGHTNESS
	   *
	   */
	  if ((strcmp((SANE_String)value, SANE_VALUE_SCAN_MODE_COLOR) == 0) ||
	      (strcmp((SANE_String)value, SANE_VALUE_SCAN_MODE_GRAY) == 0))
	    {
              device->opt[OPT_CONTRAST].cap |= SANE_CAP_INACTIVE;
              device->opt[OPT_BRIGHTNESS].cap |= SANE_CAP_INACTIVE;
	    }
	  else
	    {
              device->opt[OPT_CONTRAST].cap &= ~SANE_CAP_INACTIVE;
              device->opt[OPT_BRIGHTNESS].cap &= ~SANE_CAP_INACTIVE;
	    }
	  break;
	default:
	  DBG (1, "sane_control_option: trying to set unexpected option\n");
	  return SANE_STATUS_INVAL;
	}
      break;

    case SANE_ACTION_GET_VALUE:
      switch (option)
	{
	case OPT_NUM_OPTS:
	  *(SANE_Word *) value = NUM_OPTIONS;
	  DBG (4, "sane_control_option: get option 0, value = %d\n", NUM_OPTIONS);
	  break;
	case OPT_TL_X:		/* Fixed options */
	case OPT_TL_Y:
	case OPT_BR_X:
        case OPT_BR_Y:
          {
            *(SANE_Fixed*) value = device->val[option].w;
            DBG (4,
                "sane_control_option: get option %d (%s), value=%.1f %s\n",
                option,
                device->opt[option].name,
                SANE_UNFIX(*(SANE_Fixed* ) value),
                device->opt[option].unit == SANE_UNIT_MM ? "mm" :
                SANE_UNIT_DPI ? "dpi" : "");
            break;
          }
	case OPT_MODE:		/* String (list) options */
	  strcpy (value, device->val[option].s);
	  DBG (4, "sane_control_option: get option %d (%s), value=`%s'\n",
	       option, device->opt[option].name, (SANE_String) value);
	  break;

	case OPT_X_RESOLUTION:
        case OPT_Y_RESOLUTION:
        case OPT_BRIGHTNESS:
        case OPT_CONTRAST:
          *(SANE_Int *) value = device->val[option].w;
          DBG (4, "sane_control_option: get option %d (%s), value=%d\n",
               option, device->opt[option].name, *(SANE_Int *) value);
          break;

        default:
	  DBG (1, "sane_control_option: trying to get unexpected option\n");
	  return SANE_STATUS_INVAL;
	}
      break;
    default:
      DBG (1, "sane_control_option: trying unexpected action %d\n", action);
      return SANE_STATUS_INVAL;
    }

  if (info)
    *info = myinfo;
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  Brother_Device *device = handle;
  int rc = SANE_STATUS_GOOD;

  /*
   * Compute the geometry in terms of pixels at the selected resolution.
   * This is how the scanner wants it.
   *
   */
  device->pixel_x_width = SANE_UNFIX (device->val[OPT_BR_X].w -
                                      device->val[OPT_TL_X].w) / MM_IN_INCH *
                                         device->val[OPT_X_RESOLUTION].w;

  /*
   * X coords must be a multiple of 8.
   *
   * TODO: refine this. It's still not really right.
   *
   */
  device->pixel_x_width &= ~0x7;

  device->pixel_y_height = SANE_UNFIX (device->val[OPT_BR_Y].w -
                                       device->val[OPT_TL_Y].w) / MM_IN_INCH *
                                          device->val[OPT_Y_RESOLUTION].w;

  device->pixel_x_offset = SANE_UNFIX (device->val[OPT_TL_X].w) / MM_IN_INCH *
                                         device->val[OPT_X_RESOLUTION].w;

  device->pixel_y_offset = SANE_UNFIX (device->val[OPT_TL_Y].w) / MM_IN_INCH *
                                         device->val[OPT_Y_RESOLUTION].w;

  params->lines = device->pixel_y_height;
  params->last_frame = SANE_TRUE;

  /*
   * A bit sucky but we have to determine which mode from the text of the selected option.
   *
   */
  if (strcmp(device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_COLOR) == 0)
    {
      params->format = SANE_FRAME_RGB;
      params->pixels_per_line = device->pixel_x_width;
      params->bytes_per_line = device->pixel_x_width * 3;
      params->depth = 8;
      device->internal_scan_mode = BROTHER_INTERNAL_SCAN_MODE_COLOR;

    }
  else if (strcmp(device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY) == 0)
    {
      params->format = SANE_FRAME_GRAY;
      params->pixels_per_line = device->pixel_x_width;
      params->bytes_per_line = device->pixel_x_width;
      params->depth = 8;
      device->internal_scan_mode = BROTHER_INTERNAL_SCAN_MODE_GRAY;
    }
  else if (strcmp(device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY_DITHER) == 0)
    {
      params->format = SANE_FRAME_GRAY;
      params->pixels_per_line = device->pixel_x_width;
      params->bytes_per_line = (device->pixel_x_width + 7) / 8;
      params->depth = 1;
      device->internal_scan_mode = BROTHER_INTERNAL_SCAN_MODE_GRAY_DITHER;
    }
  else if (strcmp(device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_LINEART) == 0)
    {
      params->format = SANE_FRAME_GRAY;
      params->pixels_per_line = device->pixel_x_width;
      params->bytes_per_line = (device->pixel_x_width + 7) / 8;
      params->depth = 1;
      device->internal_scan_mode = BROTHER_INTERNAL_SCAN_MODE_BW;
    }

  /*
   * Save the parameters that we set in the device.
   * We might need them.
   *
   */
  device->params = *params;

  return rc;
}


SANE_Status
sane_start (SANE_Handle handle)
{
  Brother_Device *device = handle;
  SANE_Status res;

  DBG (DBG_EVENT, "sane_start\n");

  res = sane_get_parameters (handle, &device->params);

  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_SERIOUS, "sane_start: failed to generate parameters: %d\n", res);
      return res;
    }

  /*
   * Open a file to write the scan data.
   * TODO: This is temporary for testing.
   * We are only doing this because we need to add JPEG decoding.
   *
   */
  device->scan_file = fopen("/home/ralph/scandata.out", "w");
  if (device->scan_file == NULL)
    {
      DBG (DBG_SERIOUS, "sane_start: failed to open scan data file.\n");
      return SANE_STATUS_INVAL;
    }

  return Brother_start_scan (device);
}


SANE_Status
sane_read (SANE_Handle handle, SANE_Byte * data,
	   SANE_Int max_length, SANE_Int * length)
{
  Brother_Device *device = handle;

  if (!device->scan_file)
    {
      DBG (DBG_SERIOUS, "sane_read: no scan data file.\n");
      return SANE_STATUS_EOF;
    }

  SANE_Status res = Brother_read (device, data, max_length, length);

  if (res == SANE_STATUS_EOF)
    {
      DBG (DBG_EVENT, "sane_read: read receives EOF.\n");
      fclose(device->scan_file);
      device->scan_file = NULL;
    }

  return res;
}


void
sane_cancel (SANE_Handle handle)
{
  Brother_Device *device = handle;
  device->is_cancelled = SANE_TRUE;
}


SANE_Status
sane_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking)
{
//  DBG (3, "sane_set_io_mode: handle = %p, non_blocking = %d\n", handle,
//       non_blocking);
//  if (non_blocking != SANE_FALSE)
    return SANE_STATUS_UNSUPPORTED;
//  return SANE_STATUS_GOOD;
}


SANE_Status
sane_get_select_fd (SANE_Handle handle, SANE_Int * fd)
{
  (void) handle;		/* silence gcc */
  (void) fd;			/* silence gcc */
  return SANE_STATUS_UNSUPPORTED;
}
