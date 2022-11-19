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

#define DEBUG_NOT_STATIC

#include "../../include/sane/sanei_debug.h"
#include "../genesys/minigtest.h"
#include "brother_mfp_tests.h"

int main ()
{
  DBG_INIT ();

  test_family4 ();
  test_family3 ();
  test_family2 ();
  test_gray_rlength ();
  test_colour_int_rgb();
  test_colour_jpeg();

  return finish_tests ();
}
