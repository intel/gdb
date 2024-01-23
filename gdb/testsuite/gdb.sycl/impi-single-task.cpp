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
#include <mpi.h>
#include "../lib/sycl-util.cpp"

int
main (int argc, char *argv[])
{
  int rank = 0;
  int num_procs = 0;
  int data[4] = {7, 8, 9};

  MPI_Init (&argc, &argv);
  MPI_Comm_size (MPI_COMM_WORLD, &num_procs); /* line-after-mpi-init */
  MPI_Comm_rank (MPI_COMM_WORLD, &rank);

  data[3] = rank;

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::buffer<int, 1> buf {data, sycl::range<1> {3}};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto numbers = buf.get_access<sycl::access::mode::read_write> (cgh);

	cgh.single_task<> ([=] ()
	  {
	    int ten = numbers[1] + 2;  /* kernel-line-1 */
	    int four = numbers[2] - 5; /* kernel-line-2 */
	    int fourteen = ten + four; /* kernel-line-3 */
	    numbers[0] = fourteen * 3; /* kernel-line-4 */
	  });
      });
  }

  std::cout << "Result is " << data[0] << std::endl;  /* line-after-kernel */

  MPI_Finalize ();
  return data[0] == 42 ? 0 : 1; /* return-stmt */
}
