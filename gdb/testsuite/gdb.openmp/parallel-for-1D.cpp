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

#include <omp.h>
#include <iostream>

#pragma omp declare target
static int
update_val (int val, int offset)
{
  return val + offset;
}
#pragma omp end declare target

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 1024;

  int in_arr[DIM0], out_arr[DIM0];
  unsigned int i;

  /* Initialize the input. */
  for (i = 0; i < DIM0; i++)
    in_arr[i] = i + 123;

#pragma omp target map(to: in_arr) map(from: out_arr)
  {
    #pragma omp teams distribute parallel for
    for (i = 0; i < DIM0; i++) {
	int in_elem = update_val (in_arr[i], 100); /* kernel-first-line */
	int in_elem2 = in_arr[i] + 200; /* kernel-second-line */
	int in_elem3 = in_elem + 300;
	out_arr[i] = in_elem; /* kernel-last-line */
      }
  }

  /* Verify the output.  */
  for (i = 0; i < DIM0; i++)
    if (out_arr[i] != in_arr[i] + 100)
      {
	std::cerr << "Element " << i << " is " << out_arr[i]
		  << " but expected is " <<  (in_arr[i] + 100)  << std::endl;
	return 1;
      }

  std::cout << "Correct" << std::endl; /* end-marker */
  return 0;
}
