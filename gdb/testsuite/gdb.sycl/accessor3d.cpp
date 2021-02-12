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

struct user_type
{
  int x;
};

int
main (int argc, char *argv[])
{
  user_type data_in[2][2][2]
    = { { { 1, 2 }, { 3, 4 } }, { { 5, 6 }, { 7, 8 } } };
  user_type data_out[2][2][2];

  {
    cl::sycl::queue queue = { get_sycl_queue (argc, argv) };
    cl::sycl::buffer<user_type, 3> buffer_in
      = { &data_in[0][0][0], cl::sycl::range<3> { 2, 2, 2 } };
    cl::sycl::buffer<user_type, 3> buffer_out
      = { &data_out[0][0][0], cl::sycl::range<3> { 2, 2, 2 } };

    queue.submit ([&] (cl::sycl::handler &cgh)
      {
	auto input = buffer_in.get_access<cl::sycl::access::mode::read> (cgh);
	auto output = buffer_out.get_access<cl::sycl::access::mode::write> (cgh);

	cgh.single_task<class simple_kernel> ([=] ()
	  {
	    auto id = cl::sycl::id<3> (0, 0, 0);
	    output[id] = input[id];
	    user_type dummy = input[id]; /* kernel-line */
	  });
      });
  }
  return 0;
}
