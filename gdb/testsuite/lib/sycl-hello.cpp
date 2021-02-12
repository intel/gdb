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
#include "sycl-util.cpp"

static int numbers[8];

int
main (int argc, char *argv[])
{
  cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
  cl::sycl::range<1> length {8};

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::buffer<int, 1> buf {numbers, length};
    deviceQueue.submit ([&] (cl::sycl::handler& cgh)
      {
	auto accessor
	  = buf.get_access<cl::sycl::access::mode::read_write> (cgh);

	cgh.parallel_for<class SyclHello> (length, [=] (cl::sycl::id<1> wiID)
	  {
	    accessor[wiID] = wiID[0] + 1;
	  });
      });
  }

  return 0;
}
