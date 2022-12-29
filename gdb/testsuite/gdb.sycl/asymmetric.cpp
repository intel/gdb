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

/* We define a macro instead of a constexpr to be able to use in the
   sycl::reqd_sub_group_size attribute.  */
#define SUBGROUP_SIZE 16

int
main (int argc, char *argv[])
{
  /* Partition the data space by GDIM groups, with LDIM elements in
     each group.  Below we enforce a subgroup size (i.e. SIMD width on
     GPU) of SUBGROUP_SIZE.  */
  constexpr int GDIM = 1;
  constexpr int NUM_THREADS = 3;
  constexpr int LDIM = SUBGROUP_SIZE * NUM_THREADS;
  int out[GDIM * LDIM];
  int latch = 0;

  for (int i = 0; i < GDIM * LDIM; i++)
    out[i] = 0;

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::buffer<int> bufferOut {&out[0], sycl::range<1> {GDIM * LDIM}};
    sycl::buffer<int> bufferLatch {&latch, sycl::range<1> {1}};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	sycl::accessor out {bufferOut, cgh, sycl::write_only};
	sycl::accessor latch {bufferLatch, cgh, sycl::read_write};

	sycl::nd_range<1> kernel_range (sycl::range<1> {GDIM * LDIM},
					sycl::range<1> {LDIM});
	cgh.parallel_for (kernel_range, [=] (sycl::nd_item<1> item)
			  [[sycl::reqd_sub_group_size (SUBGROUP_SIZE)]]
	  {
	    size_t group_lid = item.get_group_linear_id ();
	    size_t local_lid = item.get_local_linear_id ();
	    size_t global_lid = item.get_global_linear_id ();

	    int value = 0;
	    if (global_lid < SUBGROUP_SIZE)
	      {
		/* Make sure other threads reach the 'else' branch.  */
		while (latch[0] == 0);

		value = global_lid; /* then-branch */
	      }
	    else
	      {
		/* The counter ensures this does not run infinitely.
		   The boolean flag is set from inside the debugger to
		   stop spinning.  We do not use the counter for that
		   purpose, because there can be a write-after-write
		   race between the debugger and the program itself.  */
		long long count = 1e8;
		bool spin = true;
		latch[0] = 1; /* Unleash the 'then' thread.  */
		while (count > 0 && spin) count--; /* busy-wait */

		value = global_lid; /* else-branch */
	      }

	    value = group_lid * 10000 + local_lid;
	    out[global_lid] = value;
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
