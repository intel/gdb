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
#include <omp.h>

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 128;
  constexpr size_t DIM1 = 64;

  int in_arr[DIM0][DIM1];
  int out_arr[DIM1][DIM0]; /* will transpose the input.  */
  unsigned int i, j;

  /* Initialize the input.  */
  int val = 123;
  for (i = 0; i < DIM0; i++)
    for (j = 0; j < DIM1; j++)
      in_arr[i][j] = val++;

#pragma omp target data map(to : in_arr) map(from : out_arr)
#pragma omp target teams num_teams(DIM0) thread_limit(DIM1)
  {
#pragma omp distribute parallel for
    for (i = 0; i < DIM0; i++)
      for (j = 0; j < DIM1; j++)
	{
	  int in_elem = in_arr[i][j]; /* kernel-first-line */
	  int in_elem2 = i;
	  int in_elem3 = j;
	  /* Negate the value, write into the transpositional location.  */
	  out_arr[j][i] = -1 * in_elem; /* kernel-last-line */
	}
  }

  /* Verify the output.  */
  for (i = 0; i < DIM0; i++)
    for (j = 0; j < DIM1; j++)
      if (in_arr[i][j] != -out_arr[j][i])
	{
	  std::cout << "Element " << j << "," << i << " is " << out_arr[j][i]
		    << std::endl;
	  return 1;
	}

  std::cout << "Correct" << std::endl;
  return 0;
}
