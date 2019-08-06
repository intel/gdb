/* This testcase is part of GDB, the GNU debugger.

   Copyright 2019 Free Software Foundation, Inc.

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

int
even_1 (int dim0)
{
  return dim0 + 2100; /* break even_1 */
}

int
even_2 (int dim0)
{
  return dim0 + 2200; /* break even_2 */
}

int
odd_1 (int dim0)
{
  return dim0 + 1100;  /* break odd_1 */
}

int
odd_2 (int dim0)
{
  return dim0 + 1200;  /* break odd_2 */
}

int
even (int dim0)
{
  int local = 0;
  if (dim0 % 4 == 0)
    {
      if (dim0 % 8 == 0)
	local++; /* break even then-then */
      else
	local++; /* break even then-else */
      return even_1 (dim0);
    }
  else
    {
      if (dim0 % 8 == 2)
	local++; /* break even else-then */
      else
	local++; /* break even else-else */
      return even_2 (dim0);
    }
}

int
odd (int dim0)
{
  int local = 0;
  if (dim0 % 4 == 1)
    {
      if (dim0 % 8 == 1)
	local++; /* break odd then-then */
      else
	local++; /* break odd then-else */
      return odd_1 (dim0);
    }
  else
    {
      if (dim0 % 8 == 3)
	local++; /* break odd else-then */
      else
	local++; /* break odd else-else */
      return odd_2 (dim0);
    }
}

int
main (int argc, char *argv[])
{
  constexpr unsigned long data_size = 25;
  int out[data_size];

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::range<1> dataRange {data_size};
    sycl::buffer<int, 1> bufferOut {&out[0], dataRange};

    deviceQueue.submit ([&] (sycl::handler& cgh)
    {
      auto accessorOut= bufferOut.get_access<sycl::access::mode::write> (cgh);

      cgh.parallel_for<class kernel> (dataRange, [=] (sycl::id<1> wiID)
				      [[sycl::reqd_sub_group_size(16)]]
      {
	int dim0 = wiID[0];          /* kernel-line-1 */
	if (dim0 % 2 == 0)
	  accessorOut[wiID] = even (dim0);  /* then-branch */
	else
	  accessorOut[wiID] = odd (dim0);   /* else-branch */
      });
    });
  }

  return 0;
}
