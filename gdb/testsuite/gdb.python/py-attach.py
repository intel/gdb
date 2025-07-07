# Copyright (C) 2025 Free Software Foundation, Inc.

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

# This file is part of the GDB testsuite.  It tests python post attach
# event.

import gdb


def continue_handler(event):
    """Test continue event handler."""
    assert isinstance(event, gdb.ContinueEvent)
    print("event type: continue")


def post_attach_handler(event):
    """Test attach event handler."""
    assert isinstance(event, gdb.PostAttachEvent)
    print("event type: post-attach")
    assert event.inferior
    print(f"post-attach: Received for inferior {event.inferior.num}")


class test_attach_events(gdb.Command):
    """Test attach events."""

    def __init__(self):
        gdb.Command.__init__(self, "test-attach-events", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        gdb.events.cont.connect(continue_handler)
        gdb.events.post_attach.connect(post_attach_handler)
        print("Attach events registered.")


test_attach_events()
