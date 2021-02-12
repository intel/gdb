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
  int data[3] = {7, 8, 9};

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    cl::sycl::buffer<int, 1> buf {data, cl::sycl::range<1> {3}};

    deviceQueue.submit ([&] (cl::sycl::handler& cgh)  /* line-before-kernel */
      {
	auto numbers
	  = buf.get_access<cl::sycl::access::mode::read_write> (cgh);

	cgh.single_task<class simple_kernel> ([=] ()
	  {
	    int ten = numbers[1] + 2;  /* kernel-line-1 */
	    int four = numbers[2] - 5; /* kernel-line-2 */
	    int fourteen = ten + four; /* kernel-line-3 */
	    numbers[0] = fourteen * 3; /* kernel-line-4 */
	  });
      });
  }

#ifndef OMIT_REPORT
  std::cout << "Result is " << data[0] << std::endl;  /* line-after-kernel */
#endif

  return 0; /* return-stmt */
}
