/* This testcase is part of GDB, the GNU debugger.

   Copyright 2019-2021 Free Software Foundation, Inc.

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

#include <iostream>
#include "omp.h"

int
main (int argc, char *argv[])
{
  constexpr unsigned int length = 4;
  int in_arr[length];

  /* Initialize the input.  */
  for (unsigned int i = 0; i < length; i++)
    in_arr[i] = i;

  /* Spawn kernels that are independent of each other.  */
  for (unsigned int i = 0; i < length; i++)
    #pragma omp target teams num_teams(1) thread_limit(1) map(to: in_arr)
      {
	int item = in_arr[i] + 100; /* kernel-line */
      }

#pragma omp single
    in_arr[0] = 1; /* line-after-kernel */
  return 0;
}
