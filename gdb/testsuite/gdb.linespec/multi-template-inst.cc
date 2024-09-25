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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

template <typename T>
int
func (T t)
{
  return t; /* Show here.  */
}

template <>
int
func<char> (char t)
{
  return t + 1; /* One location.  */
}

int
main (void)
{
  return func<int> (1) + func<float> (1.0f) + func<char> (1) + func<short> (1)
	 + func<bool> (1);
}
