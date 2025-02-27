/* Copyright 2025 Free Software Foundation, Inc.

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

int
add_one (int a)
{
  return ++a; /* cond-bp-line.  */
}

int
foo (int a)
{
  return a + add_one (a);
}

int
bar (int a)
{
  return a + 100;
}

int
baz (int a)
{
  return --a;
}

int
main ()
{

  int a = 10;
  int b = add_one (a);
  b += foo (b);
  b = bar (b) + baz (b);

  return 0;
}
