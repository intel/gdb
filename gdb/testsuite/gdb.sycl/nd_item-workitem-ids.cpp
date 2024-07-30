/* This testcase is part of GDB, the GNU debugger.

   Copyright 2023-2024 Free Software Foundation, Inc.

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
  constexpr size_t DIM0 = 60;
  constexpr size_t DIM1 = 40;
  constexpr size_t DIM2 = 20;

  int in[DIM0][DIM1][DIM2];
  int out[DIM0][DIM1][DIM2];

  /* Initialize the input.  */
  int val = 1;
  for (unsigned int i = 0; i < DIM0; i++)
    for (unsigned int j = 0; j < DIM1; j++)
      for (unsigned int k = 0; k < DIM2; k++)
	in[i][j][k] = val++;

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::range<3> dataRange {DIM0, DIM1, DIM2};
    sycl::buffer<int, 3> bufferIn {&in[0][0][0], dataRange};
    sycl::buffer<int, 3> bufferOut {&out[0][0][0], dataRange};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto accessorIn = bufferIn.get_access<sycl::access::mode::read> (cgh);
	auto accessorOut
	  = bufferOut.get_access<sycl::access::mode::write> (cgh);

	sycl::nd_range<3> kernel_range (dataRange, sycl::range<3> (4, 4, 4));
	cgh.parallel_for (kernel_range, [=] (sycl::nd_item<3> item)
	  [[sycl::reqd_sub_group_size(16)]]
	  {
	    sycl::id<3> gid = item.get_global_id (); /* kernel-first-line */

	    size_t thread_workgroup0 = item.get_group (0);
	    size_t thread_workgroup1 = item.get_group (1);
	    size_t thread_workgroup2 = item.get_group (2);

	    size_t workitem_global_id0 = item.get_global_id (0);
	    size_t workitem_global_id1 = item.get_global_id (1);
	    size_t workitem_global_id2 = item.get_global_id (2);

	    size_t workitem_local_id0 = item.get_local_id (0);
	    size_t workitem_local_id1 = item.get_local_id (1);
	    size_t workitem_local_id2 = item.get_local_id (2);

	    int in_elem = accessorIn[gid];
	    accessorOut[gid] = in_elem; /* kernel-1-last-line */
	  });
      });

    deviceQueue.wait_and_throw ();

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto accessorIn = bufferIn.get_access<sycl::access::mode::read> (cgh);
	auto accessorOut
	  = bufferOut.get_access<sycl::access::mode::write> (cgh);

	sycl::nd_range<3> kernel_range (dataRange, sycl::range<3> (5, 5, 5));
	cgh.parallel_for (kernel_range, [=] (sycl::nd_item<3> item)
	  [[sycl::reqd_sub_group_size(32)]]
	  {
	    sycl::id<3> gid = item.get_global_id ();

	    size_t thread_workgroup0 = item.get_group (0);
	    size_t thread_workgroup1 = item.get_group (1);
	    size_t thread_workgroup2 = item.get_group (2);

	    size_t workitem_global_id0 = item.get_global_id (0);
	    size_t workitem_global_id1 = item.get_global_id (1);
	    size_t workitem_global_id2 = item.get_global_id (2);

	    size_t workitem_local_id0 = item.get_local_id (0);
	    size_t workitem_local_id1 = item.get_local_id (1);
	    size_t workitem_local_id2 = item.get_local_id (2);

	    int in_elem = accessorIn[gid];
	    accessorOut[gid] = in_elem; /* kernel-2-last-line */
	  });
      });
  }

  /* Verify the output.  */
  for (unsigned int i = 0; i < DIM0; i++)
    for (unsigned int j = 0; j < DIM1; j++)
      for (unsigned int k = 0; k < DIM2; k++)
	if (in[i][j][k] != out[i][j][k])
	  {
	    std::cout << "Element " << i << "," << j << ", "
		      << k << " is " << out[i][j][k] << std::endl;
	    return 1;
	  }

  std::cout << "Correct" << std::endl;
  return 0;
}
