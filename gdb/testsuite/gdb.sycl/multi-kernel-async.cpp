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
  int data1 = 11;
  int data2 = 22;

  cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
  cl::sycl::buffer<int, 1> buf1 {&data1, cl::sycl::range<1> {1}};
  cl::sycl::buffer<int, 1> buf2 {&data2, cl::sycl::range<1> {1}};

  deviceQueue.submit ([&] (cl::sycl::handler& cgh)
    {
      auto acc1
	= buf1.get_access<cl::sycl::access::mode::read> (cgh);

      cgh.single_task<class kernel_1> ([=] ()
	{
	  int item = acc1[0] + 100; /* kernel-1-line */
	});
    });

  deviceQueue.submit ([&] (cl::sycl::handler& cgh)
    {
      auto acc2
	= buf2.get_access<cl::sycl::access::mode::read> (cgh);

      cgh.single_task<class kernel_2> ([=] ()
	{
	  int item = acc2[0] + 200; /* kernel-2-line */
	});
    });

  int result = data1 + data2; /* post-kernel-line */

  deviceQueue.wait ();
  return 0;
}
