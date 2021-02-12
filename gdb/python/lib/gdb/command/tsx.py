# Copyright (C) 2013-2021 Free Software Foundation, Inc.
#
# This file is part of GDB.
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

# Actual new command "info tsx-abort-reason"
#

import gdb

class info_tsx_abort_reason (gdb.Command):
    """Translates the abort reason encoded in an expression into human readable language."""

    def __init__ (self):
        gdb.Command.__init__ (self, "info tsx-abort-reason", gdb.COMMAND_STATUS)

    def eval_parameter (self, parameter):
        return gdb.parse_and_eval (parameter)

    def print_reason (self, reason, val, nested, retry):
        print("%s%s%s[0x%x]." % (reason, retry, nested, val))

    def print_xaborted (self, xabort, val):
        print("xabort %u, [0x%x]." % (xabort, val))

    def decode_xabort (self, val):
        return (val >> 16) & 0xFF

    def decode (self, val):
        reason = "unknown, "
        retry = ""
        nested = ""
        if val & 1:
            xabort = self.decode_xabort (val)
            self.print_xaborted (xabort, val)
            return
        if val & 2:
            retry = "retry possible, "
        if val & 4:
            reason = "conflict/race, "
        if val & 8:
            reason = "buffer overflow, "
        if val & 16:
            reason = "breakpoint hit, "
        if val & 32:
            nested = "on nested transactions, "
        self.print_reason (reason, val, nested, retry)

    def invoke (self, arg, from_tty):
        if not arg:
            raise gdb.GdbError ("\"info tsx-abort-reason\" command takes one argument.")
        val = self.eval_parameter (arg)
        self.decode (val)

info_tsx_abort_reason ()
