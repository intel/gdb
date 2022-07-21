/* This testcase is part of GDB, the GNU debugger.

   Copyright 2022 Free Software Foundation, Inc.

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

int verify (int data [], const int N)
{
  int errcode = 0;

  for (int i = 0; i < N; i++)
    {
      if ((i % 2 == 0) && (data[i] != i))
	{
	  std::cout << "data[" << i << "] = " << data[i] << ", "
		    << ", expected " << i << std::endl;
	  errcode = 1;
	}
      else if ((i % 2 == 1) && (data[i] != 2 * i))
	{
	  std::cout << "data[" << i << "] = " << data[i] << ", "
		    << ", expected " << 2 * i << std::endl;
	  errcode = 1;
	}
      data[i] = 0;
    }
  return errcode;
}

int
main (int argc, char *argv[])
{
  constexpr int simd_width16 = 16;
  constexpr int simd_width32 = 32;
  constexpr int N = 128;
  int data[N] = {0};

  sycl::queue queue {get_sycl_queue (argc, argv)};

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::range<1> data_range = sycl::range {N};
    sycl::buffer<int> data_buffer {data, data_range};

    queue.submit ([&] (auto &h)
      {
	sycl::accessor out {data_buffer, h};
	h.parallel_for (data_range, [=] (sycl::id<1> index)
			[[intel::reqd_sub_group_size (simd_width16)]]
	  {
	    const int idx = index[0];
	    if (idx % 2 == 0)
	      out[idx] = idx; /* simd.1 */
	    else
	      out[idx] = 2 * idx;
	  });
      });
  }

  int errcode = verify (data, N);

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::range<1> data_range = sycl::range {N};
    sycl::buffer<int> data_buffer {data, data_range};

    queue.submit ([&] (auto &h)
      {
	sycl::accessor out {data_buffer, h};
	h.parallel_for (data_range, [=] (sycl::id<1> index)
			[[intel::reqd_sub_group_size (simd_width32)]]
	  {
	    const int idx = index[0];
	    if (idx % 2 == 0)
	      out[idx] = idx;
	    else
	      out[idx] = 2 * idx; /* simd.2 */
	  });
      });
  }

  errcode = verify (data, N);

  return errcode; /* return-stmt */
}
