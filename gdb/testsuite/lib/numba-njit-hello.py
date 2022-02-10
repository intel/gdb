#! /usr/bin/env python3
#
# This testcase is part of GDB, the GNU debugger.
#
# Copyright 2021 Free Software Foundation, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import numpy as np
import numba
from numba import njit


@njit
def func_sum(a_in_func, b_in_func):
    result = a_in_func + b_in_func
    return result


@njit
def kernel_sum(a_in_kernel, b_in_kernel, size):
    c_in_kernel = np.empty_like(a_in_kernel)
    for i in range(size):
        c_in_kernel[i] = func_sum(a_in_kernel[i], b_in_kernel[i])
    return c_in_kernel


def main():
    print("njit:", numba.__version__)
    print("numpy:", np.__version__)

    global_size = 1
    a = np.arange(global_size, dtype=np.float32)
    b = np.arange(global_size, dtype=np.float32)
    c = np.empty_like(a)

    c = kernel_sum(a, b, global_size)
    if a[0] + b[0] == c[0]:
        print("Hello, NUMBA njit!")

    print("Done...")


if __name__ == '__main__':
    main()
