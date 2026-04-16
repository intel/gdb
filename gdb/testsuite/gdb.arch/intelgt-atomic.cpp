/* Copyright 2024-2026 Free Software Foundation, Inc.
   Copyright (C) 2023-2026 Intel Corporation.

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

#ifdef SG_SIZE
static constexpr uint32_t sg_size = SG_SIZE;
#else
static constexpr uint32_t sg_size = 16;
#endif

#ifndef MATRIX_SIZE
#define MATRIX_SIZE 2048
#endif

#define BF16_EPSILON 0.00781250

static constexpr uint32_t M = MATRIX_SIZE;
static constexpr uint32_t N = MATRIX_SIZE;
static constexpr uint32_t K = MATRIX_SIZE;
static constexpr uint32_t MCache1 = 32;
static constexpr uint32_t NCache1 = 64;
static constexpr uint32_t KCache1 = 16;
static constexpr uint32_t MCache2 = 256;
static constexpr uint32_t NCache2 = 256;
static constexpr uint32_t KCache2 = 32;

static constexpr size_t tM = 8;
static constexpr size_t tN = sg_size;
static constexpr size_t tK = 16;

using namespace sycl;
using namespace sycl::ext::oneapi::experimental::matrix;
namespace syclex = sycl::ext::oneapi::experimental;
using bfloat16 = sycl::ext::oneapi::bfloat16;

const int
get_index (int m2, int mc2, int m1, int mc1, int m, int tM)
{
  return (m2 * mc2 + m1 * mc1 + m * tM);
}

void
fill_matrix (bfloat16 *M, size_t Rows, size_t Cols, int factor)
{
  for (unsigned int i = 0; i < Rows; i++)
    {
      for (unsigned int j = 0; j < Cols; j++)
	M[i * Cols + j] = bfloat16 (1.0f * factor);
    }
}

void
load_mad (unsigned int rowsA, unsigned int colsA,
	  unsigned int rowsB, unsigned int colsB,
	  bfloat16 *A, bfloat16 *B,
	  multi_ptr<bfloat16, sycl::access::address_space::global_space,
		    sycl::access::decorated::no> &pA,
	  multi_ptr<bfloat16, sycl::access::address_space::global_space,
		    sycl::access::decorated::no> &pB,
	  sub_group sg, size_t m2, size_t n2, size_t m1, size_t n1,
	  joint_matrix<sub_group, float, use::accumulator, tM, tN>
	    tC[MCache1 / tM][NCache1 / tN])
{
  size_t sgId = sg.get_group_id ()[0];
  constexpr size_t prefRow = 8;
  constexpr size_t prefCol = 32;
  size_t pm1B = sgId / 8;
  size_t pn1B = sgId & 0x7;
  constexpr size_t prefDistance = 3;

  for (int p = 0; p < prefDistance; p++)
    {
      joint_matrix_prefetch<prefRow, prefCol> (
	sg, A + (m2 * MCache2 + sgId * prefRow) * colsA + p * prefCol, colsA,
	layout::row_major, syclex::properties{syclex::prefetch_hint_L1});
    }

  for (int p = 0; p < prefDistance; p++)
    {
      joint_matrix_prefetch<prefRow, prefCol> (
	sg,
	B + (p * KCache2 + pm1B * prefRow) * colsB + n2 * NCache2
	  + pn1B * prefCol, colsB, layout::row_major,
	syclex::properties{syclex::prefetch_hint_L1});
    }

  for (unsigned int m = 0; m < MCache1 / tM; m++)
    {
      for (unsigned int n = 0; n < NCache1 / tN; n++)
	joint_matrix_fill (sg, tC[m][n], 0);
    }

  for (unsigned int k2 = 0; k2 < colsA / KCache2; k2++)
    {
      joint_matrix<sub_group, bfloat16, use::a, tM, tK, layout::row_major>
	tA[MCache1 / tM][KCache2 / KCache1];
      joint_matrix<sub_group, bfloat16, use::b, tK, tN, layout::row_major>
	tB[NCache1 / tN][KCache2 / KCache1];

      for (unsigned int k1 = 0; k1 < KCache2 / KCache1; k1++)
	{
	  using namespace ext::intel::experimental::matrix;
	  unsigned int k = (k2 * KCache2 + k1 * KCache1) / tK;

	  for (unsigned int m = 0; m < MCache1 / tM; m++)
	    {
	      joint_matrix_load_checked (
		sg, tA[m][k1], pA, colsA, rowsA, colsA,
		get_index ( m2, MCache2, m1, MCache1, m, tM),
		k * tK);
	    }

	  for (unsigned int n = 0; n < NCache1 / tN; n++)
	    {
		joint_matrix_load_checked (
		  sg, tB[n][k1], pB, colsB, rowsB, colsB, k * tK,
		  n2 * NCache2 + n1 * NCache1 + n * tN);

		joint_matrix_load (sg, tB[n][k1],
		  pB + (k * tK) * (colsB)
		    + (n2 * NCache2 + n1 * NCache1 + n * tN),
		  colsB);
	    }

	  for (unsigned int m = 0; m < MCache1 / tM; m++)
	    {
	      for (unsigned int n = 0; n < NCache1 / tN; n++)
		joint_matrix_mad (sg, tC[m][n],
		  tA[m][k1], tB[n][k1], tC[m][n]);
	    }
	}

	auto prefetch_offsetA
	  = (m2 * MCache2 + sgId * prefRow) * colsA
	      + (k2 + prefDistance) * prefCol;

	if ((prefetch_offsetA + (prefRow * colsA) + prefCol) < (rowsA * colsA))
	  {
	    joint_matrix_prefetch<prefRow, prefCol> (
	      sg, A + prefetch_offsetA, colsA, layout::row_major,
	      syclex::properties{syclex::prefetch_hint_L1});
	  }

	auto prefetch_offsetB
	  = ((k2 + prefDistance) * KCache2 + pm1B * prefRow) * (colsB)
	      + (n2 * NCache2 + pn1B * prefCol);

	if ((prefetch_offsetB + (prefRow * colsB) + prefCol) < (rowsB * colsB))
	  {
	    joint_matrix_prefetch<prefRow, prefCol> (
	      sg, B + prefetch_offsetB, colsB, layout::row_major,
	      syclex::properties{syclex::prefetch_hint_L1});
	  }
  }

  return;
}

void
joint_matmul (unsigned int rowsA, unsigned int colsA,
	      unsigned int rowsB, unsigned int colsB,
	      bfloat16 *A, bfloat16 *B, float *C, sycl::queue &q)
{
  range<2> global{rowsA / MCache1, (colsB / NCache1) * sg_size};
  range<2> cachelocal{MCache2 / MCache1, NCache2 / NCache1 * sg_size};

  assert (colsA == rowsB);
  assert (rowsA >= MCache2 && rowsA % tM == 0);
  assert (colsA >= KCache2 && colsA % tK == 0);
  assert (colsB >= NCache2 && colsB % tN == 0);

  for (unsigned int i = 0; i < 2; i++)
    {
      auto mk = q.submit ([&] (handler &h)
	{
	  h.parallel_for<class test_joint_matrix> (
	    nd_range<2>{global, cachelocal}, [=](nd_item<2> it)
	      {
		auto pA
		  = address_space_cast
		      <sycl::access::address_space::global_space,
		       sycl::access::decorated::no>(A);
		auto pB
		  = address_space_cast
		      <sycl::access::address_space::global_space,
		       sycl::access::decorated::no>(B);
		auto pC
		  = address_space_cast
		      <sycl::access::address_space::global_space,
		       sycl::access::decorated::no>(C);

		auto m2 = it.get_group (0);
		auto n2 = it.get_group (1);
		auto m1 = it.get_local_id (0);
		auto n1 = it.get_local_id (1) / sg_size;
		auto sg = it.get_sub_group ();

		joint_matrix<sub_group, float, use::accumulator, tM, tN>
		  tC[MCache1 / tM][NCache1 / tN];

		load_mad (rowsA, colsA, rowsB, colsB,
			  A, B, pA, pB, sg, m2, n2, m1, n1, tC);

		for (unsigned int m = 0; m < MCache1 / tM; m++)
		  {
		    for (unsigned int n = 0; n < NCache1 / tN; n++)
		      {
			using namespace ext::intel::experimental::matrix;
			joint_matrix_store_checked (
			  sg, tC[m][n], pC, colsB, layout::row_major, rowsA,
			  colsB, m2 * MCache2 + m1 * MCache1 + m * tM,
			  n2 * NCache2 + n1 * NCache1 + n * tN);
		      }
		  }
	      });
	});
    }

  q.wait ();
}

bool
verify_result (float *result, float *ref, size_t M, size_t N, size_t K,
	       float floatTol = BF16_EPSILON)
{
  for (unsigned int i = 0; i < M; i++)
    {
      for (unsigned int j = 0; j < N; j++)
	{
	  float a = result[i * N + j];
	  float b = ref[i * N + j];
	  if ((fabs (a - b)) > floatTol)
	    {
	      std::cout << "failed at index " << i << ", " << j << ", res " << a
		<< " != ref " << b << " difference is " << a - b << "\n";
	      return false;
	    }
	}
    }

  return true;
}

int
main (int argc, char *argv[])
{
  sycl::queue deviceQueue{get_sycl_queue (argc, argv)};

  bfloat16 *A = malloc_shared<bfloat16> (M * K, deviceQueue);
  bfloat16 *B = malloc_shared<bfloat16> (K * N, deviceQueue);
  float *C = malloc_shared<float> (M * N, deviceQueue);
  float *refC = malloc_shared<float> (M * N, deviceQueue);

  fill_matrix (A, M, K, 2);
  fill_matrix (B, K, N, 1);

  memset (refC, 0, sizeof (float) * M * N);
  for (unsigned int i = 0; i < M; i++)
    {
      for (unsigned int k = 0; k < K; k++)
	{
	  for (unsigned int j = 0; j < N; j++)
	    refC[i * N + j]
	      += bfloat16 (A[i * K + k]) * bfloat16 (B[k * N + j]);
	}
    }

  joint_matmul (M, K, K, N, A, B, C, deviceQueue);
  bool correct
    = verify_result (C, refC, M, N, K);
  std::cout << (correct ? "Correct" : "Incorrect") << std::endl;

  free (A, deviceQueue);
  free (B, deviceQueue);
  free (C, deviceQueue);
  return 0;
}
