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

int
main (int argc, char *argv[])
{
  sycl::queue queue = get_sycl_queue (argc, argv);

  queue.submit ([&] (sycl::handler &cgh)
    {
      sycl::local_accessor<short> local_mem (1, cgh);

      cgh.parallel_for_work_group (sycl::range<1> (8),
				   sycl::range<1> (32),
				   [=] (sycl::group<1> wg)
	{
	  sycl::decorated_local_ptr<short> mptr
	    = local_mem.get_multi_ptr<sycl::access::decorated::yes> ();
	  mptr[0] = 42;

	  wg.parallel_for_work_item ([&] (sycl::h_item<1> wi)
	    {
	      auto lptr = mptr.get ();
	      short *gptr = lptr;

	      int x = 123; /* break-here.  */
	    });
	});
    });

  queue.wait ();

  return 0;
}
