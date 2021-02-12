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
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  constexpr unsigned long data_size = 8;
  int out[data_size];

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    cl::sycl::range<1> dataRange {data_size};
    cl::sycl::buffer<int, 1> bufferOut {&out[0], dataRange};

    deviceQueue.submit ([&] (cl::sycl::handler& cgh)
    {
      auto accessorOut
	= bufferOut.get_access<cl::sycl::access::mode::write> (cgh);

      cgh.parallel_for<class kernel> (dataRange, [=] (cl::sycl::id<1> wiID)
      {
	int dim0 = wiID[0];          /* kernel-line-1 */
	if (dim0 % 2 == 0)
	  accessorOut[wiID] = dim0;  /* then-branch */
	else
	  accessorOut[wiID] = -dim0; /* else-branch */
      });
    });
  }

  return 0;
}
