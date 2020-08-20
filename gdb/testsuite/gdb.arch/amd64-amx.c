/* Test program for AMX registers.

   Copyright 2022 Free Software Foundation, Inc.

   This file is part of GDB.

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

#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <asm/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#define XFEATURE_XTILEDATA 18
#define ARCH_REQ_XCOMP_PERM 0x1023

#define TILE int

#define N1 2
#define K1 3
#define M1 4

#define N2 1
#define K2 2
#define M2 3

uint8_t memA1[N1][4 * K1] = { { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2 },
			      { 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 } };

uint8_t memB1[K1][4 * M1] = {
  { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 },
  { 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4 },
  { 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5 },
};

uint32_t memC1[N1][M1] = { 0 };

uint8_t memA2[N2][4 * K2] = { { 5, 5, 5, 5, 6, 6, 6, 6 } };

uint8_t memB2[K2][4 * M2] = { { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2 },
			      { 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 } };

uint32_t memC2[N2][M2] = { 0 };


/* Data for type testing.  */

int8_t int8_matrix[2][8] = { { -1, -1, -1, -1, 1, 1, 1, 1 },
			     { 1, 1, 1, 1, -5, -5, -5, -5 } };

float fp32_matrix[2][2] = { { 1.0, 1.125 },
			    { 1.25, 1.375 } };


/* This is the bf16 matrix.  But as bf16 is not really a valid data type
in most/any compilers, and as using a library also isn't a good option,
the bytes for this are calculated manually.  This needs to take endianess into
account.

_bfloat16 bf16_matrix[2][2 * 2] =
{ { 0.0, 0.125, 0.25, 0.375 },
  { 0.5, 0.625, 0.75, 0.875 }
};

_bfloat16 bf16_binary_matrix[2][2 * 2] =
{
  { 0000000000000000, 0000000000111110, 0011111010000000, 1100000000111110 },
  { 0000000000111111, 0010000000111111, 0100000000111111, 0110000000111111 }
};

uint8_t bf16_binary_matrix[2][2 * 4] =
{
  { 00000000, 00000000, 00000000, 00111110, 10000000, 00111110, 11000000, 00111110 },
  { 00000000, 00111111, 00100000, 00111111, 01000000, 00111111, 01100000, 00111111 }
};
*/

uint8_t bf16_matrix[2][2 * 4] = { { 0, 0, 0, 62, 128, 62, 192, 62 },
				  { 0, 63, 32, 63, 64, 63, 96, 63 } };

void
tfmaps_calc (int whichMatrix, int N, int K, int M)
{
  int strideA = 4 * K;
  int strideB = 4 * M;
  int strideC = 4 * M;

  /* Configure.  */
  struct tileconfig_t
  {
    uint8_t palette_id;
    uint8_t startRow;
    uint8_t reserved[14];
    uint16_t cols[16];
    uint8_t rows[16];
  };

  struct tileconfig_t tc = { 1 };

  const TILE A = 0;
  const TILE B = 1;
  const TILE C = 2;

  tc.rows[A] = N;
  tc.cols[A] = K * 4;
  tc.rows[B] = K;
  tc.cols[B] = M * 4;
  tc.rows[C] = N;
  tc.cols[C] = M * 4;

  /* Compute.  */
  if (whichMatrix == 1)
    {
      tc.rows[3] = 2;
      tc.cols[3] = 8;
      tc.rows[4] = 2;
      tc.cols[4] = 8;
      tc.rows[5] = 2;
      tc.cols[5] = 8;

      _tile_loadconfig (&tc);

      /* Load additional types for type testing.  */
      _tile_loadd (3, bf16_matrix, 4 * 2);
      _tile_loadd (4, fp32_matrix, 2 * 4);
      _tile_loadd (5, int8_matrix, 4 * 2);

     /* Computation.  */
      _tile_loadd (A, memA1, strideA);
      _tile_loadd (B, memB1, strideB);
      _tile_dpbuud (C, A, B);
      _tile_stored (C, memC1, strideC); /* BP1.  */
    }
  else
    {
      _tile_loadconfig (&tc);
      _tile_loadd (A, memA2, strideA);
      _tile_loadd (B, memB2, strideB);
      _tile_dpbuud (C, A, B);
      _tile_stored (C, memC2, strideC); /* BP2.  */
    }

  _tile_release (); /* BP3.  */
}


int
main (int argc, char **argv)
{
  /* Ask the OS to configure AMX in xsave.  */
  if (syscall (SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA) != 0)
    return -1;

  tfmaps_calc (1, N1, K1, M1);
  tfmaps_calc (2, N2, K2, M2);

  return 0;
}
