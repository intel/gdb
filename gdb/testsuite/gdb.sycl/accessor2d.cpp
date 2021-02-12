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
  float data[2][2] = { { 1, 2 }, { 3, 4 } };

  {
    cl::sycl::queue queue = { get_sycl_queue (argc, argv) };
    cl::sycl::buffer<float, 2> buffer
	= { &data[0][0], cl::sycl::range<2> { 2, 2 } };
    queue.submit ([&] (cl::sycl::handler &cgh)
      {
	auto input
	  = buffer.get_access<cl::sycl::access::mode::read_write> (cgh);

	cgh.single_task<class simple_kernel_2> ([=] ()
	  {
	    auto id = cl::sycl::id<2> (1, 1);
	    float value = input[id];
	    float dummy = value; /* kernel-line */
	  });
      });
  }
  return 0;
}
