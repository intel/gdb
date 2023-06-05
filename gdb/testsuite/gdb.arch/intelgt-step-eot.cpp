/* This testcase is part of GDB, the GNU debugger.

   Copyright 2022-2023 Free Software Foundation, Inc.

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
#include <iostream>
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  int data[1] = {0};

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::buffer<int, 1> buf {data, sycl::range<1> {1}};
    sycl::range<1> range {2u};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto acc
	  = buf.get_access<sycl::access::mode::read_write> (cgh);

	cgh.parallel_for (range, [=] (sycl::id<1> id)
	  {
	    acc[0] += 1; /* in-kernel */
	  });
      });
  }

  return 0; /* return-stmt */
}
