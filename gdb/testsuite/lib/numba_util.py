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

# Utility file for NUMBA test programs to enable explicit selection of
# the device.

import dpctl


def select_dppy_device(argv, func, func_args):
    """Schedules the given function in the given queue.

    :param argv: Command line arguments (sys.argv)
    :param func: Function to schedule
    :param func_argc: Argument to pass to the function
    :return: Return value of the func, or -1 if failed to schedule.
    """

    device = None
    if 1 < len(argv):
        if argv[1] == "cpu":
            device = dpctl.select_cpu_device()
        elif argv[1] == "gpu":
            device = dpctl.select_gpu_device()
        elif argv[1] == "accelerator":
            device = dpctl.select_accelerator_device()

    if device is not None:
        print("Target device ...")
        device.print_device_info()
        with dpctl.device_context(device):
            return func(func_args)
        print("Failed to schedule")
    else:
        print(f"Usage: python {argv[0]} <cpu|gpu|accelerator>")

    return -1
