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

#include <CL/sycl.hpp>
#include <iostream>
#include "../lib/sycl-util.cpp"

typedef __attribute__ ((ext_vector_type (5))) unsigned char uchar5;
typedef __attribute__ ((ext_vector_type (10))) unsigned char uchar10;
typedef __attribute__ ((ext_vector_type (2))) unsigned int uint2;

struct simple_struct
{
  uint16_t x;
  bool a;
  uint16_t y;
  char b;
  char c;
  int d;
};

struct simple_struct_128b
{
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
};

struct complex_struct
{
  int x;
  int y;
  uint2 vec;
};

int
no_args ()
{
  int a = 5;
  int b = 4;
  return a * b;
}

/* All three integer arguments are passed on GRFs.  */
int
integer_grf_args (int a1, int a2, int a3)
{
  return a1 * a2 * a3; /* bp-inside-function */
}

/* Arguments less than 32-bits should not be casted to 4-byte variables.  */
int
bool_i8_i16_grf (bool a1, uint8_t a2, uint16_t a3)
{
  return 10 * a1 + (a2 * a3);
}

/* The two vectors are passed on GRFs.  */
int
vector_grf_args (uchar10 char_array10, uint2 int_array2)
{
  int sum1 = 0, sum2 = 0;
  for (int i = 0; i < 10; ++i)
    sum1 += char_array10[i];

  for (int i = 0; i < 2; ++i)
    sum2 += int_array2[i];

  return sum1 + sum2;
}

/* Object is pushed to the stack while its references and the second argument
   are passed on GRFs.  */
int
struct_stack_grf_args (complex_struct s, int a)
{
  return (s.y - s.x) * a;
}

/* The first uint64_t arguments are passed on GRFs (uses all 12 GRFs), the
   structure S, its reference, and A are passed on the stack.  */
int
struct_int_stack_args (uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
		       uint64_t a5, uint64_t a6, complex_struct s, int a)
{
  return a * (s.y - s.x) * (a1 + a2 + a3 + a4 + a5 + a6);
}

/* Promoted struct is passed on GRF.  */
int
promote_struct_grf (int a1, simple_struct s, int a2)
{
  if (s.a == true)
    return a1 * (s.b + s.c + s.d);
  else
    return a2 * (s.x + s.y);
}

/* 128-bit struct is promoted to be passed by value on GRF.  */
int
promote_struct128_grf (simple_struct_128b s)
{
  return s.a + s.b + s.c + s.d;
}

/* Promoted struct is passed on the stack.  */
int
promote_struct_stack (uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
		      uint64_t a5, uint64_t a6, simple_struct s, int a7)
{
  return (s.x + s.y + s.b + s.c + s.d) + (a1 + a2 + a3 + a4 + a5 + a6);
}

/* 128-bit struct is promoted to be passed by value on the stack.  */
int
promote_struct128_stack (uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
			 uint64_t a5, uint64_t a6, simple_struct_128b s)
{
  return (s.a + s.b + s.c + s.d) + (a1 + a2 + a3 + a4 + a5 + a6);
}

/* Vector is passed on the stack with the AoS layout.  */
int
vector_on_stack (uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
		 uint64_t a5, uint64_t a6, uchar10 arr)
{
  int sum = 0;
  for (int i = 0; i < 10; ++i)
    sum += arr[i];
  sum += a1 + a2 + a3 + a4 + a5 + a6;

  return sum;
}

/* Complex structs are converted to be passed by reference as the first
   argument, and returned on the stack.  */
complex_struct
complex_struct_return (int a, int b, unsigned int c, unsigned int d)
{
  complex_struct ans;
  ans.x = a;
  ans.y = b;
  ans.vec = { c, d };
  return ans;
}

/* Vectors with size less than 64-bits are returned on GRFs.  */
uchar5
small_vector_return ()
{
  return { 1, 2, 3, 4, 5 };
}

/* Vectors with size more than 64-bits are converted to be passed by reference
   as the first argument, and returned on the stack.  */
uchar10
long_vector_return ()
{
  return { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
}

int
mixed_types (simple_struct s_s1, int a1, complex_struct s1, complex_struct &s2,
	     int a2, uchar10 arr1, uchar10 &arr2, simple_struct s_s2)
{
  int sum = 0;
  for (int i = 0; i < 10; ++i)
    sum += arr1[i] + arr2[i];

  sum += struct_stack_grf_args (s1, a1) + struct_stack_grf_args (s2, a2);
  sum += promote_struct_grf (a1, s_s1, a2) + promote_struct_grf (a2, s_s2, a1);
  return sum;
}

int
make_all_calls ()
{
  uchar10 arr1_uchar10 = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
  uchar10 arr2_uchar10 = { 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
  uint2 arr1_uint2 = { 100, 110 };
  uint2 arr2_uint2 = { 120, 130 };
  complex_struct c_s1 = { 17, 170, { 11, 22 } };
  complex_struct c_s2 = { 18, 180, { 111, 222 } };
  simple_struct s_s1 = { 1, true, 2, 3, 4, 5 };
  simple_struct s_s2 = { 8, false, 22, 33, 44, 55 };
  simple_struct_128b s_128 = { 22, 33, 44, 55 };
  int ans = 0; /* bp-after-variables-declaration */
  ans += no_args ();
  ans += integer_grf_args (1, 2, 3);
  ans += bool_i8_i16_grf (true, 2, 3);
  ans += vector_grf_args (arr1_uchar10, arr1_uint2);
  ans += struct_stack_grf_args (c_s1, 13);
  ans += struct_int_stack_args (1, 2, 3, 4, 5, 6, c_s1, 13);
  ans += vector_on_stack (1, 2, 3, 4, 5, 6, arr1_uchar10);
  ans += promote_struct_grf (1, s_s1, 2);
  ans += promote_struct128_grf (s_128);
  ans += promote_struct128_stack (1, 2, 3, 4, 5, 6, s_128);
  ans += promote_struct_stack (1, 2, 3, 4, 5, 6, s_s1, 7);
  ans += mixed_types (s_s1, 1, c_s1, c_s2, 2, arr1_uchar10, arr2_uchar10,
		      s_s2);

  auto c_struct = complex_struct_return (1, 2, 3, 4);
  auto v1 = small_vector_return ();
  auto v2 = long_vector_return ();
  return ans;
}

int
main (int argc, char *argv[])
{
  int data[2] = {7, 8};

  { /* Extra scope enforces waiting on the kernel.  */
    cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};
    cl::sycl::buffer<int, 1> buf {data, cl::sycl::range<1> {2}};

    deviceQueue.submit ([&] (cl::sycl::handler& cgh)  /* line-before-kernel */
      {
	auto numbers
	  = buf.get_access<cl::sycl::access::mode::read_write> (cgh);

	cl::sycl::range<1> dataRange {8};
	cgh.parallel_for<class kernel> (dataRange, [=] (cl::sycl::id<1> wiID)
	  {
	    numbers[0] = make_all_calls ();  /* line-inside-kernel */
	  });
      });
  }

#ifndef OMIT_REPORT
  std::cout << "Result is " << data[0] << std::endl;  /* line-after-kernel */
#endif

  return 0; /* return-stmt */
}
