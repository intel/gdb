# Copyright (C) 2024 Free Software Foundation, Inc.

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

# Auto-attach an Intel GPU gdbserver target to GDB when a hook breakpoint
# is reached.

import gdb

class IntelgtHookBreakpoint(gdb.Breakpoint):
    """ The hook breakpoint, which, when hit, we create an intelgt
    gdbserver instance.  """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.silent = True

    def stop(self):
        """Called when the breakpoint is hit.   """

        # Reduce the amount of output caused by context switches.
        info = gdb.execute("show suppress-cli-notifications", False, True)
        was_cli_unsuppressed = info.endswith("off.\n")
        gdb.execute("set suppress-cli-notifications on", False, True)

        # Create a new inferior and connect to the remote server.
        host_inf = gdb.selected_inferior()
        gdb.execute("add-inferior -no-connection", False, True)
        gt_inf = gdb.inferiors()[-1]
        gdb.execute(f"inferior {gt_inf.num}", False, True)
        cmd = f"target remote | gdbserver-intelgt --attach - {host_inf.pid}"
        gdb.execute(cmd)
        gdb.execute(f"inferior {host_inf.num}")

        # Restore the setting.
        if was_cli_unsuppressed:
            gdb.execute("set suppress-cli-notifications off", False, True)

        # Print colored informative messages.
        gdb.write(f"""\033[93m
intelgt: Started gdbserver for process {host_inf.pid} with inferior {gt_inf.num}.
intelgt: It is recommended to do 'set schedule-multiple on'.
intelgt: Type 'continue' to resume.
\033[0m""")

        return True


def handle_new_objfile_event(event):
    """ Create the hook breakpoint when the Level-Zero backend
    library is loaded.  """

    if ('libze_intel_gpu.so' in event.new_objfile.filename):
        hook_bp = IntelgtHookBreakpoint("-qualified zeContextCreate",
                                        type=gdb.BP_BREAKPOINT,
                                        internal=1, temporary=1)

gdb.events.new_objfile.connect(handle_new_objfile_event)
gdb.execute("maint set target-non-stop on")
