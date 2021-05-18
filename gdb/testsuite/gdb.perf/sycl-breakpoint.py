# Copyright (C) 2021 Free Software Foundation, Inc.

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

from perftest import perftest

class Sycl_Breakpoint(perftest.TestCaseWithBasicMeasurements):
    def __init__(self, bp_last_line, device_type):
        test_name = "Sycl_Breakpoint-" + device_type
        super(Sycl_Breakpoint, self).__init__(test_name)
        self.bp_last_line = str(bp_last_line)
        self.bp = None

    def warm_up(self):
        """Set breakpoint inside kernel."""
        self.bp = gdb.Breakpoint(self.bp_last_line)

    def _do_continue_to_bp(self):
        """Run the command and wait for the output."""
        gdb.execute("continue")

    def execute_test(self):
        self.measure.measure(lambda: self._do_continue_to_bp(), 1)
        self.bp.condition = "(dim0 == 0 || dim0 == 256 || dim0 == 533)"
        self.measure.measure(lambda: self._do_continue_to_bp(), 2)
        self.measure.measure(lambda: self._do_continue_to_bp(), 3)
