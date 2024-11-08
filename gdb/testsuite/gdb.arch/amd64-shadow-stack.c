/* This testcase is part of GDB, the GNU debugger.

 Copyright 2018-2024 Free Software Foundation, Inc.
 Copyright (C) 2019-2024 Intel Corporation

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

static int
call2 ()
{
  return 42; /* break call2.  */
}

static int
call1 ()
{
  return call2 (); /* break call1.  */
}

int
main ()
{
  call1 (); /* break main.  */
  return 0;
}
