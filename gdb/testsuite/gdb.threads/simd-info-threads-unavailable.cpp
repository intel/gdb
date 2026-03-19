/* This testcase is part of GDB, the GNU debugger.

   Copyright 2025-2026 Free Software Foundation, Inc.

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

constexpr size_t DIM0 = 128;

int
main (int argc, char *argv[])
{
  int in[DIM0];
  int out[DIM0];

  /* Initialize the input.  */
  for (unsigned int i = 0; i < DIM0; i++)
    in[i] = i + 123;

  sycl::queue device_queue {get_sycl_queue (argc, argv)};

  /* Submit a toy kernel to trigger auto-attach.  */
  device_queue.single_task ([] () {}).wait_and_throw ();

  sycl::range<1> data_range {DIM0}; /* before-submission */
  sycl::buffer<int, 1> b_in {&in[0], data_range};
  sycl::buffer<int, 1> b_out {&out[0], data_range};

  device_queue.submit ([&] (sycl::handler& cgh)
    {
      auto a_in = b_in.get_access<sycl::access::mode::read> (cgh);
      auto a_out = b_out.get_access<sycl::access::mode::write> (cgh);

      cgh.parallel_for<class kernel> (data_range, [=] (sycl::id<1> wiID)
	{
	  int in_elem = a_in[wiID];
	  a_out[wiID] = in_elem * 2;
	});
    });

  device_queue.wait_and_throw ();

  return 0;
}
