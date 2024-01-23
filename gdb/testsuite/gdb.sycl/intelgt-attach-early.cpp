/* This testcase is part of GDB, the GNU debugger.

   Copyright 2023-2024 Free Software Foundation, Inc.

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
#include <unistd.h>
#include "../lib/sycl-util.cpp"

volatile bool spin = true;

int
main (int argc, char *argv[])
{
  int data[1] = {7};

  /* The counter ensures this does not run infinitely.
     The boolean flag is set from inside the debugger to
     stop spinning.  We do not use the counter for that
     purpose, because there can be a write-after-write
     race between the debugger and the program itself.  */
  int count = 60;
  while (count > 0 && spin)
    {
      sleep (1);
      count--;
    }


  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::buffer<int, 1> buf {data, sycl::range<1> {1}};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto numbers = buf.get_access<sycl::access::mode::write> (cgh);

	cgh.single_task<> ([=] ()
	  {
	    numbers[0] = 0; /* kernel-line */
	  });
      });
  }

  return 0;
}
