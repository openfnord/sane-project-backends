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

#define MM_IN_INCH 25.4

#define DEBUG_NOT_STATIC

#include "../include/sane/config.h"

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "brother_mfp-common.h"

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_usb.h"
#include "../include/sane/sanei_debug.h"

#include "../include/sane/sanei_backend.h"

#include "brother_mfp-driver.h"

extern "C" void sanei_debug_brother_mfp_call(int level, const char *msg, ...);

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

enum Brother_Option
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
  BrotherFamily family;
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
//    { "Brother", "MFC-465CN", BROTHER_FAMILY_2, 0x04f9, 0x01d7,
//      { 0, SANE_FIX(210), 0 },
//      { 0, SANE_FIX(295), 0 },
//      { 5, 100, 150, 200, 300, 600 },
//      { 7, 100, 150, 200, 300, 600, 1200, 2400 },
//    CAP_MODE_COLOUR |
//    CAP_MODE_GRAY |
//    CAP_MODE_GRAY_DITHER |
//    CAP_MODE_BW },

    { "Brother", "MFC-J4320DW", BROTHER_FAMILY_4, 0x04f9, 0x033a,
      { 0, SANE_FIX(213.9), 0 },
      { 0, SANE_FIX(295), 0 },
      { 6, 100, 150, 200, 300, 600, 1200 },
      { 7, 100, 150, 200, 300, 600, 1200, 2400 },
    CAP_MODE_COLOUR |
    CAP_MODE_GRAY |
    CAP_MODE_GRAY_DITHER |
    CAP_MODE_BW |
    CAP_ENCODE_JPEG |
    CAP_ENCODE_RLENCODE },

    {NULL, NULL, BROTHER_FAMILY_NONE, 0, 0, {0, 0, 0}, {0, 0, 0}, {0}, {0}, 0}
};

struct BrotherDevice
{
  BrotherDevice () :
      next (nullptr),
      model (nullptr),
      sane_device {nullptr, nullptr, nullptr, nullptr},
      name (nullptr),
      modes {0},
      params {SANE_FRAME_GRAY, SANE_FALSE, 0, 0, 0, 0},
      internal_scan_mode(nullptr),
      x_res (0),
      y_res (0),
      scan_file (nullptr),
      driver (nullptr)
  {
        (void)memset(opt, 0, sizeof(opt));
  }
  struct BrotherDevice *next;

  const Brother_Model *model;
  SANE_Device sane_device;
  char *name;

  /* options */
  SANE_Option_Descriptor opt[NUM_OPTIONS];
  Option_Value val[NUM_OPTIONS];

  SANE_String_Const modes[10];
  SANE_Parameters params;
  const char *internal_scan_mode;
  SANE_Int x_res;
  SANE_Int y_res;

  FILE *scan_file;

//  SANE_Int pixel_x_offset;
//  SANE_Int pixel_x_width;
//
//  SANE_Int pixel_y_offset;
//  SANE_Int pixel_y_height;

  // TODO: Perhaps move these to the driver?
//  SANE_Bool is_open;
//  SANE_Bool is_scanning;
//  SANE_Bool in_session;
//  SANE_Bool is_cancelled;

  BrotherDriver *driver;

};

static BrotherDevice *first_dev = NULL;
static int num_devices = 0;
static SANE_Device **devlist = NULL;

static const SANE_Range constraint_brightness_contrast = { -50, +50, 1 };




static SANE_Status
attach_with_ret (const char *devicename, BrotherDevice **dev)
{
  BrotherDevice *device;
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
  device = new BrotherDevice;
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
   * Generate a driver for this device.
   *
   */
  device->driver = new BrotherUSBDriver(devicename, model->family);
  if (nullptr == device->driver)
    {
      DBG (DBG_SERIOUS, "attach_with_ret: failed to create Brother driver: %s\n", devicename);
      return SANE_STATUS_NO_MEM;
    }

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
  size_t max_size = 0;
  SANE_Int i;

  for (i = 0; strings[i]; ++i)
    {
      size_t size = strlen (strings[i]) + 1;
      if (size > max_size)
        max_size = size;
    }
  return max_size;
}


static SANE_Status
init_options (BrotherDevice *device)
{
  SANE_Option_Descriptor *od;

  DBG (2, "init_options: device=%s\n", device->name);

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
  device->val[OPT_MODE].s = new char[od->size];
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

extern "C"
{
SANE_Status sane_init (SANE_Int * version_code, SANE_Auth_Callback authorize)
{
  DBG_INIT();

  DBG (DBG_EVENT, "sane_init\n");

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
      DBG (DBG_EVENT, "sane_init: Checking for device: %s\n", model->model);

      sanei_usb_find_devices (model->usb_vendor, model->usb_product, attach_with_no_ret);
    }

  return SANE_STATUS_GOOD;
}

void
sane_exit (void)
{
  BrotherDevice *dev, *next;

  DBG (DBG_EVENT, "sane_exit\n");

  for (dev = first_dev; dev; dev = next)
    {
      next = dev->next;
      free (dev->name);
      delete dev->driver;
      free (dev);
    }

  if (devlist)
    free (devlist);

  return;
}

SANE_Status
sane_get_devices (const SANE_Device *** device_list, SANE_Bool local_only)
{
  BrotherDevice *dev;
  int i;

  DBG (DBG_EVENT, "sane_get_devices(local_only = %s)\n", local_only? "Yes": "No");

  if (devlist)
    free (devlist);

  devlist = (SANE_Device **)malloc ((num_devices + 1) * sizeof (devlist[0]));
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

  *device_list = (const SANE_Device **)devlist;

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_open (SANE_String_Const devicename, SANE_Handle * handle)
{
  BrotherDevice *dev;
  SANE_Status status = SANE_STATUS_GOOD;

  DBG (DBG_EVENT, "sane_open: %s\n", devicename);

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
      DBG (DBG_DETAIL, "sane_open: no devicename, opening first device\n");
      dev = first_dev;
    }

  if (!dev)
    {
      DBG (DBG_WARN, "sane_open: unknown device: %s\n", devicename);
      return SANE_STATUS_INVAL;
    }

  /*
   * Open device internally.
   *
   */
  status = dev->driver->Connect();

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
  BrotherDevice *device;
  SANE_Status status;

  DBG (DBG_EVENT, "sane_close\n");

  device = static_cast<BrotherDevice *>(handle);

  /*
   * Check is a valid device handle by running through our list of devices.
   *
   */
  BrotherDevice *check_dev;

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
  status = device->driver->Disconnect();

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
  BrotherDevice *device = static_cast<BrotherDevice *>(handle);

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
  BrotherDevice *device = static_cast<BrotherDevice *>(handle);

  DBG (DBG_EVENT, "sane_control_option\n");

  SANE_Int myinfo = 0;
  SANE_Status status;

  DBG (DBG_EVENT, "sane_control_option: handle=%s, opt=%d, act=%d, val=%p, info=%p\n",
       device->name, option, action, (void *) value, (void *) info);

  if (option < 0 || option >= NUM_OPTIONS)
    {
      DBG (DBG_SERIOUS, "sane_control_option: option < 0 || option > num_options\n");
      return SANE_STATUS_INVAL;
    }

  if (!SANE_OPTION_IS_ACTIVE (device->opt[option].cap))
    {
      DBG (DBG_WARN, "sane_control_option: option is inactive\n");
      return SANE_STATUS_INVAL;
    }

  if (device->opt[option].type == SANE_TYPE_GROUP)
    {
      DBG (DBG_WARN, "sane_control_option: option is a group\n");
      return SANE_STATUS_INVAL;
    }

  switch (action)
    {
    case SANE_ACTION_SET_VALUE:
      if (!SANE_OPTION_IS_SETTABLE (device->opt[option].cap))
	{
	  DBG (DBG_WARN, "sane_control_option: option is not setable\n");
	  return SANE_STATUS_INVAL;
	}
      status = sanei_constrain_value (&device->opt[option], value, &myinfo);
      if (status != SANE_STATUS_GOOD)
	{
	  DBG (DBG_WARN, "sane_control_option: sanei_constrain_value returned %s\n",
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
	      DBG (DBG_DETAIL, "sane_control_option: option %d (%s) not changed\n",
		   option, device->opt[option].name);
	      break;
	    }
	  device->val[option].w = *(SANE_Fixed *) value;
	  myinfo |= SANE_INFO_RELOAD_PARAMS;
	  DBG (DBG_DETAIL, "sane_control_option: set option %d (%s) to %.0f %s\n",
	       option, device->opt[option].name,
	       SANE_UNFIX (*(SANE_Fixed *) value),
	       device->opt[option].unit == SANE_UNIT_MM ? "mm" : "dpi");
	  break;

        case OPT_X_RESOLUTION:
        case OPT_Y_RESOLUTION:
          if (device->val[option].w == *(SANE_Int *) value)
            {
              DBG (DBG_DETAIL, "sane_control_option: option %d (%s) not changed\n",
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
              DBG (DBG_DETAIL, "sane_control_option: option %d (%s) not changed\n",
                   option, device->opt[option].name);
              break;
            }
          device->val[option].w = *(SANE_Int *) value;
          DBG (DBG_DETAIL, "sane_control_option: set option %d (%s) to %d\n",
               option, device->opt[option].name, *(SANE_Int *) value);

          status = SANE_STATUS_GOOD;
          break;

	case OPT_MODE:
	  if (strcmp (device->val[option].s, (SANE_String)value) == 0)
	    {
	      DBG (DBG_DETAIL, "sane_control_option: option %d (%s) not changed\n",
		   option, device->opt[option].name);
	      break;
	    }
	  strcpy (device->val[option].s, (SANE_String) value);

	  myinfo |= SANE_INFO_RELOAD_PARAMS;
	  myinfo |= SANE_INFO_RELOAD_OPTIONS;
	  DBG (DBG_DETAIL, "sane_control_option: set option %d (%s) to %s\n",
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
	  DBG (DBG_SERIOUS, "sane_control_option: trying to set unexpected option\n");
	  return SANE_STATUS_INVAL;
	}
      break;

    case SANE_ACTION_GET_VALUE:
      switch (option)
	{
	case OPT_NUM_OPTS:
	  *(SANE_Word *) value = NUM_OPTIONS;
	  DBG (DBG_DETAIL, "sane_control_option: get option 0, value = %d\n", NUM_OPTIONS);
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
                 (device->opt[option].unit == SANE_UNIT_DPI ? "dpi" : ""));
            break;
          }
	case OPT_MODE:		/* String (list) options */
	  strcpy ((SANE_String)value, device->val[option].s);
	  DBG (DBG_DETAIL, "sane_control_option: get option %d (%s), value=`%s'\n",
	       option, device->opt[option].name, (SANE_String) value);
	  break;

	case OPT_X_RESOLUTION:
        case OPT_Y_RESOLUTION:
        case OPT_BRIGHTNESS:
        case OPT_CONTRAST:
          *(SANE_Int *) value = device->val[option].w;
          DBG (DBG_DETAIL, "sane_control_option: get option %d (%s), value=%d\n",
               option, device->opt[option].name, *(SANE_Int *) value);
          break;

        default:
	  DBG (DBG_SERIOUS, "sane_control_option: trying to get unexpected option\n");
	  return SANE_STATUS_INVAL;
	}
      break;
    default:
      DBG (DBG_SERIOUS, "sane_control_option: trying unexpected action %d\n", action);
      return SANE_STATUS_INVAL;
    }

  if (info)
    *info = myinfo;

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  BrotherDevice *device = static_cast<BrotherDevice *>(handle);
  SANE_Status rc = SANE_STATUS_GOOD;

  DBG (DBG_EVENT, "sane_get_parameters\n");

 /*
   * Compute the geometry in terms of pixels at the selected resolution.
   * This is how the scanner wants it.
   *
   */
  SANE_Int pixel_x_width = SANE_UNFIX (device->val[OPT_BR_X].w -
                                      device->val[OPT_TL_X].w) / MM_IN_INCH *
                                         device->val[OPT_X_RESOLUTION].w;

  /*
   * X coords must be a multiple of 8.
   *
   * TODO: refine this. It's still not really right.
   *
   */
  pixel_x_width &= ~0x7;

  SANE_Int pixel_y_height = SANE_UNFIX (device->val[OPT_BR_Y].w -
                                        device->val[OPT_TL_Y].w) / MM_IN_INCH *
                                        device->val[OPT_Y_RESOLUTION].w;

  SANE_Int pixel_x_offset = SANE_UNFIX (device->val[OPT_TL_X].w) / MM_IN_INCH *
                                        device->val[OPT_X_RESOLUTION].w;

  SANE_Int pixel_y_offset = SANE_UNFIX (device->val[OPT_TL_Y].w) / MM_IN_INCH *
                                        device->val[OPT_Y_RESOLUTION].w;

  params->lines = pixel_y_height;
  params->last_frame = SANE_TRUE;

  rc = device->driver->SetScanDimensions (pixel_x_offset,
                                          pixel_x_width,
                                          pixel_y_offset,
                                          pixel_y_height);
  if (rc != SANE_STATUS_GOOD)
    {
      return rc;
    }

  rc = device->driver->SetRes ((int)device->val[OPT_X_RESOLUTION].w, (int)device->val[OPT_Y_RESOLUTION].w);
  if (rc != SANE_STATUS_GOOD)
    {
      return rc;
    }

  /*
   * Brightness and contrast.
   *
   */
  rc = device->driver->SetContrast((SANE_Int)device->val[OPT_CONTRAST].w);
  if (rc != SANE_STATUS_GOOD)
    {
      return rc;
    }

  rc = device->driver->SetBrightness((SANE_Int)device->val[OPT_BRIGHTNESS].w);
  if (rc != SANE_STATUS_GOOD)
    {
      return rc;
    }

  /*
   * A bit sucky but we have to determine which mode from the text of the selected option.
   *
   */
  if (strcmp(device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_COLOR) == 0)
    {
      params->format = SANE_FRAME_RGB;
      params->pixels_per_line = pixel_x_width;
      params->bytes_per_line = pixel_x_width * 3;
      params->depth = 8;

      rc = device->driver->SetScanMode(BROTHER_SCAN_MODE_COLOR);
      if (rc != SANE_STATUS_GOOD)
        {
          return rc;
        }
    }
  else if (strcmp(device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY) == 0)
    {
      params->format = SANE_FRAME_GRAY;
      params->pixels_per_line = pixel_x_width;
      params->bytes_per_line = pixel_x_width;
      params->depth = 8;

      rc = device->driver->SetScanMode(BROTHER_SCAN_MODE_GRAY);
      if (rc != SANE_STATUS_GOOD)
        {
          return rc;
        }
    }
  else if (strcmp(device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY_DITHER) == 0)
    {
      params->format = SANE_FRAME_GRAY;
      params->pixels_per_line = pixel_x_width;
      params->bytes_per_line = (pixel_x_width + 7) / 8;
      params->depth = 1;

      rc = device->driver->SetScanMode(BROTHER_SCAN_MODE_GRAY_DITHERED);
      if (rc != SANE_STATUS_GOOD)
        {
          return rc;
        }
    }
  else if (strcmp(device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_LINEART) == 0)
    {
      params->format = SANE_FRAME_GRAY;
      params->pixels_per_line = pixel_x_width;
      params->bytes_per_line = (pixel_x_width + 7) / 8;
      params->depth = 1;

      rc = device->driver->SetScanMode(BROTHER_SCAN_MODE_TEXT);
      if (rc != SANE_STATUS_GOOD)
        {
          return rc;
        }
    }

  /*
   * Save the parameters that we set in the device.
   * We might need them.
   *
   */
  device->params = *params;

  DBG (DBG_SERIOUS,
       "sane_get_parameters: params: format:%d last_frame:%d bytes_per_line:%d "
       "pixels_per_line: %d lines:%d depth:%d\n",
       params->format,
       params->last_frame,
       params->bytes_per_line,
       params->pixels_per_line,
       params->lines,
       params->depth);

//  SANE_Frame format;
//  SANE_Bool last_frame;
//  SANE_Int bytes_per_line;
//  SANE_Int pixels_per_line;
//  SANE_Int lines;
//  SANE_Int depth;
  return rc;
}


SANE_Status
sane_start (SANE_Handle handle)
{
  BrotherDevice *device = static_cast<BrotherDevice *>(handle);
  SANE_Status res;

  DBG (DBG_EVENT, "sane_start\n");

  /*
   * We manually call sane_get_parameters because there are computations and driver
   * settings in there that we need to happen. We don't rely on the frontend
   * to ensure that this is called as calling it is not a pre-requisite for scannning.
   *
   */
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

  return device->driver->StartScan();
}


SANE_Status
sane_read (SANE_Handle handle, SANE_Byte * data,
	   SANE_Int max_length, SANE_Int * length)
{
  BrotherDevice *device = static_cast<BrotherDevice *>(handle);

  DBG (DBG_EVENT, "sane_read\n");

  *length = 0;

  if (!device->scan_file)
    {
      DBG (DBG_SERIOUS, "sane_read: no scan data file.\n");
      return SANE_STATUS_EOF;
    }

  size_t scan_len = 0;
  SANE_Status res = device->driver->ReadScanData(data, (size_t)max_length, &scan_len);
  *length = (SANE_Int)scan_len;

  /*
   * TODO: remove me! For test only.
   *
   */
  fwrite(data, *length, 1, device->scan_file);

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
  SANE_Status res;
  BrotherDevice *device = static_cast<BrotherDevice *>(handle);

  DBG (DBG_EVENT, "sane_cancel\n");

  res = device->driver->CancelScan();

  if (res != SANE_STATUS_GOOD)
    {
      DBG (DBG_EVENT, "sane_cancel: scan failed to cancel.\n");
    }

  if (device->scan_file)
    {
      fclose(device->scan_file);
      device->scan_file = NULL;
    }
}


SANE_Status
sane_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking)
{
  (void)handle;
  (void)non_blocking;

  DBG (DBG_EVENT, "sane_set_io_mode\n");

  return SANE_STATUS_UNSUPPORTED;
}


SANE_Status
sane_get_select_fd (SANE_Handle handle, SANE_Int * fd)
{
  (void) handle;
  (void) fd;

  DBG (DBG_EVENT, "sane_get_select_fd\n");
  return SANE_STATUS_UNSUPPORTED;
}

}