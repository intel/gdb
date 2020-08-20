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

#define N 2
#define K 3
#define M 4

uint8_t memA[N][4 * K] = {
  {0,0,0,0, 1,1,1,1, 2,2,2,2},
  {1,1,1,1, 2,2,2,2, 3,3,3,3}
};

uint8_t memB[K][4 * M] = {
  {0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3},
  {1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4},
  {2,2,2,2, 3,3,3,3, 4,4,4,4, 5,5,5,5},
};

uint32_t memC[N][M] = {0};

int
main (int argc, char **argv)
{
  /* Calculate memC.  */
  int strideA = 4 * K;
  int strideB = 4 * M;
  int strideC = 4 * M;

  struct tileconfig_t {
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
  _tile_loadconfig (&tc);
  _tile_loadd (A, memA, strideA);
  _tile_loadd (B, memB, strideB);
  _tile_dpbusd (C, A, B);
  _tile_stored (C, memC, strideC);
  _tile_release (); /* breakpoint here  */

  return 0;
}
