/* Test program for FP16 of AMX registers.

   Copyright 2023 Free Software Foundation, Inc.

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

#include <string.h>
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

#define N 2
#define K 8
#define M 4

_Float16 memA[N][K] = {
  {0.0, 0, 0.125, 0, 0.25, 0, 1, 0},
  {0.375, 0, 0.5, 0, 0.625, 0, 1, 0}
};

_Float16 memB[M][K] = {
  {0.0, 0.125, 0.25, 0.375, 1, 1, 1, 1},
  {0.5, 0.625, 0.75, 0.875, 1, 1, 1, 1},
  {1.0, 1.125, 1.25, 1.375, 1, 1, 1, 1},
  {1.0, 1.125, 1.25, 1.375, 1, 1, 1, 1}
};

_Float16 memC[N][M] = {
  {0.0, 0.0, 0.0, 0.0},
  {0.0, 0.0, 0.0, 0.0}
};

int
main (int argc, char **argv)
{
  /* Ask the OS to configure AMX in xsave.  */
  if (syscall (SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA))
    return -1;

  int strideA = 2*K;
  int strideB = 2*K;
  int strideC = 2*K;

  /* Configure.  */
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

  tc.rows[A] = N;  tc.cols[A] = 2*K;
  tc.rows[B] = M;  tc.cols[B] = 2*K;
  tc.rows[C] = N;  tc.cols[C] = 2*K;

  /* Compute.  */
  _tile_loadconfig(&tc);

  _tile_loadd (0, memA, strideA);
  _tile_loadd (1, memB, strideB);
  _tile_dpfp16ps (2, 0, 1);
  _tile_stored (2, memC, strideC); /* BP1.  */

  _tile_release ();

  return 0;
}
