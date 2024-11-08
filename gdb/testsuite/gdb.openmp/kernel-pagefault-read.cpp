/* This testcase is part of GDB, the GNU debugger.

   Copyright 2023-2024 Free Software Foundation, Inc.
   Copyright (C) 2023-2024 Intel Corporation

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

#include <omp.h>

int
main ()
{
  int data[1] {7};

#pragma omp target teams num_teams(1) thread_limit(1) map(tofrom: data)
  {
    int *src = nullptr;  /* line-before-pagefault */
    data[0] = src[0];
  }

  return 0;
}
