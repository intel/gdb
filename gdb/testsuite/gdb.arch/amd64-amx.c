/* Test program for AMX registers.

   Copyright 2021 Free Software Foundation, Inc.

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


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <immintrin.h>

#define TILE int

#define N1 2
#define K1 3
#define M1 4

#define N2 1
#define K2 2
#define M2 3

uint8_t memA1[N1][4 * K1] = {
  {0,0,0,0, 1,1,1,1, 2,2,2,2},
  {1,1,1,1, 2,2,2,2, 3,3,3,3}
};

uint8_t memB1[K1][4 * M1] = {
  {0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3},
  {1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4},
  {2,2,2,2, 3,3,3,3, 4,4,4,4, 5,5,5,5},
};

uint32_t memC1[N1][M1] = {0};


uint8_t memA2[N2][4 * K2] = {
  {5,5,5,5, 6,6,6,6}
};

uint8_t memB2[K2][4 * M2] = {
  {0,0,0,0, 1,1,1,1, 2,2,2,2 },
  {1,1,1,1, 2,2,2,2, 3,3,3,3 }
};

uint32_t memC2[N2][M2] = {0};


void
tfmaps_calc (int whichMatrix, int N, int K, int M)
{
  int strideA = 4 * K;
  int strideB = 4 * M;
  int strideC = 4 * M;

  /* Configure */
  struct tileconfig_t
  {
    uint8_t  palette_id;
    uint8_t  startRow;
    uint8_t  reserved[14];
    uint16_t cols[16];
    uint8_t  rows[16];
  };

  struct tileconfig_t tc = {1};

  const TILE A = 0;
  const TILE B = 1;
  const TILE C = 2;

  tc.rows[A] = N;  tc.cols[A] = K * 4;
  tc.rows[B] = K;  tc.cols[B] = M * 4;
  tc.rows[C] = N;  tc.cols[C] = M * 4;

  /* Compute */
  _tile_loadconfig(&tc);

  if (whichMatrix == 1)
  {
    _tile_loadd (A, memA1, strideA);
    _tile_loadd (B, memB1, strideB);
    _tile_dpbuud (C, A, B);
    _tile_stored (C, memC1, strideC); /* BP1.  */
  } else
  {
    _tile_loadd (A, memA2, strideA);
    _tile_loadd (B, memB2, strideB);
    _tile_dpbuud (C, A, B);
    _tile_stored (C, memC2, strideC); /* BP2.  */
  }

  /* Test updated tilecfg.  */
  tc.rows[A] = 1;  tc.cols[A] = 1;
  tc.rows[B] = 1;  tc.cols[B] = 1;
  tc.rows[C] = 1;  tc.cols[C] = 1;

  _tile_loadconfig(&tc);

  _tile_release (); /* BP3.  */
}

int
main (int argc, char **argv)
{
  tfmaps_calc (1, N1, K1, M1);
  tfmaps_calc (2, N2, K2, M2);

  return 0;
}
