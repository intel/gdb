/* This testcase is part of GDB, the GNU debugger.

   Copyright 2024 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Source code has intentionally been formatted not according to GNU style
   to show issues with breakpoint propagation/sliding.  */

/* break here one.  */
void one () { int var = 0;
}

/* break here two.  */
void two () { {int var = 0;} } /* func line two.  */

/* break here three.  */
void three () { /* func line three.  */
 {int var = 0;} /* func body three.  */
}

int
main (void)
{
  one ();
  two ();
  three ();

  return 0;
}
