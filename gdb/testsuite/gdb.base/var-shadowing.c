/* Copyright (C) 2021 Free Software Foundation, Inc.

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

/* This is only ever run if it is compiled with a new-enough GCC, but
   we don't want the compilation to fail if compiled by some other
   compiler.  */

#include <stdlib.h>

void
shadowing ()
{
  int a;
  unsigned int val1 = 1;
  unsigned int val2 = 2;
  a = 101;  /* bp for locals 1 */
  {
    unsigned int val2 = 3;
    unsigned int val3 = 4;
    a = 102;  /* bp for locals 2 */
    {
      unsigned int val1 = 5;
      a = 103;  /* bp for locals 3 */
      {
	unsigned int val1 = 6;
	unsigned int val2 = 7;
	unsigned int val3 = 8;
	a = 104;  /* bp for locals 4 */
      }
    }
  }
  a = 0; /* bp for locals 5 */
}

int
main (void)
{
  shadowing ();
  return 0;
}
