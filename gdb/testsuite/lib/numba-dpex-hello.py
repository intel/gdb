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

try:
    import numba_dpex as dpex
    from numba_dpex import Range
    import dpctl
    import dpnp
    import numpy as np
    import numba
except ModuleNotFoundError:
    print ("NUMBA: Python exception ModuleNotFoundError detected!")
    quit ()

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'lib'))


@dpex.kernel
def data_parallel_sum(item, a, b, c):
    i = item.get_id(0)
    c[i] = a[i] + b[i]


def main():
    if len(sys.argv) < 2:
        print(f"Usage: python {sys.argv[0]} <opencl|level_zero|cuda>:<cpu|gpu>:<0|1|...>")
        return

    print("njit:", numba.__version__)
    print("numpy:", np.__version__)
    print("dpex:", dpex.__version__)
    print("dpctl:", dpctl.__version__)

    a = dpnp.array([1234.5], device=sys.argv[1])
    b = dpnp.array([9876.5], device=sys.argv[1])
    global_size = a.size
    c = dpnp.ones_like(a)

    # Submit a Range kernel.
    dpex.call_kernel(data_parallel_sum, Range(global_size), a, b, c)

    if a[0] + b[0] == c[0]:
        print("Hello, NUMBA-DPEX!")

    print("Done...")


if __name__ == '__main__':
    main()
