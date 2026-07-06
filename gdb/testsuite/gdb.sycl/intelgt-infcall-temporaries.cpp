/* This testcase is part of GDB, the GNU debugger.

   Copyright 2026 Free Software Foundation, Inc.
   Copyright (C) 2026 Intel Corporation.

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

struct int_x
{
  int x = 100;

  int
  getx ()
  {
    return x;
  }
};

/* Test 16-byte, 4-byte aligned struct return value for temporaries.  */
struct int_array_4
{
  int data[4];

  int &
  operator[] (int id)
  {
    return data[id];
  }
};

/* Test 4-byte, 1-byte aligned struct return value for temporaries.  */
struct uchar_array_4
{
  unsigned char data[4];

  unsigned char &
  operator[] (int id)
  {
    return data[id];
  }
};

struct struct_greater_64b
{
  long ll = 0;
  int32_t i = 0;
  int_array_4 int_data[4];
  uchar_array_4 uchar_data[4];

  int_array_4
  getint_array (int id)
  {
    int_array_4 int4;

    for (int j = 0; j < 4; j++)
      int4.data[j] = int_data[id][j];

    return int4;
  }

  uchar_array_4
  getuchar_array (int id)
  {
    uchar_array_4 uc4;

    for (int j = 0; j < 4; j++)
      uc4.data[j] = uchar_data[id][j];

    return uc4;
  }
};

struct S2
{
  int_array_4 x;
  int_array_4 y;
};

int_array_4
add (S2 s2)
{
  int_array_4 result;

  for (int i = 0; i < 4; i++)
    result[i] = s2.x[i] + s2.y[i];

  return result;
}

int_array_4
return_int_array (int dim0)
{
  int_array_4 result;

  for (int j = 0; j < 4; j++)
    result[j] = 100 * dim0 + j;

  return result;
}

uchar_array_4
return_uchar_array (int dim0)
{
  uchar_array_4 result;

  for (unsigned int j = 0; j < 4; j++)
    result[j] = dim0 + j;

  return result;
}

S2
make_s2 (int dim0)
{
  S2 s2;

  for (int j = 0; j < 4; j++)
    {
      s2.x[j] = 100 * dim0 + j;
      s2.y[j] = 100;
    }

  return s2;
}

struct_greater_64b
make_str_gr_64 (int dim0)
{
  struct_greater_64b str_gr_64;

  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      {
	str_gr_64.int_data[i][j] = 100 * dim0 + j;
	str_gr_64.uchar_data[i][j] = dim0 + j;
      }

  return str_gr_64;
}

int
make_output (int dim0)
{
  int_x ivar;
  ivar.x = dim0;

  /* Test struct/class return values.  */
  int_array_4 int4 = return_int_array (ivar.getx ());
  uchar_array_4 uc4 = return_uchar_array (ivar.getx ());

  /* Test struct S2 with arrays.  */
  S2 s2 = make_s2 (dim0);
  int_array_4 res = add (s2);

  /* Test struct_greater_64b member functions.  */
  struct_greater_64b str_gr_64 = make_str_gr_64 (dim0);

  int_array_4 int4_member = str_gr_64.getint_array (0);
  uchar_array_4 uc4_member = str_gr_64.getuchar_array (0);

  return int4.data[0]; /* break-line */
}

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 16;

  int out[DIM0];

  { /* Extra scope enforces waiting on the kernel.  */
    sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    sycl::range<1> dataRange {DIM0};
    sycl::buffer<int, 1> bufferOut {&out[0], dataRange};

    deviceQueue.submit ([&] (sycl::handler& cgh)
      {
	auto accessorOut
	  = bufferOut.get_access<sycl::access::mode::write> (cgh);

	cgh.parallel_for (dataRange, [=] (sycl::id<1> wiID)
	  {
	    accessorOut[wiID] = make_output (wiID[0]);
	  });
      });
  }

  /* Verify the output.  */
  for (unsigned int i = 0; i < DIM0; i++)
    if (out[i] != i * 100)
      {
	std::cout << "Element " << i << " is " << out[i] << std::endl;
	return 1;
      }

  std::cout << "Correct" << std::endl; /* end-marker */
  return 0;
}
