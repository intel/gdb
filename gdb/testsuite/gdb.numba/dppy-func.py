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
import numba_dppy as dppy
import dpctl


import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'lib'))
from numba_util import select_dppy_device


@dppy.func(debug=True)
def func_sum(a_in_func, b_in_func):
    result = 0                                                # func_line_1
    result = a_in_func + b_in_func                            # func_line_2
    return result                                             # func_line_3


@dppy.kernel(debug=True)
def kernel_sum(a_in_kernel, b_in_kernel, c_in_kernel):
    i = dppy.get_global_id(0)                                 # numba-kernel-breakpoint
    c_in_kernel[i] = func_sum(a_in_kernel[i], b_in_kernel[i]) # kernel_line_2


def driver(args):
    a = args[0]
    b = args[1]
    c = args[2]
    global_size = args[3]
    kernel_sum[global_size, dppy.DEFAULT_LOCAL_SIZE](a, b, c)


def main():
    a = np.array([1234.5, 1234.5, 1234.5, 1234.5, 1234.5, 1234.5, 1234.5, 1234.5, 1234.5, 1234.5])
    b = np.array([9876.5, 9876.5, 9876.5, 9876.5, 9876.5, 9876.5, 9876.5, 9876.5, 9876.5, 9876.5])
    global_size = np.size(a)
    c = np.ones_like(a)

    # Schedule on the queue requested at the command line.
    args = [a, b, c, global_size]
    select_dppy_device(sys.argv, driver, args)


if __name__ == "__main__":
    main()
