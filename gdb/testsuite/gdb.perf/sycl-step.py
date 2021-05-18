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

class Sycl_Step(perftest.TestCaseWithBasicMeasurements):
    def __init__(self, bp_first_line, device_type):
        test_name = "Sycl_Step-" + device_type
        super(Sycl_Step, self).__init__(test_name)
        self.bp_first_line = str(bp_first_line)
        self.bp = None

    def warm_up(self):
        """Set breakpoint inside kernel and set scheduler stepping to
        perf further commands on same thread."""
        self.bp = gdb.Breakpoint(self.bp_first_line)
        gdb.execute("set scheduler-locking step")
        gdb.execute("continue")

    def _do_gdb_step_into_func(self):
        """Run the step command."""
        gdb.execute("step")

    def _do_gdb_step_single_inst(self):
        """Run the stepi command."""
        gdb.execute("stepi")

    def _do_gdb_next(self):
        """Run the next command."""
        gdb.execute("next")

    def execute_test(self):
        self.measure.measure(lambda: self._do_gdb_step_into_func(), 1)
        self.measure.measure(lambda: self._do_gdb_step_single_inst(), 2)
        self.measure.measure(lambda: self._do_gdb_step_single_inst(), 3)
        self.measure.measure(lambda: self._do_gdb_step_single_inst(), 4)
        # Continue at this step will stop gdb at the first line with another
        # thread, because the scheduler-locking is set to step.
        gdb.execute("continue")
        # Step over function.
        self.measure.measure(lambda: self._do_gdb_next(), 5)
        # Step over kernel line.
        self.measure.measure(lambda: self._do_gdb_next(), 6)
        # Step over kernel complex line.
        self.measure.measure(lambda: self._do_gdb_next(), 7)
