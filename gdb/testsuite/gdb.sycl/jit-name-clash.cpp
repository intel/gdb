/* This testcase is part of GDB, the GNU debugger.

   Copyright 2026 Free Software Foundation, Inc.

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

struct S
{
  int get ()
  {
#ifdef __SYCL_DEVICE_ONLY__
    return 42;
#else
    return 11;
#endif
  }
};

int
main (int argc, char *argv[])
{
  sycl::queue deviceQueue {get_sycl_queue (argc, argv)};

  S s_out;
  int outside = s_out.get ();

  deviceQueue.submit ([&] (sycl::handler& cgh)  /* line-before-kernel */
    {
      cgh.single_task<class simple_kernel> ([=] ()
	{
	  S s_in;
	  int inside = s_in.get ();
	  int dummy = inside; /* kernel-line */
	});
    });

  deviceQueue.wait_and_throw ();

  return 0;
}
