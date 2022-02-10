#! /usr/bin/env python3
#
# This testcase is part of GDB, the GNU debugger.
#
# Copyright 2022 Free Software Foundation, Inc.
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
from numba import njit


@njit(debug=True)
def func_sum(a_in_func, b_in_func):
    result = a_in_func                                            # func_line_1
    result = result + b_in_func                                   # func_line_2
    return result                                                 # func_line_3


@njit(debug=True)
def kernel_sum(a_in_kernel, b_in_kernel, size):
    c_in_kernel = np.empty_like(a_in_kernel)                      # numba-kernel-breakpoint
    for i in range(size):                                         # kernel_line_2
        a = a_in_kernel[i]                                        # kernel_line_3
        c_in_kernel[i] = func_sum(a, b_in_kernel[i])              # kernel_line_4
    return c_in_kernel                                            # kernel_line_5


def main():
    a = np.array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
    b = np.array([11, 12, 13, 14, 15, 16, 17, 18, 19, 20])
    global_size = np.size(a)
    c = np.ones_like(a)

    c = kernel_sum(a, b, global_size)


if __name__ == '__main__':
    main()
