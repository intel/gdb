/* This testcase is part of GDB, the GNU debugger.

   Copyright 2025 Free Software Foundation, Inc.

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

int __attribute__ ((noinline))
bar (int x)
{
  return x + x;
}

int __attribute__ ((noinline))
foo (int a, int b, int c, int d)
{
  a += bar (a) + bar (b) + bar (c) + bar (d);
  return a;
}

int
main (int argc, char **argv)
{
  int a = foo (argc, argc + 1, argc + 2, argc * 2);
  return a;
}
