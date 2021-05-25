/* This testcase is part of GDB, the GNU debugger.

   Copyright 2021 Free Software Foundation, Inc.

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
#include <iostream>
#include "../lib/sycl-util.cpp"

struct struct_greater_64b
{
  long ll;
  int int4[4];
  int32_t i;
  unsigned char uchar4[4];
};

struct struct4
{
  int32_t i;
};

union union4
{
  int32_t i;
  char c[4];
};

typedef __attribute__(( ext_vector_type(2) )) unsigned char uchar2;
typedef __attribute__(( ext_vector_type(3) )) unsigned char uchar3;
typedef __attribute__(( ext_vector_type(4) )) unsigned char uchar4;
typedef __attribute__(( ext_vector_type(8) )) unsigned char uchar8;
typedef __attribute__(( ext_vector_type(10) )) unsigned char uchar10;
typedef __attribute__(( ext_vector_type(4) )) int int4;

int
do_something_and_return (int i) {
  return i * 5;
}

int
return_int (int i)
{
  return i * 100; /* bp-at-return_int */
}

unsigned char
do_smt_and_return_uchar (int i)
{
  int k = do_something_and_return (i);
  return i % 10; /* bp-at-do_smt_and_return_uchar */
}

struct4
return_struct4 (union4 u4)
{
  struct4 str4;

  str4.i = return_int (u4.i);

  /* Do something after function call.  */
  do_something_and_return (u4.i);

  return str4;
}

struct_greater_64b
return_struct_greater_64b (int4 i4, uchar4 uc4)
{
  struct_greater_64b str_gr_64b;
  union4 u4 {i4[1]};
  struct4 str4 = return_struct4 (u4);

  str_gr_64b.ll = i4[0];
  str_gr_64b.i = i4[1] + 200;

  for (int i = 0; i < 4; i++)
    {
      str_gr_64b.int4[i] = i4[i];
      str_gr_64b.uchar4[i] = uc4[i];
    }

  return str_gr_64b;
}

int4
return_int4 (int dim0)
{
  int4 i4;
  int k = do_something_and_return (dim0); /* return_int4-after-prologue */

  for (int i = 0; i < 4; i++) /* bp-at-return_int4 */
    i4[i] = 100 * dim0 + i;

  return i4;
}

uchar2
return_uchar2 (int dim0)
{
  uchar2 uc2;

  for (int i = 0; i < 2; i++) /* bp-at-return_uchar2 */
    uc2[i] = dim0 * 10 + i;

  return uc2;
}

uchar3
do_smt_and_return_uchar3 (int dim0)
{
  uchar3 uc3;
  int k = do_something_and_return (dim0);

  for (int i = 0; i < 3; i++) /* bp-at-do_smt_and_return_uchar3 */
    uc3[i] = dim0 * 10 + i;

  return uc3;
}

uchar4
return_uchar4 (int dim0)
{
  uchar4 uc4;

  for (int i = 0; i < 4; i++) /* bp-at-return_uchar4 */
    uc4[i] = dim0 << i;

  return uc4;
}

uchar8
return_uchar8 (int dim0)
{
  uchar8 uc8;

  for (int i = 0; i < 8; i++) /* bp-at-return_uchar8 */
    uc8[i] = dim0 * 10 + i;

  return uc8;
}

uchar10
return_uchar10 (int dim0)
{
  uchar10 uc10;

  for (int i = 0; i < 10; i++) /* bp-at-return_uchar10 */
    uc10[i] = dim0 * 10 + i;

  return uc10;
}


uchar10
do_smt_and_return_uchar10 (int dim0)
{
  uchar10 uc10;
  int k = do_something_and_return (dim0);

  for (int i = 0; i < 10; i++) /* bp-at-do_smt_and_return_uchar10 */
    uc10[i] = dim0 * 10 + i;

  return uc10;
}

int
tail_call_inner_int (int dim0) {
  return dim0 * 2; /* bp-at-tail_call_inner_int */
}

int
tail_call_outer_int (int dim0) {
  return tail_call_inner_int (dim0 * 2);
}

int
make_output (int dim0)
{
  int4 i4 = return_int4 (dim0); /* return_int4-outer */
  uchar4 uc4 = return_uchar4 (dim0);
  struct_greater_64b str_gr_64 = return_struct_greater_64b (i4, uc4);
  do_smt_and_return_uchar (dim0);
  return_uchar2 (dim0);
  do_smt_and_return_uchar3 (dim0);
  return_uchar8 (dim0);
  return_uchar10 (dim0);
  do_smt_and_return_uchar10 (dim0);
  tail_call_outer_int (dim0);

  return str_gr_64.ll;
}

static int
get_dim (cl::sycl::id<1> wi, int index)
{
  return wi[index];
}

int
main (int argc, char *argv[])
{
  constexpr size_t DIM0 = 8;

  int out[DIM0];

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    cl::sycl::range<1> dataRange {DIM0};
    cl::sycl::buffer<int, 1> bufferOut {&out[0], dataRange};

    deviceQueue.submit ([&] (cl::sycl::handler& cgh) /* line-before-kernel */
      {
	auto accessorOut
	  = bufferOut.get_access<cl::sycl::access::mode::write> (cgh);

	cgh.parallel_for (dataRange, [=] (cl::sycl::id<1> wiID)
	  {
	    int dim0 = get_dim (wiID, 0); /* kernel-first-line */
	    accessorOut[wiID] = make_output (dim0); /* dim0-defined */
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
