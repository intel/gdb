#! /usr/bin/env python3
#
# This testcase is part of GDB, the GNU debugger.
#
# Copyright 2021-2023 Free Software Foundation, Inc.
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

try:
    import numpy as np
    import numba_dpex as dpex
    import numba
    import dpctl
except ModuleNotFoundError:
    print ("NUMBA: Python exception ModuleNotFoundError detected!")
    quit ()

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'lib'))
from numba_util import select_dpex_device


@dpex.kernel
def data_parallel_sum(a, b, c):
    i = dpex.get_global_id(0)
    c[i] = a[i] + b[i]


def driver(args):
    a = args[0]
    b = args[1]
    c = args[2]
    global_size = args[3]
    data_parallel_sum[global_size, dpex.DEFAULT_LOCAL_SIZE](a, b, c)
    if a[0] + b[0] == c[0]:
        print("Hello, NUMBA-DPEX!")


def main():
    print("njit:", numba.__version__)
    print("numpy:", np.__version__)
    print("dpex:", dpex.__version__)
    print("dpctl:", dpctl.__version__)

    global_size = 1
    N = global_size

    a = np.array(np.random.random(N), dtype=np.float32)
    b = np.array(np.random.random(N), dtype=np.float32)
    c = np.ones_like(a)

    # Schedule on the queue requested at the command line.
    args = [a, b, c, global_size]
    select_dpex_device(sys.argv, driver, args)

    print("Done...")


if __name__ == '__main__':
    main()
