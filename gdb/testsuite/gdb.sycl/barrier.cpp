/* This testcase is part of GDB, the GNU debugger.

   Copyright 2022-2023 Free Software Foundation, Inc.

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
  /* Partition the data space by GDIM groups, with LDIM elements in
     each group.  Use a large value for LDIM to ensure multiple
     threads in each group even for a SIMD width > 64.  On some GPU
     systems, the max permitted value is 256, so we use that.  */
  constexpr int GDIM = 5;
  constexpr int LDIM = 256;
  int out[GDIM * LDIM];

  for (int i = 0; i < GDIM * LDIM; i++)
    out[i] = 0;

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::buffer<int> bufferOut {&out[0], sycl::range<1> {GDIM * LDIM}};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	sycl::accessor out {bufferOut, cgh, sycl::write_only};

	sycl::nd_range<1> kernel_range (sycl::range<1> {GDIM * LDIM},
					sycl::range<1> {LDIM});
	cgh.parallel_for (kernel_range, [=] (sycl::nd_item<1> item)
	  {
	    size_t group_lid = item.get_group_linear_id (); /* first-line */
	    size_t local_lid = item.get_local_linear_id ();
	    size_t global_lid = item.get_global_linear_id ();

	    group_barrier (item.get_group ()); /* the-barrier */

	    int value = group_lid * 10000 + local_lid; /* the-value */
	    out[global_lid] = value; /* last-line */
	  });
      });
  }

  /* Verify the output.  */
  for (int i = 0; i < GDIM; i++)
    for (int j = 0; j < LDIM; j++)
      {
	int loc = i * LDIM + j;
	int expected = i * 10000 + j;
	if (out[loc] != expected)
	  {
	    std::cout << "Element " << loc << " is " << out[loc]
		      << " but was expecting " << expected << std::endl;
	    return 1;
	  }
      }

  std::cout << "Correct" << std::endl;
  return 0;
}
