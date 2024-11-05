#! /usr/bin/env python3
#
# This testcase is part of GDB, the GNU debugger.
#
# Copyright 2024 Free Software Foundation, Inc.
# Copyright (C) 2024 Intel Corporation
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

import numba_dpex as dpex
from numba_dpex import kernel_api as kapi
from numba_dpex import Range
import dpctl
import dpnp

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'lib'))


@dpex.device_func(debug=True)
def func_sum(a_in_func, b_in_func):
    result = a_in_func                                 # func_line_1
    result = result + b_in_func                        # func_line_2
    return result                                      # func_line_3


@dpex.kernel(debug=True)
def kernel_sum(item : kapi.Item, a_in_kernel, b_in_kernel, c_in_kernel):
    i = item.get_id(0)                                 # numba-kernel-breakpoint
    a = a_in_kernel[i]                                 # kernel_line_2
    c_in_kernel[i] = func_sum(a, b_in_kernel[i])       # kernel_line_3


def main():
    if len(sys.argv) < 2:
        print(f"Usage: python {sys.argv[0]} <opencl|level_zero|cuda>:<cpu|gpu>:<0|1|...>")
        return

    #swich to dpnp array with compute-follow-data
    a = dpnp.array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], device=sys.argv[1])
    b = dpnp.array([11, 12, 13, 14, 15, 16, 17, 18, 19, 20], device=sys.argv[1])
    global_size = a.size
    c = dpnp.ones_like(a)

    # Submit a Range kernel.
    dpex.call_kernel(kernel_sum, Range(global_size), a, b ,c)

    # Verify the output
    for i in range(0, len(c)):
        if c[i] != (a[i] + b[i]):
            print(f"Element {i} is {c[i]} but was expecting {a[i]+b[i]}")
            return

    print("Correct")


if __name__ == "__main__":
    main()
