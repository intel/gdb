/* This testcase is part of GDB, the GNU debugger.

   Copyright 2020-2021 Free Software Foundation, Inc.

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
  int condition_value = 37;

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
	    int dim0 = wiID[0]; /* kernel-first-line */
	    int in_elem = accessorIn[wiID];
	    if (dim0 == condition_value)
	      in_elem = in_elem + 2000; /* kernel-condition */
	    accessorOut[wiID] = in_elem + 100; /* kernel-last-line */
	  });
      });
  }

  /* Verify the output.  */
  for (unsigned int i = 0; i < DIM0; i++)
    if ((out[i] != in[i] + 100 + ((i == condition_value) ? 2000 : 0)))
      {
	std::cout << "Element " << i << " is " << out[i] << std::endl;
	return 1;
      }

  std::cout << "Correct" << std::endl; /* end-marker */
  return 0;
}
