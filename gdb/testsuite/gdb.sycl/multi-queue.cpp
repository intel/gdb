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

static void
kernel2 (int argc, char *argv[])
{
  int data = 22;
  cl::sycl::queue queue { get_sycl_queue (argc, argv) };
  cl::sycl::buffer buffer {&data, cl::sycl::range {1}};
  queue.submit ([&] (cl::sycl::handler& cgh)
    {
      cl::sycl::accessor acc (buffer, cgh, cl::sycl::read_write);
      cgh.single_task<class simple_kernel_1> ([=] ()
	{
	  acc[0] += 200; /* inside-kernel2 */
	});
    });
  queue.wait ();
}

static void
kernel1 (int argc, char *argv[])
{
  int data = 11;
  cl::sycl::queue queue { get_sycl_queue (argc, argv) };
  cl::sycl::buffer buffer {&data, cl::sycl::range {1}};
  queue.submit ([&] (cl::sycl::handler& cgh)
    {
      cl::sycl::accessor acc (buffer, cgh, cl::sycl::read_write);
      cgh.single_task<class simple_kernel_2> ([=] ()
	{
	  acc[0] += 100; /* inside-kernel1 */
	});
    });
  queue.wait ();
}

int
main (int argc, char *argv[])
{
  kernel1(argc, argv);
  kernel2(argc, argv);
  return 0;
}
