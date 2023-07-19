/* This testcase is part of GDB, the GNU debugger.

   Copyright 2023 Free Software Foundation, Inc.

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

/* This is an OpenMP program with a nested 'distribute' kernel where
   there is no stack-based function call inside the kernel.  */

#include <omp.h>
#include <iostream>

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 60;
  constexpr size_t DIM1 = 100;

  int in_arr[DIM0 * DIM1], out_arr[DIM0 * DIM1];

  /* Initialize the input.  */
  for (int i = 0; i < DIM0 * DIM1; i++)
    in_arr[i] = i + 123;

  #pragma omp parallel for
  for (int i = 0; i < DIM0; i++)
    {
      #pragma omp target teams distribute parallel for map(to: in_arr) map(from: out_arr)
      for (int j = 0; j < DIM1; j++) /* second-for-header */
	{
	  const int idx = i * DIM1 + j;
	  int element = in_arr[idx];
	  int result = element + 100;
	  out_arr[idx] = result;         /* kernel-last-line */
	}
    }

  /* Verify the output.  */
  for (int i = 0; i < DIM0 * DIM1; i++)
    if (out_arr[i] != in_arr[i] + 100)
      {
	std::cerr << "Element " << i << " is " << out_arr[i]
		  << " but expected is " <<  (in_arr[i] + 100)  << std::endl;
	return 1;
      }

  std::cout << "Correct" << std::endl;
  return 0;
}
