/* This testcase is part of GDB, the GNU debugger.

   Copyright 2022-2024 Free Software Foundation, Inc.

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

#ifndef DATA_SIZE
#define DATA_SIZE 64
#endif
#ifndef SUB_GROUP_SIZE
#define SUB_GROUP_SIZE 16
#endif

int
main (int argc, char *argv[])
{
  int data[DATA_SIZE];

  /* Initialize the input.  */
  for (unsigned int i = 0; i < DATA_SIZE; i++)
    data[i] = 0;

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::range<1> dataRange {DATA_SIZE};
    sycl::buffer<int, 1> buffer {&data[0], dataRange};

    deviceQueue.submit ([&] (sycl::handler& cgh) /* line-before-kernel */
      {
	auto accessor = buffer.get_access<sycl::access::mode::write> (cgh);

	cgh.parallel_for<class kernel>(
		dataRange, [=] (sycl::id<1> wiID)
				[[sycl::reqd_sub_group_size (SUB_GROUP_SIZE)]]
	  {
	    /* The counter ensures this does not run infinitely.
	       The boolean flag is set from inside the debugger to
	       stop spinning.  We do not use the counter for that
	       purpose, because there can be a write-after-write
	       race between the debugger and the program itself.  */
	    long long count = 1e8;
	    bool spin = true;
	    while (count > 0 && spin) count--;  /* spinning-line */
	    accessor[wiID] = 100; /* kernel-last-line */
	  });
      });
  }

  /* Verify the output.  */
  for (unsigned int i = 0; i < DATA_SIZE; i++)
    if (data[i] != 100)
      {
	std::cout << "Element " << i << " is " << data[i] << std::endl;
	return 1;
      }

  return 0;
}
