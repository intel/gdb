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

#include "../lib/sycl-util.cpp"
#include <mpi.h>
#include <sycl/sycl.hpp>

int
main (int argc, char *argv[])
{
  int rank = 0;
  int num_procs = 0;
  int data[3] = { 1, 2, 3 };

  MPI_Init (&argc, &argv);
  MPI_Comm_size (MPI_COMM_WORLD, &num_procs); /* line-after-mpi-init */
  MPI_Comm_rank (MPI_COMM_WORLD, &rank);

  {
    sycl::queue queue = { get_sycl_queue (argc, argv) };
    sycl::buffer<int, 1> buffer = { data, sycl::range<1> { 3 } };
    queue.submit ([&] (sycl::handler &cgh)
      {
	auto input = buffer.get_access<sycl::access::mode::read> (cgh);
	cgh.single_task<> ([=] ()
	  {
	    int one = input[0];
	    auto id = sycl::id<1> (1);
	    int two = input[id];
	    int dummy = one + two; /* kernel-line */
	  });
      });
  }
  MPI_Finalize ();
  return 0;
}
