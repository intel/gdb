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

class SyclThread(perftest.TestCaseWithBasicMeasurements):
    def __init__(self, bp_last_line, device_type):
        test_name = "Sycl_Thread-" + device_type
        super(SyclThread, self).__init__(test_name)
        self.bp_last_line = str(bp_last_line)
        self.bp = None

    def warm_up(self):
        """Set breakpoint inside kernel."""
        self.bp = gdb.Breakpoint(self.bp_last_line)

    def _do_thread_info(self):
        """Run the thread info command."""
        gdb.execute("info thread")

    def _do_thread_apply(self):
        """Run the thread apply all command and wait for the output."""
        gdb.execute("thread apply all print /x $ip")

    def _do_gdb_interrupt(self):
        """Continue in background afterwards send the interrupt command."""
        gdb.execute("continue&")
        gdb.execute("interrupt")

    def execute_test(self):
        self.measure.measure(lambda: self._do_thread_info(), 1)
        # Continue to invalidate the regcache for the next thread info command.
        gdb.execute("continue")
        self.measure.measure(lambda: self._do_thread_apply(), 2)
        self.measure.measure(lambda: self._do_gdb_interrupt(), 3)
