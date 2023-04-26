/* This testcase is part of GDB, the GNU debugger.

   Copyright 2023 Free Software Foundation, Inc.

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
#include <sycl/sycl.hpp>

int
main (int argc, char *argv[])
{
  int data[3] = { 1, 2, 3 };

  {
    sycl::queue queue = { get_sycl_queue (argc, argv) };
    sycl::buffer<int, 1> buffer = { data, sycl::range<1>{ 3 } };

    queue.submit ([&] (sycl::handler &cgh)
      {
	auto input = buffer.get_access<sycl::access::mode::read> (cgh);
	sycl::local_accessor<int> local_mem (3, cgh);
	sycl::local_accessor<int *> local_mem_ptr (3, cgh);

	cgh.parallel_for_work_group (sycl::range<1> (1),
				     sycl::range<1> (1),
				     [=] (sycl::group<1> wg)
	  {
	    local_mem[0] = input[0];
	    local_mem[1] = input[1];
	    local_mem[2] = input[2];
	    int local_var = 32;
	    int *local_ptr = &local_var;
	    int &local_ref = local_var;
	    *local_ptr = 33; /* BP1. */

	    wg.parallel_for_work_item ([&] (sycl::h_item<1> wi)
	      {
		int generic_var = 421;
		local_mem_ptr[0] = &local_mem[0];
		local_mem_ptr[1] = &generic_var;

		sycl::decorated_local_ptr<int> d_local_ptr
		  = local_mem.get_multi_ptr<sycl::access::decorated::yes> ();

		sycl::raw_local_ptr<int *> r_local_ptr
		  = local_mem_ptr.get_multi_ptr<sycl::access::decorated::no> ();

		int *generic_ptr = &local_mem[1];
		*local_ptr += 1;
		local_ref += 1;
		generic_var = 11 + local_var; /* BP2. */
		generic_ptr = &generic_var;
		generic_var = 3; /* BP3. */
		generic_ptr = &local_mem[2];
		generic_var = 4; /* BP4. */
		*generic_ptr += local_mem[2];
		generic_var = 5;
	      });
	  });
      });
  }
  return 0;
}
