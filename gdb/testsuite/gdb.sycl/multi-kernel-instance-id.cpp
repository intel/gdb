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

constexpr int length = 128;
constexpr int num_kernels = 2;

int
main (int argc, char *argv[])
{
  int data1[num_kernels][1] {{11}, {11}};
  int data2[1] {22};
  sycl::range range {length};

  sycl::queue deviceQueue {get_sycl_queue (argc, argv)};

  sycl::buffer<int, 1> buf1[num_kernels] {
    {data1[0], sycl::range<1> {1}},
    {data1[1], sycl::range<1> {1}}
  };
  sycl::buffer<int, 1> buf2 {data2, sycl::range<1> {1}};

  /* We submit `kernel_1` multiple times.  We expect the kernels to run
     simultaneously and the kernel-instance-ids are expected to be constant
     per kernel submission.  Next, we submit a mutually independent kernel
     `kernel_2` that is expected to run simultaneously with the submitted
     instances of `kernel_1`.  */
  for (int i = 0; i < num_kernels; i++)
    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto acc1 = buf1[i].get_access<sycl::access::mode::read> (cgh);
	cgh.parallel_for<class kernel_1> (range, [=] (sycl::id<1> index)
	  {
	    int item = acc1[0] + 100; /* kernel-1-line.  */
	  });
      });

  deviceQueue.submit ([&] (sycl::handler& cgh)
    {
      auto acc2 = buf2.get_access<sycl::access::mode::read> (cgh);

      /* Submit a mutually independent kernel to test for different
	 kernel-instance-ids.  A `single_task` is sufficient here.  */
      cgh.single_task<class kernel_2> ([=] ()
	{
	  int item = acc2[0] + 200; /* kernel-2-line.  */
	});
    });

  deviceQueue.wait ();

  return 0; /* post-kernel-line */
}
