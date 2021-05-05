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
import numba_dppy as dppy


import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'lib'))
from numba_util import select_dppy_device


@dppy.kernel
def data_parallel_sum(a, b, c):
    i = dppy.get_global_id(0)   # numba-kernel-breakpoint
    l1 = a[i]                   # second-line
    l2 = b[i]                   # third-line
    c[i] = l1 + l2              # fourth-line


def driver(args):
    a = args[0]
    b = args[1]
    c = args[2]
    global_size = args[3]
    data_parallel_sum[global_size, dppy.DEFAULT_LOCAL_SIZE](a, b, c)


def main():
    global_size = 10
    N = global_size

    a = np.array(np.random.random(N), dtype=np.float32)
    b = np.array(np.random.random(N), dtype=np.float32)
    c = np.ones_like(a)

    # Schedule on the queue requested at the command line.
    args = [a, b, c, global_size]
    select_dppy_device(sys.argv, driver, args)

    print("Done...")


if __name__ == "__main__":
    main()
