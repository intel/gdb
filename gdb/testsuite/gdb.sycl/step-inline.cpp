/* This testcase is part of GDB, the GNU debugger.

   Copyright 2020 Free Software Foundation, Inc.

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

__attribute__((always_inline))
static int
get_dim (cl::sycl::id<1> wi, int index)
{
  return wi[index];
}

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 1024;

  int in[DIM0];
  int out[DIM0];

  /* Initialize the input.  */
  for (unsigned int i = 0; i < DIM0; i++)
    in[i] = i + 123;

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    cl::sycl::range<1> dataRange {DIM0};
    cl::sycl::buffer<int, 1> bufferIn {&in[0], dataRange};
    cl::sycl::buffer<int, 1> bufferOut {&out[0], dataRange};

    deviceQueue.submit ([&] (cl::sycl::handler& cgh)
      {
	auto accessorIn
	  = bufferIn.get_access<cl::sycl::access::mode::read> (cgh);
	auto accessorOut
	  = bufferOut.get_access<cl::sycl::access::mode::write> (cgh);

	cgh.parallel_for<class kernel> (dataRange, [=] (cl::sycl::id<1> wiID)
	  {
	    int in_elem = accessorIn[wiID[0]]; /* kernel-first-line */
	    accessorOut[wiID] = in_elem + 100; /* breakpoint-here */
	    int dim0 = get_dim (wiID, 0); /* before-branch */
	    if (dim0 % 2 == 0) /* condition */
	      accessorOut[wiID] += get_dim (wiID, 0) * 0; /* then-branch */
	    else
	      accessorOut[wiID] -= get_dim (wiID, 0) * 0; /* else-branch */
	  });
      });
  }

  /* Verify the output.  */
  for (unsigned int i = 0; i < DIM0; i++)
    if (out[i] != in[i] + 100)
      {
	std::cout << "Element " << i << " is " << out[i] << std::endl;
	return 1;
      }

  std::cout << "Correct" << std::endl; /* end-marker */
  return 0;
}
