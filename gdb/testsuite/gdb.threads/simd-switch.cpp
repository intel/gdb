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

  int in[DIM0];
  int out[DIM0];

  /* Initialize the input.  */
  for (unsigned int i = 0; i < DIM0; i++)
    in[i] = i + 123;

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    cl::sycl::range<1> dataRange {DIM0};
    cl::sycl::buffer<int, 1> bufferIn {&in[0], dataRange};

    deviceQueue.submit ([&] (cl::sycl::handler& cgh)
      {
	auto accessorIn
	  = bufferIn.get_access<cl::sycl::access::mode::read> (cgh);

	cgh.parallel_for (dataRange, [=] (cl::sycl::id<1> wiID)
	  {
	    int dim0 = wiID[0]; /* kernel-first-line */
	    dim0 += 0; /* kernel-second-line */

	    if (dim0 % 2 == 0) /* kernel-condition-line-1 */
	      dim0 += 200; /* kernel-even-branch */

	    if (dim0 % 2 == 1) /* kernel-condition-line-2 */
	      dim0 += 300; /* kernel-odd-branch */
	  });
      });
  }

  return 0;
}
