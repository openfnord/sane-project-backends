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

#pragma once

#define BACKEND_NAME            brother_mfp
#define BROTHER_MFP_CONFIG_FILE "brother_mfp.conf"

/*
 * Model capabilities.
 *
 * TODO: Some of these capabilities might be redundant if all models possess them.
 * E.g. MODE_GRAY
 *
 */
#define CAP_MODE_COLOUR                 (1u << 0)
#define CAP_MODE_GRAY                   (1u << 1)
#define CAP_MODE_GRAY_DITHER            (1u << 2)
#define CAP_MODE_BW                     (1u << 3)

#define CAP_MODE_BUTTON_SCAN_EMAIL      (1u << 4)
#define CAP_MODE_BUTTON_SCAN_OCR        (1u << 5)
#define CAP_MODE_BUTTON_SCAN_FILE       (1u << 6)
#define CAP_MODE_BUTTON_SCAN_IMAGE      (1u << 7)

#define CAP_MODE_HAS_ADF                (1u << 8)
#define CAP_MODE_HAS_ADF_IS_DUPLEX      (1u << 9)

#define CAP_MODE_HAS_RAW                (1u << 10)
#define CAP_MODE_HAS_JPEG               (1u << 11)

// Oddities of particular models.
#define CAP_MODE_RAW_IS_CrYCb           (1u << 12)

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
