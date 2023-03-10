/* sane - Scanner Access Now Easy.

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
   This file implements a SANE backend for the Ibm 2456 flatbed scanner,
   written by mf <massifr@tiscalinet.it>. It derives from the backend for
   Ricoh flatbed scanners written by Feico W. Dillema.

   Currently maintained by Henning Meier-Geinitz <henning@meier-geinitz.de>.
*/

#define BUILD 5

#include "../include/sane/config.h"

#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <ctype.h>

#include "../include/sane/sane.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_scsi.h"

#define BACKEND_NAME ibm
#include "../include/sane/sanei_backend.h"

#ifndef PATH_MAX
# define PATH_MAX	1024
#endif

#include "../include/sane/sanei_config.h"
#define IBM_CONFIG_FILE "ibm.conf"

#include "ibm.h"
#include "ibm-scsi.c"

#define MAX(a,b)	((a) > (b) ? (a) : (b))

static int num_devices = 0;
static Ibm_Device *first_dev = NULL;
static Ibm_Scanner *first_handle = NULL;
/* static int is50 = 0; */


static size_t
max_string_size (const SANE_String_Const strings[])
{
  size_t size, max_size = 0;
  int i;
  DBG (11, ">> max_string_size\n");

  for (i = 0; strings[i]; ++i)
    {
      size = strlen (strings[i]) + 1;
      if (size > max_size)
        max_size = size;
    }

  DBG (11, "<< max_string_size\n");
  return max_size;
}

static SANE_Status
attach (const char *devnam, Ibm_Device ** devp)
{
  SANE_Status status;
  Ibm_Device *dev;

  int fd;
  struct inquiry_data ibuf;
  struct measurements_units_page mup;
  struct ibm_window_data wbuf;
  size_t buf_size;
  char *str;
  DBG (11, ">> attach\n");

  for (dev = first_dev; dev; dev = dev->next)
    {
      if (strcmp (dev->sane.name, devnam) == 0)
        {
          if (devp)
            *devp = dev;
          return (SANE_STATUS_GOOD);
        }
    }

  DBG (3, "attach: opening %s\n", devnam);
  status = sanei_scsi_open (devnam, &fd, NULL, NULL);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: open failed: %s\n", sane_strstatus (status));
      return (status);
    }

  DBG (3, "attach: sending INQUIRY\n");
  memset (&ibuf, 0, sizeof (ibuf));
  buf_size = sizeof(ibuf);
/* next line by mf */
  ibuf.byte2 = 2;
  status = inquiry (fd, &ibuf, &buf_size);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: inquiry failed: %s\n", sane_strstatus (status));
      sanei_scsi_close (fd);
      return (status);
    }

  if (ibuf.devtype != 6)
    {
      DBG (1, "attach: device \"%s\" is not a scanner\n", devnam);
      sanei_scsi_close (fd);
      return (SANE_STATUS_INVAL);
    }

  if (!(
	(strncmp ((char *)ibuf.vendor, "IBM", 3) ==0
         && strncmp ((char *)ibuf.product, "2456", 4) == 0)
        || (strncmp ((char *)ibuf.vendor, "RICOH", 5) == 0
	    && strncmp ((char *)ibuf.product, "IS420", 5) == 0)
        || (strncmp ((char *)ibuf.vendor, "RICOH", 5) == 0
	    && strncmp ((char *)ibuf.product, "IS410", 5) == 0)
        || (strncmp ((char *)ibuf.vendor, "RICOH", 5) == 0
	    && strncmp ((char *)ibuf.product, "IS430", 5) == 0)
	))
    {
      DBG (1, "attach: device \"%s\" doesn't look like a scanner I know\n",
	   devnam);
      sanei_scsi_close (fd);
      return (SANE_STATUS_INVAL);
    }

  DBG (3, "attach: sending TEST_UNIT_READY\n");
  status = test_unit_ready (fd);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: test unit ready failed (%s)\n",
           sane_strstatus (status));
      sanei_scsi_close (fd);
      return (status);
    }
  /*
   * Causes a problem with RICOH IS420
   * Ignore this function ... seems to work ok
   * Suggested to George Murphy george@topfloor.ie by henning
   */
  if (strncmp((char *)ibuf.vendor, "RICOH", 5) != 0
      && strncmp((char *)ibuf.product, "IS420", 5) != 0)
    {
      DBG (3, "attach: sending OBJECT POSITION\n");
      status = object_position (fd, OBJECT_POSITION_UNLOAD);
      if (status != SANE_STATUS_GOOD)
    	{
	  DBG (1, "attach: OBJECT POSITION failed\n");
	  sanei_scsi_close (fd);
	  return (SANE_STATUS_INVAL);
    	}
    }

  memset (&mup, 0, sizeof (mup));
  mup.page_code = MEASUREMENTS_PAGE;
  mup.parameter_length = 0x06;
  mup.bmu = INCHES;
  mup.mud[0] = (DEFAULT_MUD >> 8) & 0xff;
  mup.mud[1] = (DEFAULT_MUD & 0xff);

#if 0
  DBG (3, "attach: sending MODE SELECT\n");
  status = mode_select (fd, (struct mode_pages *) &mup);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: MODE_SELECT failed\n");
      sanei_scsi_close (fd);
      return (SANE_STATUS_INVAL);
    }
#endif

#if 0
  DBG (3, "attach: sending MODE SENSE\n");
  memset (&mup, 0, sizeof (mup));
  status = mode_sense (fd, (struct mode_pages *) &mup, PC_CURRENT | MEASUREMENTS_PAGE);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: MODE_SENSE failed\n");
      sanei_scsi_close (fd);
      return (SANE_STATUS_INVAL);
    }
#endif

  DBG (3, "attach: sending GET WINDOW\n");
  memset (&wbuf, 0, sizeof (wbuf));
  status = get_window (fd, &wbuf);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: GET_WINDOW failed %d\n", status);
      sanei_scsi_close (fd);
      DBG (11, "<< attach\n");
      return (SANE_STATUS_INVAL);
    }

  sanei_scsi_close (fd);

  dev = malloc (sizeof (*dev));
  if (!dev)
    return (SANE_STATUS_NO_MEM);
  memset (dev, 0, sizeof (*dev));

  dev->sane.name = strdup (devnam);
  dev->sane.vendor = "IBM";

  size_t prod_rev_size = sizeof(ibuf.product) + sizeof(ibuf.revision) + 1;
  str = malloc (prod_rev_size);
  if (str)
    {
      snprintf (str, prod_rev_size, "%.*s%.*s",
                (int) sizeof(ibuf.product), (const char *) ibuf.product,
                (int) sizeof(ibuf.revision), (const char *) ibuf.revision);
    }
  dev->sane.model = str;
  dev->sane.type = "flatbed scanner";

  DBG (5, "dev->sane.name = %s\n", dev->sane.name);
  DBG (5, "dev->sane.vendor = %s\n", dev->sane.vendor);
  DBG (5, "dev->sane.model = %s\n", dev->sane.model);
  DBG (5, "dev->sane.type = %s\n", dev->sane.type);

  dev->info.xres_default = _2btol(wbuf.x_res);
  dev->info.yres_default = _2btol(wbuf.y_res);
  dev->info.image_mode_default = wbuf.image_comp;

  /* if you throw the MRIF bit the brightness control reverses too */
  /* so I reverse the reversal in software for symmetry's sake */
  /* I should make this into an option */

  if (wbuf.image_comp == IBM_GRAYSCALE || wbuf.image_comp == IBM_DITHERED_MONOCHROME)
    {
      dev->info.brightness_default = 256 - wbuf.brightness;
/*
      if (is50)
	dev->info.contrast_default = wbuf.contrast;
      else
*/
      dev->info.contrast_default = 256 - wbuf.contrast;
    }
  else /* wbuf.image_comp == IBM_BINARY_MONOCHROME */
    {
      dev->info.brightness_default = wbuf.brightness;
      dev->info.contrast_default = wbuf.contrast;
    }

/* da rivedere
  dev->info.adf_default = wbuf.adf_state;
*/
  dev->info.adf_default = ADF_UNUSED;
  dev->info.adf_default = IBM_PAPER_USER_DEFINED;

#if 1
  dev->info.bmu = mup.bmu;
  dev->info.mud = _2btol(mup.mud);
  if (dev->info.mud == 0) {
    /* The Ricoh says it uses points as default Basic Measurement Unit */
    /* but gives a Measurement Unit Divisor of zero */
    /* So, we set it to the default (SCSI-standard) of 1200 */
    /* with BMU in inches, i.e. 1200 points equal 1 inch */
    dev->info.bmu = INCHES;
    dev->info.mud = DEFAULT_MUD;
  }
#else
    dev->info.bmu = INCHES;
    dev->info.mud = DEFAULT_MUD;
#endif

  DBG (5, "xres_default=%d\n", dev->info.xres_default);
  DBG (5, "xres_range.max=%d\n", dev->info.xres_range.max);
  DBG (5, "xres_range.min=%d\n", dev->info.xres_range.min);

  DBG (5, "yres_default=%d\n", dev->info.yres_default);
  DBG (5, "yres_range.max=%d\n", dev->info.yres_range.max);
  DBG (5, "yres_range.min=%d\n", dev->info.yres_range.min);

  DBG (5, "x_range.max=%d\n", dev->info.x_range.max);
  DBG (5, "y_range.max=%d\n", dev->info.y_range.max);

  DBG (5, "image_mode=%d\n", dev->info.image_mode_default);

  DBG (5, "brightness=%d\n", dev->info.brightness_default);
  DBG (5, "contrast=%d\n", dev->info.contrast_default);

  DBG (5, "adf_state=%d\n", dev->info.adf_default);

  DBG (5, "bmu=%d\n", dev->info.bmu);
  DBG (5, "mud=%d\n", dev->info.mud);

  ++num_devices;
  dev->next = first_dev;
  first_dev = dev;

  if (devp)
    *devp = dev;

  DBG (11, "<< attach\n");
  return (SANE_STATUS_GOOD);
}

static SANE_Status
attach_one(const char *devnam)
{
  attach (devnam, NULL);
  return SANE_STATUS_GOOD;
}

static SANE_Status
init_options (Ibm_Scanner * s)
{
  int i;
  DBG (11, ">> init_options\n");

  memset (s->opt, 0, sizeof (s->opt));
  memset (s->val, 0, sizeof (s->val));

  for (i = 0; i < NUM_OPTIONS; ++i)
    {
      s->opt[i].size = sizeof (SANE_Word);
      s->opt[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    }

  s->opt[OPT_NUM_OPTS].title = SANE_TITLE_NUM_OPTIONS;
  s->opt[OPT_NUM_OPTS].desc = SANE_DESC_NUM_OPTIONS;
  s->opt[OPT_NUM_OPTS].type = SANE_TYPE_INT;
  s->opt[OPT_NUM_OPTS].cap = SANE_CAP_SOFT_DETECT;
  s->val[OPT_NUM_OPTS].w = NUM_OPTIONS;

  /* "Mode" group: */
  s->opt[OPT_MODE_GROUP].title = "Scan Mode";
  s->opt[OPT_MODE_GROUP].desc = "";
  s->opt[OPT_MODE_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_MODE_GROUP].cap = 0;
  s->opt[OPT_MODE_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* scan mode */
  s->opt[OPT_MODE].name = SANE_NAME_SCAN_MODE;
  s->opt[OPT_MODE].title = SANE_TITLE_SCAN_MODE;
  s->opt[OPT_MODE].desc = SANE_DESC_SCAN_MODE;
  s->opt[OPT_MODE].type = SANE_TYPE_STRING;
  s->opt[OPT_MODE].size = max_string_size (mode_list);
  s->opt[OPT_MODE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  s->opt[OPT_MODE].constraint.string_list = mode_list;
  s->val[OPT_MODE].s = strdup (mode_list[s->hw->info.image_mode_default]);

  /* x resolution */
  s->opt[OPT_X_RESOLUTION].name = "X" SANE_NAME_SCAN_RESOLUTION;
  s->opt[OPT_X_RESOLUTION].title = "X " SANE_TITLE_SCAN_RESOLUTION;
  s->opt[OPT_X_RESOLUTION].desc = SANE_DESC_SCAN_RESOLUTION;
  s->opt[OPT_X_RESOLUTION].type = SANE_TYPE_INT;
  s->opt[OPT_X_RESOLUTION].unit = SANE_UNIT_DPI;
  s->opt[OPT_X_RESOLUTION].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_X_RESOLUTION].constraint.range = &ibm2456_res_range;
  s->val[OPT_X_RESOLUTION].w = s->hw->info.xres_default;
/*
  if (is50)
    s->opt[OPT_X_RESOLUTION].constraint.range = &is50_res_range;
  else
*/
  s->opt[OPT_X_RESOLUTION].constraint.range = &ibm2456_res_range;

  /* y resolution */
  s->opt[OPT_Y_RESOLUTION].name = "Y" SANE_NAME_SCAN_RESOLUTION;
  s->opt[OPT_Y_RESOLUTION].title = "Y " SANE_TITLE_SCAN_RESOLUTION;
  s->opt[OPT_Y_RESOLUTION].desc = SANE_DESC_SCAN_RESOLUTION;
  s->opt[OPT_Y_RESOLUTION].type = SANE_TYPE_INT;
  s->opt[OPT_Y_RESOLUTION].unit = SANE_UNIT_DPI;
  s->opt[OPT_Y_RESOLUTION].constraint_type = SANE_CONSTRAINT_RANGE;
  s->val[OPT_Y_RESOLUTION].w =  s->hw->info.yres_default;
  s->opt[OPT_Y_RESOLUTION].constraint.range = &ibm2456_res_range;

  /* adf */
  s->opt[OPT_ADF].name = "adf";
  s->opt[OPT_ADF].title = "Use ADF";
  s->opt[OPT_ADF].desc = "Uses the automatic document feeder.";
  s->opt[OPT_ADF].type = SANE_TYPE_BOOL;
  s->opt[OPT_ADF].unit = SANE_UNIT_NONE;
  s->opt[OPT_ADF].constraint_type = SANE_CONSTRAINT_NONE;
  s->val[OPT_ADF].b =  s->hw->info.adf_default;

  /* "Geometry" group: */
  s->opt[OPT_GEOMETRY_GROUP].title = "Geometry";
  s->opt[OPT_GEOMETRY_GROUP].desc = "";
  s->opt[OPT_GEOMETRY_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_GEOMETRY_GROUP].cap = SANE_CAP_ADVANCED;
  s->opt[OPT_GEOMETRY_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* paper */
  s->opt[OPT_PAPER].name = "paper";
  s->opt[OPT_PAPER].title = "Paper format";
  s->opt[OPT_PAPER].desc = "Sets the paper format.";
  s->opt[OPT_PAPER].type = SANE_TYPE_STRING;
  s->opt[OPT_PAPER].size = max_string_size (paper_list);
  s->opt[OPT_PAPER].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  s->opt[OPT_PAPER].constraint.string_list = paper_list;
  s->val[OPT_PAPER].s = strdup (paper_list[s->hw->info.paper_default]);

  /* top-left x */
  s->opt[OPT_TL_X].name = SANE_NAME_SCAN_TL_X;
  s->opt[OPT_TL_X].title = SANE_TITLE_SCAN_TL_X;
  s->opt[OPT_TL_X].desc = SANE_DESC_SCAN_TL_X;
  s->opt[OPT_TL_X].type = SANE_TYPE_INT;
  s->opt[OPT_TL_X].unit = SANE_UNIT_PIXEL;
  s->opt[OPT_TL_X].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_TL_X].constraint.range = &default_x_range;
  s->val[OPT_TL_X].w = 0;

  /* top-left y */
  s->opt[OPT_TL_Y].name = SANE_NAME_SCAN_TL_Y;
  s->opt[OPT_TL_Y].title = SANE_TITLE_SCAN_TL_Y;
  s->opt[OPT_TL_Y].desc = SANE_DESC_SCAN_TL_Y;
  s->opt[OPT_TL_Y].type = SANE_TYPE_INT;
  s->opt[OPT_TL_Y].unit = SANE_UNIT_PIXEL;
  s->opt[OPT_TL_Y].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_TL_Y].constraint.range = &default_y_range;
  s->val[OPT_TL_Y].w = 0;

  /* bottom-right x */
  s->opt[OPT_BR_X].name = SANE_NAME_SCAN_BR_X;
  s->opt[OPT_BR_X].title = SANE_TITLE_SCAN_BR_X;
  s->opt[OPT_BR_X].desc = SANE_DESC_SCAN_BR_X;
  s->opt[OPT_BR_X].type = SANE_TYPE_INT;
  s->opt[OPT_BR_X].unit = SANE_UNIT_PIXEL;
  s->opt[OPT_BR_X].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_BR_X].constraint.range = &default_x_range;
  s->val[OPT_BR_X].w = default_x_range.max;

  /* bottom-right y */
  s->opt[OPT_BR_Y].name = SANE_NAME_SCAN_BR_Y;
  s->opt[OPT_BR_Y].title = SANE_TITLE_SCAN_BR_Y;
  s->opt[OPT_BR_Y].desc = SANE_DESC_SCAN_BR_Y;
  s->opt[OPT_BR_Y].type = SANE_TYPE_INT;
  s->opt[OPT_BR_Y].unit = SANE_UNIT_PIXEL;
  s->opt[OPT_BR_Y].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_BR_Y].constraint.range = &default_y_range;
  s->val[OPT_BR_Y].w = default_y_range.max;

  /* "Enhancement" group: */
  s->opt[OPT_ENHANCEMENT_GROUP].title = "Enhancement";
  s->opt[OPT_ENHANCEMENT_GROUP].desc = "";
  s->opt[OPT_ENHANCEMENT_GROUP].type = SANE_TYPE_GROUP;
  s->opt[OPT_ENHANCEMENT_GROUP].cap = 0;
  s->opt[OPT_ENHANCEMENT_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

  /* brightness */
  s->opt[OPT_BRIGHTNESS].name = SANE_NAME_BRIGHTNESS;
  s->opt[OPT_BRIGHTNESS].title = SANE_TITLE_BRIGHTNESS;
  s->opt[OPT_BRIGHTNESS].desc = SANE_DESC_BRIGHTNESS;
  s->opt[OPT_BRIGHTNESS].type = SANE_TYPE_INT;
  s->opt[OPT_BRIGHTNESS].unit = SANE_UNIT_NONE;
  s->opt[OPT_BRIGHTNESS].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_BRIGHTNESS].constraint.range = &u8_range;
  s->val[OPT_BRIGHTNESS].w =  s->hw->info.brightness_default;

  /* contrast */
  s->opt[OPT_CONTRAST].name = SANE_NAME_CONTRAST;
  s->opt[OPT_CONTRAST].title = SANE_TITLE_CONTRAST;
  s->opt[OPT_CONTRAST].desc = SANE_DESC_CONTRAST;
  s->opt[OPT_CONTRAST].type = SANE_TYPE_INT;
  s->opt[OPT_CONTRAST].unit = SANE_UNIT_NONE;
  s->opt[OPT_CONTRAST].constraint_type = SANE_CONSTRAINT_RANGE;
  s->opt[OPT_CONTRAST].constraint.range = &u8_range;
  s->val[OPT_CONTRAST].w =  s->hw->info.contrast_default;

  DBG (11, "<< init_options\n");
  return SANE_STATUS_GOOD;
}

static SANE_Status
do_cancel (Ibm_Scanner * s)
{
  SANE_Status status;
  DBG (11, ">> do_cancel\n");

  DBG (3, "cancel: sending OBJECT POSITION\n");
  status = object_position (s->fd, OBJECT_POSITION_UNLOAD);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "cancel: OBJECT POSITION failed\n");
    }

  s->scanning = SANE_FALSE;

  if (s->fd >= 0)
    {
      sanei_scsi_close (s->fd);
      s->fd = -1;
    }

  DBG (11, "<< do_cancel\n");
  return (SANE_STATUS_CANCELLED);
}

SANE_Status
sane_init (SANE_Int * version_code, SANE_Auth_Callback authorize)
{
  char devnam[PATH_MAX] = "/dev/scanner";
  FILE *fp;

  DBG_INIT ();
  DBG (11, ">> sane_init (authorize %s null)\n", (authorize) ? "!=" : "==");

#if defined PACKAGE && defined VERSION
  DBG (2, "sane_init: ibm backend version %d.%d-%d ("
       PACKAGE " " VERSION ")\n", SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, BUILD);
#endif

  if (version_code)
    *version_code = SANE_VERSION_CODE (SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, 0);

  fp = sanei_config_open(IBM_CONFIG_FILE);
  if (fp)
    {
      char line[PATH_MAX], *lp;
      size_t len;

      /* read config file */
      while (sanei_config_read (line, sizeof (line), fp))
        {
          if (line[0] == '#')           /* ignore line comments */
            continue;
          len = strlen (line);

          if (!len)
            continue;                   /* ignore empty lines */

	  /* skip white space: */
	  for (lp = line; isspace(*lp); ++lp)
            ;
          strcpy (devnam, lp);
        }
      fclose (fp);
    }
  sanei_config_attach_matching_devices (devnam, attach_one);
  DBG (11, "<< sane_init\n");
  return SANE_STATUS_GOOD;
}

void
sane_exit (void)
{
  Ibm_Device *dev, *next;
  DBG (11, ">> sane_exit\n");

  for (dev = first_dev; dev; dev = next)
    {
      next = dev->next;
      free ((void *) dev->sane.name);
      free ((void *) dev->sane.model);
      free (dev);
    }

  DBG (11, "<< sane_exit\n");
}

SANE_Status
sane_get_devices (const SANE_Device *** device_list, SANE_Bool local_only)
{
  static const SANE_Device **devlist = 0;
  Ibm_Device *dev;
  int i;
  DBG (11, ">> sane_get_devices (local_only = %d)\n", local_only);

  if (devlist)
    free (devlist);
  devlist = malloc ((num_devices + 1) * sizeof (devlist[0]));
  if (!devlist)
    return (SANE_STATUS_NO_MEM);

  i = 0;
  for (dev = first_dev; dev; dev = dev->next)
    devlist[i++] = &dev->sane;
  devlist[i++] = 0;

  *device_list = devlist;

  DBG (11, "<< sane_get_devices\n");
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_open (SANE_String_Const devnam, SANE_Handle * handle)
{
  SANE_Status status;
  Ibm_Device *dev;
  Ibm_Scanner *s;
  DBG (11, ">> sane_open\n");

  if (devnam[0] == '\0')
    {
      for (dev = first_dev; dev; dev = dev->next)
        {
          if (strcmp (dev->sane.name, devnam) == 0)
            break;
        }

      if (!dev)
        {
          status = attach (devnam, &dev);
          if (status != SANE_STATUS_GOOD)
            return (status);
        }
    }
  else
    {
      dev = first_dev;
    }

  if (!dev)
    return (SANE_STATUS_INVAL);

  s = malloc (sizeof (*s));
  if (!s)
    return SANE_STATUS_NO_MEM;
  memset (s, 0, sizeof (*s));

  s->fd = -1;
  s->hw = dev;

  init_options (s);

  s->next = first_handle;
  first_handle = s;

  *handle = s;

  DBG (11, "<< sane_open\n");
  return SANE_STATUS_GOOD;
}

void
sane_close (SANE_Handle handle)
{
  Ibm_Scanner *s = (Ibm_Scanner *) handle;
  DBG (11, ">> sane_close\n");

  if (s->fd != -1)
    sanei_scsi_close (s->fd);
  free (s);

  DBG (11, ">> sane_close\n");
}

const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
  Ibm_Scanner *s = handle;
  DBG (11, ">> sane_get_option_descriptor\n");

  if ((unsigned) option >= NUM_OPTIONS)
    return (0);

  DBG (11, "<< sane_get_option_descriptor\n");
  return (s->opt + option);
}

SANE_Status
sane_control_option (SANE_Handle handle, SANE_Int option,
                     SANE_Action action, void *val, SANE_Int * info)
{
  Ibm_Scanner *s = handle;
  SANE_Status status;
  SANE_Word cap;
  DBG (11, ">> sane_control_option\n");

  if (info)
    *info = 0;

  if (s->scanning)
    return (SANE_STATUS_DEVICE_BUSY);
  if (option >= NUM_OPTIONS)
    return (SANE_STATUS_INVAL);

  cap = s->opt[option].cap;
  if (!SANE_OPTION_IS_ACTIVE (cap))
    return (SANE_STATUS_INVAL);

  if (action == SANE_ACTION_GET_VALUE)
    {
      DBG (11, "sane_control_option get_value\n");
      switch (option)
        {
          /* word options: */
        case OPT_X_RESOLUTION:
        case OPT_Y_RESOLUTION:
        case OPT_TL_X:
        case OPT_TL_Y:
        case OPT_BR_X:
        case OPT_BR_Y:
        case OPT_NUM_OPTS:
        case OPT_BRIGHTNESS:
        case OPT_CONTRAST:
          *(SANE_Word *) val = s->val[option].w;
          return (SANE_STATUS_GOOD);

          /* bool options: */
	case OPT_ADF:
          *(SANE_Bool *) val = s->val[option].b;
          return (SANE_STATUS_GOOD);

          /* string options: */
        case OPT_MODE:
	case OPT_PAPER:
          strcpy (val, s->val[option].s);
          return (SANE_STATUS_GOOD);
        }
    }
  else {
    DBG (11, "sane_control_option set_value\n");
    if (action == SANE_ACTION_SET_VALUE)
    {
      if (!SANE_OPTION_IS_SETTABLE (cap))
        return (SANE_STATUS_INVAL);

      status = sanei_constrain_value (s->opt + option, val, info);
      if (status != SANE_STATUS_GOOD)
        return status;

      switch (option)
        {
          /* (mostly) side-effect-free word options: */
        case OPT_X_RESOLUTION:
        case OPT_Y_RESOLUTION:
          if (info && s->val[option].w != *(SANE_Word *) val)
            *info |= SANE_INFO_RELOAD_PARAMS;
          s->val[option].w = *(SANE_Word *) val;
          return (SANE_STATUS_GOOD);

	case OPT_TL_X:
        case OPT_TL_Y:
        case OPT_BR_X:
        case OPT_BR_Y:
          if (info && s->val[option].w != *(SANE_Word *) val)
            *info |= SANE_INFO_RELOAD_PARAMS;
          s->val[option].w = *(SANE_Word *) val;
	  /* resets the paper format to user defined */
	  if (strcmp(s->val[OPT_PAPER].s, paper_list[IBM_PAPER_USER_DEFINED]) != 0)
	    {
	      if (info)
		*info |= SANE_INFO_RELOAD_OPTIONS;
              if (s->val[OPT_PAPER].s)
                free (s->val[OPT_PAPER].s);
              s->val[OPT_PAPER].s = strdup (paper_list[IBM_PAPER_USER_DEFINED]);
	    }
          return (SANE_STATUS_GOOD);

	case OPT_NUM_OPTS:
        case OPT_BRIGHTNESS:
        case OPT_CONTRAST:
          s->val[option].w = *(SANE_Word *) val;
          return (SANE_STATUS_GOOD);

        case OPT_MODE:
          if (info && strcmp (s->val[option].s, (SANE_String) val))
            *info |= SANE_INFO_RELOAD_OPTIONS | SANE_INFO_RELOAD_PARAMS;
          if (s->val[option].s)
            free (s->val[option].s);
          s->val[option].s = strdup (val);
          return (SANE_STATUS_GOOD);

	case OPT_ADF:
	  s->val[option].b = *(SANE_Bool *) val;
	  if (*(SANE_Bool *) val)
	    s->adf_state = ADF_ARMED;
          else
	    s->adf_state = ADF_UNUSED;
	  return (SANE_STATUS_GOOD);

	case OPT_PAPER:
          if (info && strcmp (s->val[option].s, (SANE_String) val))
            *info |= SANE_INFO_RELOAD_OPTIONS | SANE_INFO_RELOAD_PARAMS;
          if (s->val[option].s)
            free (s->val[option].s);
          s->val[option].s = strdup (val);
	  if (strcmp (s->val[OPT_PAPER].s, "User") != 0)
	    {
              s->val[OPT_TL_X].w = 0;
	      s->val[OPT_TL_Y].w = 0;
	    if (strcmp (s->val[OPT_PAPER].s, "A3") == 0)
	      {
                s->val[OPT_BR_X].w = PAPER_A3_W;
	        s->val[OPT_BR_Y].w = PAPER_A3_H;
	      }
	    else if (strcmp (s->val[OPT_PAPER].s, "A4") == 0)
	      {
                s->val[OPT_BR_X].w = PAPER_A4_W;
	        s->val[OPT_BR_Y].w = PAPER_A4_H;
	      }
	    else if (strcmp (s->val[OPT_PAPER].s, "A4R") == 0)
	      {
                s->val[OPT_BR_X].w = PAPER_A4R_W;
	        s->val[OPT_BR_Y].w = PAPER_A4R_H;
	      }
	    else if (strcmp (s->val[OPT_PAPER].s, "A5") == 0)
	      {
                s->val[OPT_BR_X].w = PAPER_A5_W;
	        s->val[OPT_BR_Y].w = PAPER_A5_H;
	      }
	    else if (strcmp (s->val[OPT_PAPER].s, "A5R") == 0)
	      {
                s->val[OPT_BR_X].w = PAPER_A5R_W;
	        s->val[OPT_BR_Y].w = PAPER_A5R_H;
  	      }
	    else if (strcmp (s->val[OPT_PAPER].s, "A6") == 0)
	      {
                s->val[OPT_BR_X].w = PAPER_A6_W;
	        s->val[OPT_BR_Y].w = PAPER_A6_H;
	      }
	    else if (strcmp (s->val[OPT_PAPER].s, "B4") == 0)
	      {
                s->val[OPT_BR_X].w = PAPER_B4_W;
	        s->val[OPT_BR_Y].w = PAPER_B4_H;
	      }
	    else if (strcmp (s->val[OPT_PAPER].s, "Legal") == 0)
	      {
                s->val[OPT_BR_X].w = PAPER_LEGAL_W;
	        s->val[OPT_BR_Y].w = PAPER_LEGAL_H;
	      }
	    else if (strcmp (s->val[OPT_PAPER].s, "Letter") == 0)
	      {
                s->val[OPT_BR_X].w = PAPER_LETTER_W;
	        s->val[OPT_BR_Y].w = PAPER_LETTER_H;
	      }
	  }
	  return (SANE_STATUS_GOOD);
        }

    }
  }

  DBG (11, "<< sane_control_option\n");
  return (SANE_STATUS_INVAL);
}

SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  Ibm_Scanner *s = handle;
  DBG (11, ">> sane_get_parameters\n");

  if (!s->scanning)
    {
      int width, length, xres, yres;
      const char *mode;

      memset (&s->params, 0, sizeof (s->params));

      width = s->val[OPT_BR_X].w - s->val[OPT_TL_X].w;
      length = s->val[OPT_BR_Y].w - s->val[OPT_TL_Y].w;
      xres = s->val[OPT_X_RESOLUTION].w;
      yres = s->val[OPT_Y_RESOLUTION].w;

      /* make best-effort guess at what parameters will look like once
         scanning starts.  */
      if (xres > 0 && yres > 0 && width > 0 && length > 0)
        {
          s->params.pixels_per_line = width * xres / s->hw->info.mud;
          s->params.lines = length * yres / s->hw->info.mud;
        }

      mode = s->val[OPT_MODE].s;
      if ((strcmp (mode, SANE_VALUE_SCAN_MODE_LINEART) == 0) ||
	  (strcmp (mode, SANE_VALUE_SCAN_MODE_HALFTONE)) == 0)
        {
          s->params.format = SANE_FRAME_GRAY;
          s->params.bytes_per_line = s->params.pixels_per_line / 8;
	  /* the Ibm truncates to the byte boundary, so: chop! */
          s->params.pixels_per_line = s->params.bytes_per_line * 8;
          s->params.depth = 1;
        }
      else /* if (strcmp (mode, SANE_VALUE_SCAN_MODE_GRAY) == 0) */
        {
          s->params.format = SANE_FRAME_GRAY;
          s->params.bytes_per_line = s->params.pixels_per_line;
          s->params.depth = 8;
        }
      s->params.last_frame = SANE_TRUE;
    }
  else
    DBG (5, "sane_get_parameters: scanning, so can't get params\n");

  if (params)
    *params = s->params;

  DBG (1, "%d pixels per line, %d bytes, %d lines high, total %lu bytes, "
       "dpi=%d\n", s->params.pixels_per_line, s->params.bytes_per_line,
       s->params.lines, (u_long) s->bytes_to_read, s->val[OPT_Y_RESOLUTION].w);

  DBG (11, "<< sane_get_parameters\n");
  return (SANE_STATUS_GOOD);
}


SANE_Status
sane_start (SANE_Handle handle)
{
  char *mode_str;
  Ibm_Scanner *s = handle;
  SANE_Status status;
  struct ibm_window_data wbuf;
  struct measurements_units_page mup;

  DBG (11, ">> sane_start\n");

  /* First make sure we have a current parameter set.  Some of the
     parameters will be overwritten below, but that's OK.  */
  status = sane_get_parameters (s, 0);
  if (status != SANE_STATUS_GOOD)
    return status;

  status = sanei_scsi_open (s->hw->sane.name, &s->fd, 0, 0);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "open of %s failed: %s\n",
           s->hw->sane.name, sane_strstatus (status));
      return (status);
    }

  mode_str = s->val[OPT_MODE].s;
  s->xres = s->val[OPT_X_RESOLUTION].w;
  s->yres = s->val[OPT_Y_RESOLUTION].w;
  s->ulx = s->val[OPT_TL_X].w;
  s->uly = s->val[OPT_TL_Y].w;
  s->width = s->val[OPT_BR_X].w - s->val[OPT_TL_X].w;
  s->length = s->val[OPT_BR_Y].w - s->val[OPT_TL_Y].w;
  s->brightness = s->val[OPT_BRIGHTNESS].w;
  s->contrast = s->val[OPT_CONTRAST].w;
  s->bpp = s->params.depth;
  if (strcmp (mode_str, SANE_VALUE_SCAN_MODE_LINEART) == 0)
    {
      s->image_composition = IBM_BINARY_MONOCHROME;
    }
  else if (strcmp (mode_str, SANE_VALUE_SCAN_MODE_HALFTONE) == 0)
    {
      s->image_composition = IBM_DITHERED_MONOCHROME;
    }
  else if (strcmp (mode_str, SANE_VALUE_SCAN_MODE_GRAY) == 0)
    {
      s->image_composition = IBM_GRAYSCALE;
    }

  memset (&wbuf, 0, sizeof (wbuf));
/* next line commented out by mf */
/*  _lto2b(sizeof(wbuf) - 8, wbuf.len); */
/* next line by mf */
  _lto2b(IBM_WINDOW_DATA_SIZE, wbuf.len); /* size=320 */
  _lto2b(s->xres, wbuf.x_res);
  _lto2b(s->yres, wbuf.y_res);
  _lto4b(s->ulx, wbuf.x_org);
  _lto4b(s->uly, wbuf.y_org);
  _lto4b(s->width, wbuf.width);
  _lto4b(s->length, wbuf.length);

  wbuf.image_comp = s->image_composition;
  /* if you throw the MRIF bit the brightness control reverses too */
  /* so I reverse the reversal in software for symmetry's sake */
  if (wbuf.image_comp == IBM_GRAYSCALE || wbuf.image_comp == IBM_DITHERED_MONOCHROME)
    {
      if (wbuf.image_comp == IBM_GRAYSCALE)
	wbuf.mrif_filtering_gamma_id = (SANE_Byte) 0x80; /* it was 0x90 */
      if (wbuf.image_comp == IBM_DITHERED_MONOCHROME)
	wbuf.mrif_filtering_gamma_id = (SANE_Byte) 0x10;
      wbuf.brightness = 256 - (SANE_Byte) s->brightness;
/*
      if (is50)
        wbuf.contrast = (SANE_Byte) s->contrast;
      else
*/
      wbuf.contrast = 256 - (SANE_Byte) s->contrast;
    }
  else /* wbuf.image_comp == IBM_BINARY_MONOCHROME */
    {
      wbuf.mrif_filtering_gamma_id = (SANE_Byte) 0x00;
      wbuf.brightness = (SANE_Byte) s->brightness;
      wbuf.contrast = (SANE_Byte) s->contrast;
    }

  wbuf.threshold = 0;
  wbuf.bits_per_pixel = s->bpp;

  wbuf.halftone_code = 2;     /* diithering */
  wbuf.halftone_id = 0x0A;    /* 8x8 Bayer pattenr */
  wbuf.pad_type = 3;
  wbuf.bit_ordering[0] = 0;
  wbuf.bit_ordering[1] = 7;   /* modified by mf (it was 3) */

  DBG (5, "xres=%d\n", _2btol(wbuf.x_res));
  DBG (5, "yres=%d\n", _2btol(wbuf.y_res));
  DBG (5, "ulx=%d\n", _4btol(wbuf.x_org));
  DBG (5, "uly=%d\n", _4btol(wbuf.y_org));
  DBG (5, "width=%d\n", _4btol(wbuf.width));
  DBG (5, "length=%d\n", _4btol(wbuf.length));
  DBG (5, "image_comp=%d\n", wbuf.image_comp);

  DBG (11, "sane_start: sending SET WINDOW\n");
  status = set_window (s->fd, &wbuf);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "SET WINDOW failed: %s\n", sane_strstatus (status));
      return (status);
    }

  DBG (11, "sane_start: sending GET WINDOW\n");
  memset (&wbuf, 0, sizeof (wbuf));
  status = get_window (s->fd, &wbuf);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "GET WINDOW failed: %s\n", sane_strstatus (status));
      return (status);
    }
  DBG (5, "xres=%d\n", _2btol(wbuf.x_res));
  DBG (5, "yres=%d\n", _2btol(wbuf.y_res));
  DBG (5, "ulx=%d\n", _4btol(wbuf.x_org));
  DBG (5, "uly=%d\n", _4btol(wbuf.y_org));
  DBG (5, "width=%d\n", _4btol(wbuf.width));
  DBG (5, "length=%d\n", _4btol(wbuf.length));
  DBG (5, "image_comp=%d\n", wbuf.image_comp);

  DBG (11, "sane_start: sending MODE SELECT\n");
  memset (&mup, 0, sizeof (mup));
  mup.page_code = MEASUREMENTS_PAGE;
  mup.parameter_length = 0x06;
  mup.bmu = INCHES;
  mup.mud[0] = (DEFAULT_MUD >> 8) & 0xff;
  mup.mud[1] = (DEFAULT_MUD & 0xff);
/* next lines by mf */
  mup.adf_page_code = 0x26;
  mup.adf_parameter_length = 6;
  if (s->adf_state == ADF_ARMED)
    mup.adf_control = 1;
  else
    mup.adf_control = 0;
/* end lines by mf */

  status = mode_select (s->fd, (struct mode_pages *) &mup);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "attach: MODE_SELECT failed\n");
      return (SANE_STATUS_INVAL);
    }

  status = trigger_scan (s->fd);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "start of scan failed: %s\n", sane_strstatus (status));
      /* next line introduced not to freeze xscanimage */
      do_cancel(s);
      return status;
    }

  /* Wait for scanner to become ready to transmit data */
  status = ibm_wait_ready (s);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "GET DATA STATUS failed: %s\n", sane_strstatus (status));
      return (status);
    }

  s->bytes_to_read = s->params.bytes_per_line * s->params.lines;

  DBG (1, "%d pixels per line, %d bytes, %d lines high, total %lu bytes, "
       "dpi=%d\n", s->params.pixels_per_line, s->params.bytes_per_line,
       s->params.lines, (u_long) s->bytes_to_read, s->val[OPT_Y_RESOLUTION].w);

  s->scanning = SANE_TRUE;

  DBG (11, "<< sane_start\n");
  return (SANE_STATUS_GOOD);
}

SANE_Status
sane_read (SANE_Handle handle, SANE_Byte * buf, SANE_Int max_len,
           SANE_Int * len)
{
  Ibm_Scanner *s = handle;
  SANE_Status status;
  size_t nread;
  DBG (11, ">> sane_read\n");

  *len = 0;

  DBG (11, "sane_read: bytes left to read: %ld\n", (u_long) s->bytes_to_read);

  if (s->bytes_to_read == 0)
    {
      do_cancel (s);
      return (SANE_STATUS_EOF);
    }

  if (!s->scanning) {
    DBG (11, "sane_read: scanning is false!\n");
    return (do_cancel (s));
  }

  nread = max_len;
  if (nread > s->bytes_to_read)
    nread = s->bytes_to_read;

  DBG (11, "sane_read: read %ld bytes\n", (u_long) nread);
  status = read_data (s->fd, buf, &nread);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (11, "sane_read: read error\n");
      do_cancel (s);
      return (SANE_STATUS_IO_ERROR);
    }
  *len = nread;
  s->bytes_to_read -= nread;

  DBG (11, "<< sane_read\n");
  return (SANE_STATUS_GOOD);
}

void
sane_cancel (SANE_Handle handle)
{
  Ibm_Scanner *s = handle;
  DBG (11, ">> sane_cancel\n");

  s->scanning = SANE_FALSE;

  DBG (11, "<< sane_cancel\n");
}

SANE_Status
sane_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking)
{
  DBG (5, ">> sane_set_io_mode (handle = %p, non_blocking = %d)\n",
       handle, non_blocking);
  DBG (5, "<< sane_set_io_mode\n");

  return SANE_STATUS_UNSUPPORTED;
}

SANE_Status
sane_get_select_fd (SANE_Handle handle, SANE_Int * fd)
{
  DBG (5, ">> sane_get_select_fd (handle = %p, fd = %p)\n",
       handle, (void *) fd);
  DBG (5, "<< sane_get_select_fd\n");

  return SANE_STATUS_UNSUPPORTED;
}
