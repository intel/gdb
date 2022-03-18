/* Test program for AMX startrow.

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

#include <errno.h>
#include <immintrin.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <asm/prctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#define XFEATURE_XTILEDATA 18
#define ARCH_REQ_XCOMP_PERM 0x1023

/* To test infcalls.  */
int
square (int a, int b)
{
  int tmp;
  tmp = a * b; /* BP2.  */
  return tmp;
}

int
main (int argc, char **argv)
{
  /* Ask the OS to configure AMX in xsave.  */
  if (syscall (SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA) != 0)
    return -1;

  /* Configure tiles.  */
  struct tileconfig_t
  {
    uint8_t palette_id;
    uint8_t startRow;
    uint8_t reserved[14];
    uint16_t cols[16];
    uint8_t rows[16];
  };

  const int tmm0 = 0;

  struct tileconfig_t tc = { 1 };

  tc.rows[tmm0] = 16;
  tc.cols[tmm0] = 64;

  _tile_loadconfig (&tc);

  const uint32_t memA1[16][16]
    = { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 },
	{ 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47 },
	{ 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63 },
	{ 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79 },
	{ 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95 },
	{ 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
	  110, 111 },
	{ 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124,
	  125, 126, 127 },
	{ 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140,
	  141, 142, 143 },
	{ 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156,
	  157, 158, 159 },
	{ 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172,
	  173, 174, 175 },
	{ 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188,
	  189, 190, 191 },
	{ 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204,
	  205, 206, 207 },
	{ 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220,
	  221, 222, 223 },
	{ 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236,
	  237, 238, 239 },
	{ 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252,
	  253, 254, 255 } };

  /* Load tile that is stored over a page boundary.  */
  const long page_size = sysconf (_SC_PAGESIZE);
  if (page_size == -1)
    return -1;

  void *p;
  int ret = posix_memalign (&p, page_size, 2 * page_size);
  if (ret != 0)
    return -1;

  void *p2 = p + page_size;

  memmove (p2 - 512, memA1, sizeof (memA1));

  /* Protect the second page to produce a fault.  */
  if (mprotect (p2, page_size, PROT_NONE) == -1)
    return -1;

  _tile_loadd (tmm0, p2 - 512, 64); /* BP1.  */

  square (2, 2); /* Jump.  */
  free (p);
  return 0;
}
