/* This testcase is part of GDB, the GNU debugger.

   Copyright 2019-2020 Free Software Foundation, Inc.

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

#include <sycl/sycl.hpp>
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 3;
  constexpr size_t DIM1 = 2;
  constexpr size_t DIM2 = 2;

  int data_1D[DIM0];
  int data_2D[DIM0][DIM1];
  int data_3D[DIM0][DIM1][DIM2];

  /* Initialize the input.  */
  int val = 10;
  for (unsigned int i = 0; i < DIM0; i++)
    {
      data_1D[i] = 11 + i;
      for (unsigned int j = 0; j < DIM1; j++)
	{
	  data_2D[i][j] = 21 + (i * DIM1) + j;
	  for (unsigned int k = 0; k < DIM2; k++)
	    data_3D[i][j][k] = 30;
	}
    }

  sycl::id<1> id_1D {11};
  sycl::id<2> id_2D {11, 22};
  sycl::id<3> id_3D {11, 22, 33};

  sycl::range<1> range_1D {DIM0};
  sycl::range<2> range_2D {DIM0, DIM1};
  sycl::range<3> range_3D {DIM0, DIM1, DIM2};

  sycl::buffer<int, 1> buffer_1D {&data_1D[0], range_1D};
  sycl::buffer<int, 2> buffer_2D {&data_2D[0][0], range_2D};
  sycl::buffer<int, 3> buffer_3D {&data_3D[0][0][0], range_3D};

  return 0; /* end-of-program */
}
