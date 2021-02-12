# Copyright (C) 2019-2021 Free Software Foundation, Inc.

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

"""Auto-attach to debug kernels offloaded to Intel GPU

The python script allows the debugger to spawn an instance of
'gdbserver-gt' to listen to debug events from the GPU.  This is done
to ensure debuggability in case the kernel is offloaded to GPU.  The
auto-attach feature is turned on by default, and it is harmless to the
debugging capability on the CPU device.

The auto-attach feature can be disabled by setting the OS environment
variable 'INTELGT_AUTO_ATTACH_DISABLE' to a non-zero value.  It can be
also set or unset in the inferior's environment, so that the user can
disable or enable auto-attach in the same gdb session.
'DISABLE_AUTO_ATTACH' is marked as deprecated and ignored.

By default, it is expected that the 'gdbserver-gt' executable exists
in the PATH.  Below are customization options via environment
variables.

 * To use a 'gdbserver-gt' that resides in a specific directory <DIR>,
   set the INTELGT_AUTO_ATTACH_GDBSERVER_GT_PATH environment variable
   to <DIR>.

 * To use a specific name for the gdbserver binary name instead of
   'gdbserver-gt', set the INTELGT_AUTO_ATTACH_GDBSERVER_GT_BINARY
   environment variable to the desired value.

 * The content of the INTELGT_AUTO_ATTACH_IGFXDBG_LIB_PATH environment
   variable is prepended to the LD_LIBRARY_PATH environment variable
   when spawning 'gdbserver-gt'.

 * For remote-debugging scenarios, 'gdbserver-gt' is spawned in the
   remote machine, via the 'ssh -T' protocol and using the current
   username.  The remote machine address is extracted from GDB.

   - To change the protocol, set the INTELGT_AUTO_ATTACH_REMOTE_PROTOCOL
     environment variable to the desired value.

   - To change the username, set the INTELGT_AUTO_ATTACH_REMOTE_USERNAME
     environment variable to the desired value.

   - To change the remote machine address, set the
     INTELGT_AUTO_ATTACH_REMOTE_TARGET environment variable to the
     desired value.
"""

import gdb

# Breakpoint function for executing the auto-attach.
BP_FCT = "igfxdbgxchgDebuggerHook"

class IntelgtAutoAttach(gdb.Breakpoint):
    """Class to debug kernels offloaded to Intel GPU"""
    def __init__(self):
        """Registration of new loaded object file handler event."""
        gdb.events.new_objfile.connect(self.handle_new_objfile_event)
        gdb.events.quit.connect(self.handle_quit_event)
        gdb.events.exited.connect(self.handle_exited_event)
        gdb.events.before_prompt.connect(self.handle_before_prompt_event)
        self.inf_dict = {}
        self.host_inf_for_auto_remove = None
        self.is_nonstop = False
        self.hook_bp = None
        self.gdbserver_gt_binary = "gdbserver-gt"

    @staticmethod
    def get_env_variable(var, default=None):
        """Helper function to get environment variable value."""
        env_var = gdb.execute(f"show env {var}", to_string=True)
        if env_var.find(f"Environment variable \"{var}\" " \
                         "not defined.") != -1:
            return default
        return env_var.replace(f"{var} = ", "", 1).strip()

    def is_auto_attach_disabled(self):
        """Helper function to check if auto-attach has been disabled via
        os environment variable or in inferior's environment."""
        if self.get_env_variable("DISABLE_AUTO_ATTACH") is not None:
            print("""\
intelgt: env variable 'DISABLE_AUTO_ATTACH' is deprecated.  Use
'INTELGT_AUTO_ATTACH_DISABLE' instead.""")
        env_value = self.get_env_variable("INTELGT_AUTO_ATTACH_DISABLE")
        return not(env_value is None or env_value == "0")

    def handle_new_objfile_event(self, event):
        """Handler for a new object file load event.  If auto-attach has
        not been disabled, set a breakpoint at location 'BP_FCT'."""
        if event is None:
            return

        if not 'libigfxdbgxchg64.so' in event.new_objfile.filename:
            return

        if not self.is_auto_attach_disabled():
            self.hook_bp = gdb.Breakpoint(BP_FCT, type=gdb.BP_BREAKPOINT,
                                          internal=1, temporary=0)
            self.hook_bp.silent = True
            commands = ("python gdb.function.intelgt_auto_attach" +
                        ".INTELGT_AUTO_ATTACH.init_gt_inferior()")
            # Find out if we are in nonstop mode.  It is safe to do it
            # here and only once, because the setting cannot be
            # changed after the program starts executing.
            nonstop_info = gdb.execute("show non-stop", False, True)
            self.is_nonstop = nonstop_info.endswith("on.\n")

            if self.is_nonstop:
                self.hook_bp.commands = commands + "\ncontinue -a"
            else:
                self.hook_bp.commands = commands + "\ncontinue"

    def remove_gt_inf_for(self, host_inf):
        """First detach from, then remove the gt inferior that was created
        for HOST_INF."""
        gt_inf = self.inf_dict[host_inf]
        if gt_inf is None:
            return

        ate_error = False
        if gt_inf == gdb.selected_inferior():
            # Switch to host to be able to detach/remove gt_inf.
            # Catch exceptions, since the switch will likely raise an error
            # because the host inf also exited.
            ate_error = self.protected_gdb_execute("inferior %d" % host_inf.num,
                                                   True)

        if gt_inf.pid != 0:
            # Detach before removing.
            gdb.execute("detach inferiors %d" % gt_inf.num, False, True)

        # Save the inf num for logging after removal.
        gt_inf_num = gt_inf.num
        self.protected_gdb_execute("remove-inferiors %d" % gt_inf_num)
        print("intelgt: inferior %d (%s) has been removed."
              % (gt_inf_num, self.gdbserver_gt_binary))

        # Remove the internal track.
        self.inf_dict[host_inf] = None

        if ate_error:
            # The error seen and eaten up by the switch to the host inf
            # indicates that the host inf exited but the GT inf's exit
            # event arrived first.  Because we already removed the GT
            # inf, it does not make sense to stay at a seemingly live
            # but in fact dead host inf.  Just continue to consume the
            # pending exit event.
            self.protected_gdb_execute("continue")

    @staticmethod
    def protected_gdb_execute(cmd, suppress_output=False):
        """Execute the GDB command CMD.  Return True if a 'Couldn't get
        registers: No such process.' error was seen and eaten up; False
        if no GDB error occured."""
        try:
            gdb.execute(cmd, False, suppress_output)
            return False
        except gdb.error as ex:
            if str(ex) == "Couldn't get registers: No such process.":
                # This exception occurs if the gt inferior exit event
                # is received before the host inferior exit event.
                # At this point, host inf also exited; GDB just has not
                # shown this to the user yet.  We just have to swallow
                # the error and tell this to the caller.
                return True
            raise

    def handle_before_prompt_event(self):
        """Handler for GDB's 'before prompt' event.  Remove a gt inferior
        if such an inferior was stored for removal."""
        if self.host_inf_for_auto_remove is None:
            return

        host_inf = self.host_inf_for_auto_remove
        self.remove_gt_inf_for(host_inf)
        self.host_inf_for_auto_remove = None

    def handle_exited_event(self, event):
        """Indication that an inferior's process has exited.
        Save the host inferior to remove later the GT inferior that
        was associated with it."""
        if event is None:
            return

        if not self.host_inf_for_auto_remove is None:
            return

        if event.inferior in self.inf_dict.keys():
            if not self.is_nonstop:
                # Don't mark this in nonstop mode because the gt inf's
                # exit event will also arrive.  Events are async.
                if not self.inf_dict[event.inferior] is None:
                    self.host_inf_for_auto_remove = event.inferior
        elif event.inferior in self.inf_dict.values():
            for key, value in self.inf_dict.items():
                if event.inferior == value:
                    self.host_inf_for_auto_remove = key

    def handle_quit_event(self, event):
        """Handler for GDB's quit event."""
        if event is None:
            return

        inf_nums = " ".join(str(inf.num)
                            for inf in self.inf_dict.values() if inf is not None)
        if inf_nums:
            gdb.execute("detach inferior " + inf_nums)

    def handle_error(self, inf):
        """Remove the inferior and clean-up dict in case of error."""
        print("""\
intelgt: gdbserver-gt failed to start.  Check if igfxdcd is installed, or use
env variable INTELGT_AUTO_ATTACH_DISABLE=1 to disable auto-attach.""")
        gdb.execute("inferior %d" % inf.num, False, True)
        gdb.execute("remove-inferior %d" % self.inf_dict[inf].num)
        self.inf_dict[inf] = None

    @staticmethod
    def get_connection_str(var):
        """Helper function to get the string used on a connection over stdio
        pipe. Note that not all corner case are covered, e.g. if 'gdbserver'
        is in the hostname or protocol options' value."""
        found = False
        connection_str = ''
        for val in var.split():
            if "gdbserver" in val:
                found = True
                break
            connection_str += f"{val} "
        if not found:
            connection_str = None
        return connection_str

    def handle_attach_gdbserver_gt(self, connection, inf):
        """Attach gdbserver to gt inferior either locally or remotely."""
        # check 'INTELGT_AUTO_ATTACH_GDBSERVER_GT_PATH' in the inferior's
        # environment.
        gdbserver_gt_path_env_var = \
          self.get_env_variable("INTELGT_AUTO_ATTACH_GDBSERVER_GT_PATH")

        self.gdbserver_gt_binary = \
          self.get_env_variable("INTELGT_AUTO_ATTACH_GDBSERVER_GT_BINARY",
                                "gdbserver-gt")

        if gdbserver_gt_path_env_var:
            gdbserver_gt_attach_str = \
              "%s/%s --attach - %d" \
              % (gdbserver_gt_path_env_var, self.gdbserver_gt_binary, inf.pid)
        else:
            gdbserver_gt_attach_str = "%s --attach - %d" \
                                      % (self.gdbserver_gt_binary, inf.pid)

        # check 'INTELGT_AUTO_ATTACH_IGFXDBG_PATH' in the inferior's
        # environment.
        igfxdbg_lib_path_env_var = \
          self.get_env_variable("INTELGT_AUTO_ATTACH_IGFXDBG_LIB_PATH")
        ld_lib_path_str = ""
        if igfxdbg_lib_path_env_var:
            ld_lib_path_str = \
                f"LD_LIBRARY_PATH={igfxdbg_lib_path_env_var}:$LD_LIBRARY_PATH"

        if connection == 'native':
            self.make_native_gdbserver(inf, ld_lib_path_str,
                                       gdbserver_gt_attach_str)
        elif connection == 'remote':
            self.make_remote_gdbserver(inf, ld_lib_path_str,
                                       gdbserver_gt_attach_str)

    def make_native_gdbserver(self, inf, ld_lib_path, gdbserver_cmd):
        """Spawn and connect to a native instance of gdbserver-gt."""
        # Switch to the gt inferior.
        gdb.execute("inferior %d" % (self.inf_dict[inf].num), False, True)
        gdb.execute("set sysroot /")
        target_remote_str = " ".join(("%s %s" % (ld_lib_path,
                                                 gdbserver_cmd)).split())
        connection_output = gdb.execute("target remote | %s"
                                        % target_remote_str, False, True)
        # Print the first line only.  It contains useful device id and
        # architecture info.
        print(connection_output.split("\n")[0])
        print("intelgt: inferior %d (gdbserver-gt) created for "
              "process %d." % (self.inf_dict[inf].num, inf.pid))

    def make_remote_gdbserver(self, inf, ld_lib_path, gdbserver_cmd):
        """Spawn and connect to a remote instance of gdbserver-gt."""
        # Check 'INTELGT_AUTO_ATTACH_REMOTE_TARGET' in the inferior's
        # environment.
        remote_target_env_var = self.get_env_variable(
            "INTELGT_AUTO_ATTACH_REMOTE_TARGET")

        # Check 'INTELGT_AUTO_ATTACH_REMOTE_USERNAME' in the inferior's
        # environment.
        remote_username_str = self.get_env_variable(
            "INTELGT_AUTO_ATTACH_REMOTE_USERNAME", "")
        if remote_username_str != "":
            remote_username_str += "@"

        # Check 'INTELGT_AUTO_ATTACH_REMOTE_PROTOCOL' in the inferior's
        # environment.
        remote_protocol_str = self.get_env_variable(
            "INTELGT_AUTO_ATTACH_REMOTE_PROTOCOL", "ssh -T")

        # Switch to the gt inferior.
        gdb.execute("inferior %d" % (self.inf_dict[inf].num), False, True)

        if not remote_target_env_var:
            connection_target = inf.connection_target
            if connection_target is None:
                # Connection over stdio pipe.
                target_str = self.get_connection_str(inf.connection_string)
                if target_str is None:
                    print("intelgt: could not get connection string.")
                    self.handle_error(inf)
                    return
            else:
                # Connection over tcp.
                target_str = f"{remote_protocol_str} " \
                             f"{remote_username_str}{connection_target}"
                gdb.execute("set remotetimeout 20")
        else:
            # Connection over stdio pipe or tcp.
            target_str = f"{remote_protocol_str} " \
                         f"{remote_username_str}{remote_target_env_var}"
            gdb.execute("set remotetimeout 20")

        target_remote_str = \
            " ".join((f"{target_str} {ld_lib_path} {gdbserver_cmd}").split())

        gdb.execute("target remote | %s" % target_remote_str)
        print("""\
intelgt: inferior %d created remotely for process %d using command
'%s'""" % (self.inf_dict[inf].num, inf.pid, target_remote_str))

    def init_gt_inferior(self):
        """Create/initialize a second inferior with gdbserver-gt connection"""
        # get the host inferior.
        host_inf = gdb.selected_inferior()

        connection = host_inf.connection_shortname
        if connection not in ('remote', 'native'):
            print(f"intelgt: connection name '{connection}' not recognized.")
            self.handle_error(host_inf)
            return

        self.hook_bp.delete()

        if host_inf not in self.inf_dict or self.inf_dict[host_inf] is None:
            if not self.is_nonstop:
                gdb.execute("set schedule-multiple off")
            gdb.execute("add-inferior -no-connection", False, True)
            self.inf_dict[host_inf] = gdb.inferiors()[-1]

            # attach gdbserver-gt to gt inferior.
            try:
                self.handle_attach_gdbserver_gt(connection, host_inf)
            except gdb.error:
                self.handle_error(host_inf)
                return

            # switch to host inferior.
            gdb.execute(f"inferior {host_inf.num}", False, True)
            if not self.is_nonstop:
                gdb.execute("set schedule-multiple on")

INTELGT_AUTO_ATTACH = IntelgtAutoAttach()
