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

#include <CL/sycl.hpp>
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  constexpr unsigned int length = 4;
  int in[length];

  /* Initialize the input.  */
  for (unsigned int i = 0; i < length; i++)
    in[i] = i;

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    cl::sycl::buffer<int, 1> bufIn {&in[0], cl::sycl::range<1> {length}};

    /* Spawn kernels that are independent of each other.  */
    for (unsigned int i = 0; i < length; i++)
      deviceQueue.submit ([&] (cl::sycl::handler& cgh)
	{
	  auto accessorIn
	    = bufIn.get_access<cl::sycl::access::mode::read> (cgh);

	  cgh.single_task ([=] ()
	    {
	      int item = accessorIn[i] + 100; /* kernel-line */
	    });
	});
  }

  return 0; /* line-after-kernel */
}
