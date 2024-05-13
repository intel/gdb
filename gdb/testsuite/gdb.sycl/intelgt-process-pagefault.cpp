/* This testcase is part of GDB, the GNU debugger.

   Copyright 2024 Free Software Foundation, Inc.

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
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  size_t data[1] {7};

  sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
  sycl::buffer<size_t, 1> buf {data, sycl::range<1> {1}};

  deviceQueue.submit ([&] (sycl::handler& cgh)
    {
      auto numbers = buf.get_access<sycl::access::mode::write> (cgh);

      /* One thread goes to the "else" branch where it would cause a
	 page fault, whereas the other thread goes to the "then"
	 branch, keeping the kernel alive.  The goal is to trigger a
	 scenario where the page-faulting thread terminates
	 immediately after accessing a bad address.  For this reason,
	 the debug API cannot associate the pagefault with a
	 particular thread; it emits a generic process pagefault event
	 instead.  */

      sycl::nd_range<1> range (sycl::range<1> {2}, sycl::range<1> {1});
      cgh.parallel_for (range, [=] (sycl::nd_item<1> index)
	{
	  sycl::id<1> gid = index.get_global_id ();
	  if (gid[0] == 0)
	    {
	      /* Spin to keep the kernel alive.  */
	      size_t count = 1e8;
	      while (count > 0)
		{
		  numbers[0] = count;
		  count--;
		}
	    }
	  else
	    {
	      /* Trigger a write-pagefault but immediately exit
		 without waiting for the result.  */
	      size_t *p = nullptr;
	      *p = gid[0];
	    }
	});
    });

  deviceQueue.wait_and_throw ();
  return 0;
}
