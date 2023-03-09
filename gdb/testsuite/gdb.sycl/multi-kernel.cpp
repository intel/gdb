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
#include <iostream>
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  int data[3] = {7, 8, 9};

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::buffer<int, 1> buf {data, sycl::range<1> {3}};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto numbers_w = buf.get_access<sycl::access::mode::write> (cgh);

	cgh.single_task<class kernel_1> ([=] ()
	  {
	    numbers_w[1] = 32;
	    numbers_w[2] = 10; /* kernel-1-line */
	  });
      });

    deviceQueue.wait ();
    data[0] += 5; /* in-between-kernels */

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto numbers_rw = buf.get_access<sycl::access::mode::read_write> (cgh);

	cgh.single_task<class kernel_2> ([=] ()
	  {
	    int num1 = numbers_rw[1];
	    int num2 = numbers_rw[2];
	    numbers_rw[0] = num1 + num2; /* kernel-2-line */
	  });
      });
  }

  std::cout << "Result is " << data[0] << std::endl;  /* line-after-kernel */

  return 0;
}
