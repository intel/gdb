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

#include <CL/sycl.hpp>
#include <iostream>
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 128;
  constexpr size_t DIM1 = 64;

  int in[DIM0][DIM1];
  int out[DIM1][DIM0]; /* will transpose the input.  */

  /* Initialize the input.  */
  int val = 123;
  for (unsigned int i = 0; i < DIM0; i++)
    for (unsigned int j = 0; j < DIM1; j++)
      in[i][j] = val++;

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    cl::sycl::range<2> dataRangeIn {DIM0, DIM1};
    cl::sycl::range<2> dataRangeOut {DIM1, DIM0};
    cl::sycl::buffer<int, 2> bufferIn {&in[0][0], dataRangeIn};
    cl::sycl::buffer<int, 2> bufferOut {&out[0][0], dataRangeOut};

    deviceQueue.submit ([&] (cl::sycl::handler& cgh)
      {
	auto accessorIn
	  = bufferIn.get_access<cl::sycl::access::mode::read> (cgh);
	auto accessorOut
	  = bufferOut.get_access<cl::sycl::access::mode::write> (cgh);

	cgh.parallel_for<class kernel> (dataRangeIn, [=] (cl::sycl::id<2> wiID)
	  {
	    int dim0 = wiID[0]; /* kernel-first-line */
	    int dim1 = wiID[1];
	    int in_elem = accessorIn[wiID];
	    /* Negate the value, write into the transpositional location.  */
	    accessorOut[dim1][dim0] = -1 * in_elem; /* kernel-last-line */
	  });
      });
  }

  /* Verify the output.  */
  for (unsigned int i = 0; i < DIM0; i++)
    for (unsigned int j = 0; j < DIM1; j++)
      if (in[i][j] != -out[j][i])
	{
	  std::cout << "Element " << j << "," << i
		    << " is " << out[j][i] << std::endl;
	  return 1;
	}

  std::cout << "Correct" << std::endl;
  return 0;
}
