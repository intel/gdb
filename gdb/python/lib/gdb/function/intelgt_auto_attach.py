# Copyright (C) 2024-2026 Free Software Foundation, Inc.
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

# Auto-attach an Intel GPU gdbserver target to GDB when a hook breakpoint
# is reached.

import gdb


def get_env_variable(var, default=None):
    """Helper function to get environment variable value."""
    env_var = gdb.execute(f"show env {var}", to_string=True)
    if env_var.find(f'Environment variable "{var}" ' "not defined.") != -1:
        return default
    return env_var.replace(f"{var} = ", "", 1).strip()


class IntelgtHookBreakpoint(gdb.Breakpoint):
    """The hook breakpoint, which, when hit, we create an intelgt
    gdbserver instance."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.attached = False

    def attach(self):
        # Reduce the amount of output caused by context switches.
        info = gdb.execute("show suppress-cli-notifications", False, True)
        was_cli_unsuppressed = info.endswith("off.\n")
        gdb.execute("set suppress-cli-notifications on", False, True)

        # We need to switch back again.
        host_thread = gdb.selected_thread()

        # Create a new inferior and connect to the remote server.
        gdb.execute("add-inferior -no-connection", False, True)
        gt_inf = gdb.inferiors()[-1]
        gdb.execute(f"inferior {gt_inf.num}", False, True)

        # Env variable to pass custom flags to gdbserver such as
        # "--debug" and "--remote-debug" for debugging purposes.
        args = get_env_variable("INTELGT_AUTO_ATTACH_GDBSERVER_ARGS", " ")

        gdb.execute(
            f"target extended-remote | gdbserver-intelgt {args} --multi --once -",
            False,
            True,
        )
        gdb.execute(f"attach {host_thread.inferior.pid}", False, True)

        # We attached.  Prevent further attempts on subsequent instances
        # of this breakpoint.
        self.attached = True

        # Restore original state.
        host_thread.switch()
        if was_cli_unsuppressed:
            gdb.execute("set suppress-cli-notifications off", False, True)

        # This is almost essential when dealing with separate GPU inferiors.
        gdb.execute("set schedule-multi on", False, True)

    def stop(self):
        """Called when the breakpoint is hit."""

        # Since we don't stop, the breakpoint never triggers, but we must
        # still attach only once.
        if not self.attached:
            self.attach()

        # Don't stop.  We just wanted to trigger the attach.
        return False


def handle_new_objfile_event(event):
    """Create the hook breakpoint when the Level-Zero backend
    library is loaded."""

    if "libze_intel_gpu.so" in event.new_objfile.filename:
        IntelgtHookBreakpoint(
            "-qualified zeModuleCreate",
            type=gdb.BP_BREAKPOINT,
            internal=1,
        )


AUTO_ATTACH_DISABLED = get_env_variable("INTELGT_AUTO_ATTACH_DISABLE", "0")
if AUTO_ATTACH_DISABLED != "1":
    gdb.events.new_objfile.connect(handle_new_objfile_event)
