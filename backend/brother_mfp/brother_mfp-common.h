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

#define BACKEND_NAME        brother_mfp
#define BROTHER_MFP_CONFIG_FILE "brother_mfp.conf"

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

#define MIN(x,y)        ((x)>(y)?(y):(x))
