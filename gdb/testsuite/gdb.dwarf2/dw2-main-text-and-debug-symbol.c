/* This testcase is part of GDB, the GNU debugger.

   Copyright 2022 Free Software Foundation, Inc.

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

/* Artificial 'main' function - we will rename 'MAIN__' to DW_AT_name = 'main'
   and DW_AT_linkage_name = 'MAIN__' in the testcase's DWARF and think of it as
   the actual 'main' function.  */

int
MAIN__ ()
{
  asm ("MAIN___label: .globl MAIN___label");
  return 0;
}

int
main ()
{
  return MAIN__ ();
}
