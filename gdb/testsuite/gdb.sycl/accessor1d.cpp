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

#include "../lib/sycl-util.cpp"
#include <CL/sycl.hpp>

int
main (int argc, char *argv[])
{
  int data[3] = { 1, 2, 3 };

  {
    cl::sycl::queue queue = { get_sycl_queue (argc, argv) };
    cl::sycl::buffer<int, 1> buffer = { data, cl::sycl::range<1> { 3 } };
    queue.submit ([&] (cl::sycl::handler &cgh)
      {
	auto input = buffer.get_access<cl::sycl::access::mode::read> (cgh);
	cgh.single_task<class simple_kernel> ([=] ()
	  {
	    int one = input[0];
	    auto id = cl::sycl::id<1> (1);
	    int two = input[id];
	    int dummy = one + two; /* kernel-line */
	  });
      });
  }
  return 0;
}
