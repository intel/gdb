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
#include "../lib/sycl-util.cpp"

struct packed_struct1
{
  uint8_t x;
  uint32_t y;
  uint8_t z;
  uint16_t a;
} __attribute__ ((packed));

struct packed_struct2
{
  uint8_t x;
  uint8_t y;
  uint8_t z;
  uint16_t a;
} __attribute__ ((packed));

struct struct_bit_fields1
{
  uint16_t a : 3;
  uint16_t b : 3;
  uint16_t c : 3;
  uint16_t d : 3;
  uint16_t e : 3;
  uint16_t f : 1;
};

struct struct_bit_fields2
{
  uint16_t a : 7;
  uint16_t b : 3;
  uint32_t c : 5;
  uint8_t d : 3;
};

int
make_all_calls ()
{
  auto p_s1 = packed_struct1 { 1, 2, 3, 4 };
  auto p_s2 = packed_struct1 { 11, 12, 13, 14 };
  packed_struct1 arr_p1[] = { p_s1, p_s2 };

  auto p_s3 = packed_struct2 { 1, 2, 3, 4 };
  auto p_s4 = packed_struct2 { 11, 12, 13, 14 };
  packed_struct2 arr_p2[] = { p_s3, p_s4 };

  auto bf_s1 = struct_bit_fields1 { 1, 2, 3, 4, 5, 1 };
  auto bf_s2 = struct_bit_fields1 { 6, 7, 0, 1, 2, 0 };
  struct_bit_fields1 arr_bf1[] = { bf_s1, bf_s2 };

  auto bf_s3 = struct_bit_fields2 { 1, 0, 3, 4 };
  auto bf_s4 = struct_bit_fields2 { 11, 1, 13, 7 };
  struct_bit_fields2 arr_bf2[] = { bf_s3, bf_s4 };

  int ans = 0; /* line-after-var-declaration */
  return ans;
}

int
main (int argc, char *argv[])
{
  cl::sycl::queue deviceQueue {get_sycl_queue (argc, argv)};

  deviceQueue.submit ([&] (cl::sycl::handler& cgh)  /* line-before-kernel */
    {
      cl::sycl::range<1> dataRange {8};
      cgh.parallel_for<class kernel> (dataRange, [=] (cl::sycl::id<1> wiID)
	{
	  int a = make_all_calls ();  /* line-inside-kernel */
	});
    });
  deviceQueue.wait();

  return 0; /* return-stmt */
}

