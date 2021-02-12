/* Copyright (C) 2020-2021 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Test program for DW_AT_INTEL_simd_width and
   DW_OP_INTEL_push_simd_lane.  */

#include "amd64-simd.h"


#define N 1024

struct ts tsa[N];

int
main (void)
{
  int i, result;

  for (i = 0; i < N; ++i)
    {
      tsa[i].a = 100 + i;
      tsa[i].b = 7;
    }

  result = test (tsa, 5);
  if (result < 0)
    result = -1;

  return result;
}
