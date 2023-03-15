/* This testcase is part of GDB, the GNU debugger.

   Copyright 2022 Free Software Foundation, Inc.

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
  int data[3] = {7, 8, 9};

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::buffer<int, 1> buf {data, sycl::range<1> {3}};

    deviceQueue.submit ([&] (sycl::handler& cgh)  /* line-before-kernel */
      {
	auto numbers = buf.get_access<sycl::access::mode::read_write> (cgh);

	cgh.single_task ([=] ()
	  {
	    int result = 0;
	    int c;
	    for (int i = 0; i < 3; i++)
	      {
	        int b = i + 100;
	        c = i + 10;
	        result += b + c; /* kernel-last-loop-line */
	      }
	    numbers[0] = result;
	  });
      });
  }

  std::cout << "Result is " << data[0] << std::endl;  /* line-after-kernel */

  return data[0] == 336 ? 0 : 1; /* return-stmt */
}
