/* Copyright 2024 Free Software Foundation, Inc.

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

#include "sycl/sycl.hpp"
#include "../lib/sycl-util.cpp"
#include <iostream>
#include <algorithm>
#include <cmath>

#ifdef SG_SIZE
static constexpr uint32_t sg_size = SG_SIZE;
#else
static constexpr uint32_t sg_size = 16;
#endif
static constexpr uint32_t m_tile = 8;
static constexpr uint32_t n_tile = sg_size;
static constexpr uint32_t k_tile = 16;
static constexpr uint32_t M = m_tile * 2;
static constexpr uint32_t N = n_tile * 4;
static constexpr uint32_t K = k_tile * 4;

using bfloat16 = sycl::ext::oneapi::bfloat16;

void
matrix_multiply (float *C, bfloat16 *A, bfloat16 *B, sycl::queue &deviceQueue)
{
  size_t NDRangeM = M / m_tile;
  size_t NDRangeN = N / n_tile;
  sycl::buffer<bfloat16, 2> bufA (A, sycl::range<2> (M, K));
  sycl::buffer<bfloat16, 2> bufB (B, sycl::range<2> (K, N));
  sycl::buffer<float, 2> bufC (C, sycl::range<2> (M, N));

  deviceQueue.submit ([&](sycl::handler &cgh)
    {
      auto accC = bufC.get_access<sycl::access::mode::read_write> (cgh);
      auto accA = bufA.get_access<sycl::access::mode::read> (cgh);
      auto accB = bufB.get_access<sycl::access::mode::read> (cgh);
      auto range = sycl::nd_range<2> ({NDRangeM, NDRangeN * sg_size},
				      {1, 1 * sg_size});

      cgh.parallel_for (range, [=](sycl::nd_item<2> item)
	[[intel::reqd_sub_group_size (sg_size)]]
	{
	  using namespace sycl::ext::oneapi::experimental::matrix;

	  const auto gidx = item.get_global_id (0); /* kernel-line-1.  */
	  const auto gidy = item.get_global_id (1);
	  const auto sg_startx = gidx - item.get_local_id (0);
	  const auto sg_starty = gidy - item.get_local_id (1);

	  sycl::sub_group sg = item.get_sub_group ();
	  constexpr int n_k_tile = K / k_tile;
	  joint_matrix<sycl::sub_group, bfloat16, use::a,
		       m_tile, k_tile, layout::row_major> sub_a[n_k_tile];
	  /* For atomic sequence the layout has to be packed.  */
	  joint_matrix<sycl::sub_group, bfloat16, use::b, k_tile, n_tile,
		       layout::ext_intel_packed>
	    sub_b[n_k_tile];
	  joint_matrix<sycl::sub_group, float, use::accumulator,
		       m_tile, n_tile> sub_c[n_k_tile];

	  joint_matrix_load (sg, sub_c[0],
			     accC.get_multi_ptr<sycl::access::decorated::no> ()
			       + (sg_startx * m_tile) * N
			       + sg_starty / sg_size * n_tile,
			     N, /* Stride.  */
			     layout::row_major);
	  for (int k = 0; k < n_k_tile; k++)
	    {
	      using namespace sycl::access;
	      joint_matrix_load (sg, sub_a[k],
				 accA.get_multi_ptr<decorated::no> ()
				   + sg_startx * m_tile * K + k * k_tile,
				 K /* Stride.  */);
	      joint_matrix_load (sg, sub_b[k],
				 accB.get_multi_ptr<decorated::no> ()
				   + k * k_tile
				   + sg_starty / sg_size * n_tile * K,
				 K /* Stride.  */);
	    }

	  /* To generate an atomic sequence on ATSM & PVC the resulting matrix
	     has to be independent on the previous iteration.  Otherwise, sync
	     instructions are added after each iteration.  */
	  for (int k = 0; k < n_k_tile; k++)
	    joint_matrix_mad (sg, sub_c[k], sub_a[k], sub_b[k], sub_c[k]);

	  joint_matrix_store (
	    sg, sub_c[0],
	    accC.get_multi_ptr<sycl::access::decorated::no> ()
	      + (sg_startx * m_tile) * N + sg_starty / sg_size * n_tile,
	    N, layout::row_major);
	}); // kernel end
    }).wait ();
}

int
main (int argc, char *argv[])
{
  bfloat16 A[M * K];
  bfloat16 B[K * N];
  float C[M * N] = {};
  float D[M * N] = {};

  /* Row major.  */
  for (int i = 0; i < M; i++)
    {
      for (int j = 0; j < K; j++)
	A[i * K + j] = bfloat16 (1.0f * i);
    }

  /* Fill B with 1.0.  */
  std::fill (std::begin (B), std::end (B), bfloat16 (1.0f));

  sycl::queue deviceQueue{get_sycl_queue (argc, argv)};
  matrix_multiply (C, A, B, deviceQueue);

  /* Verify correct results.  */
  for (int i = 0; i < M; i++)
    {
      for (int j = 0; j < N; j++)
	{
	  float sum = 0.0f;
	  for (int k = 0; k < K; k++)
	    sum += A[i * M + k] * B[k * N + j];

	  D[i * N + j] = sum;
	}
    }
  bool correct
    = std::equal (std::begin (C), std::end (C),
		  std::begin (D), std::end (D), [] (float a, float b)
    {
      return std::fabs (a - b) < 1e-16;
    });
  std::cout << (correct ? "Correct" : "Incorrect") << std::endl;

  return 0;
}
