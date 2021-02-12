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
  constexpr size_t DIM0 = 32;
  constexpr size_t DIM1 = 16;
  constexpr size_t DIM2 = 8;

  int in[DIM0][DIM1][DIM2];
  int out[DIM0][DIM1][DIM2];

  /* Initialize the input.  */
  int val = 1;
  for (unsigned int i = 0; i < DIM0; i++)
    for (unsigned int j = 0; j < DIM1; j++)
      for (unsigned int k = 0; k < DIM2; k++)
	in[i][j][k] = val++;

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    cl::sycl::range<3> dataRange {DIM0, DIM1, DIM2};
    cl::sycl::buffer<int, 3> bufferIn {&in[0][0][0], dataRange};
    cl::sycl::buffer<int, 3> bufferOut {&out[0][0][0], dataRange};

    deviceQueue.submit ([&] (cl::sycl::handler& cgh)
      {
	auto accessorIn
	  = bufferIn.get_access<cl::sycl::access::mode::read> (cgh);
	auto accessorOut
	  = bufferOut.get_access<cl::sycl::access::mode::write> (cgh);

	cl::sycl::nd_range<3> kernel_range (dataRange,
					    cl::sycl::range<3> (4, 4, 4));
	cgh.parallel_for<class kernel> (kernel_range,
					[=] (cl::sycl::nd_item<3> item)
	  {
	    cl::sycl::id<3> gid = item.get_global_id (); /* kernel-first-line */
	    size_t gid0 = item.get_global_id (0);
	    size_t gid1 = item.get_global_id (1);
	    size_t gid2 = item.get_global_id (2);
	    int in_elem = accessorIn[gid];
	    accessorOut[gid] = in_elem; /* kernel-last-line */
	  });
      });
  }

  /* Verify the output.  */
  for (unsigned int i = 0; i < DIM0; i++)
    for (unsigned int j = 0; j < DIM1; j++)
      for (unsigned int k = 0; k < DIM2; k++)
	if (in[i][j][k] != out[i][j][k])
	  {
	    std::cout << "Element " << i << "," << j << ", "
		      << k << " is " << out[i][j][k] << std::endl;
	    return 1;
	  }

  std::cout << "Correct" << std::endl;
  return 0;
}
