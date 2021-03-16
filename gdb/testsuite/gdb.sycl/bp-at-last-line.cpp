/* This testcase is part of GDB, the GNU debugger.

   Copyright 2021 Free Software Foundation, Inc.

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
#include "../lib/sycl-util.cpp"

void
foo (void)
{
  int bar = 1;  /* foo-first-line */
  bar += 1;
}   /* foo-last-line */

int
main (int argc, char *argv[])
{
  cl::sycl::queue queue { get_sycl_queue (argc, argv) };
  queue.submit ([&] (cl::sycl::handler& cgh)
    {
      cgh.single_task<class simple_kernel_2> ([=] ()
	{
	  foo ();
	});
    });
  queue.wait ();

  return 0;
}
