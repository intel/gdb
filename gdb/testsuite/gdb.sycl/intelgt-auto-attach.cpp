/* This testcase is part of GDB, the GNU debugger.

   Copyright 2019-2026 Free Software Foundation, Inc.
   Copyright (C) 2020-2026 Intel Corporation.

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

  sycl::queue deviceQueue {get_sycl_queue (argc, argv)};

  /* Submit a workload to trigger auto-attach.  */
  deviceQueue.single_task ([=] () {}).wait_and_throw (); /* toy-kernel */

  sycl::buffer<int, 1> buf {data, sycl::range<1> {3}};

  deviceQueue.submit ([&] (sycl::handler& cgh)  /* line-before-kernel */
    {
      auto numbers = buf.get_access<sycl::access::mode::read_write> (cgh);

      cgh.single_task<class simple_kernel> ([=] ()
	{
	  int x = numbers[1]; /* kernel-line-1 */
	  int y = x + 1;
	  int z = numbers[2]; /* kernel-line-3 */
	  numbers[0] = x + y + z;
	});
    });

  deviceQueue.wait_and_throw ();

  return 0; /* return-stmt */
}
