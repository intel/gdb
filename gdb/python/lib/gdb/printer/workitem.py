# Pretty-printers for workitem coordinates _gdb_workitem.
# Copyright (C) 2023-2024 Free Software Foundation, Inc.

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

import sys

import gdb.printing


class WorkitemPrinter:
    """Prints workitem coordinates with their names."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        return "{<x>: %d, <y>: %d, <z>: %d}" % (self.val[0], self.val[1], self.val[2])

gdb.printing.add_builtin_pretty_printer(
    "workitem", "^_gdb_workitem", WorkitemPrinter
)
