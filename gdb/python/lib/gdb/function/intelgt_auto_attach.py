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

import subprocess
import gdb

# Breakpoint function for executing the auto-attach.
BP_FCT = "igfxdbgxchgDebuggerHook"

# Function call to query the number of devices.
NUM_DEVICES_FCT = "(int)igfxdbgxchgNumDevices()"

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
                        ".INTELGT_AUTO_ATTACH.init_gt_inferiors()")
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
        """First detach from, then remove the gt inferiors that were created
        for HOST_INF."""
        gt_connection = self.inf_dict[host_inf]
        if gt_connection is None:
            return

        # Find all the gt infs whose connection is the same as 'gt_connection'.
        gt_infs = []
        for an_inf in gdb.inferiors():
            if an_inf.connection_num == gt_connection:
                gt_infs.append(an_inf)

        # First, detach all gt inferiors.
        for gt_inf in gt_infs:
            if gt_inf.pid != 0:
                gdb.execute(f"detach inferiors {gt_inf.num}", False, True)

        # Terminate the gdbserver-gt session.  We should do this from a
        # gt inf.
        if gdb.selected_inferior() not in gt_infs:
            cmd = f"inferior {gt_infs[0].num}"
            self.protected_gdb_execute(cmd, True)
        gdb.execute("monitor exit")

        # Switch to host to be able to remove gt infs.
        # Catch exceptions, since the switch will likely raise
        # an error because the host inf also exited.
        ate_error = False
        if host_inf != gdb.selected_inferior():
            cmd = f"inferior {host_inf.num}"
            ate_error = self.protected_gdb_execute(cmd, True)

        # Now remove the gt inferiors.
        for gt_inf in gt_infs:
            gt_inf_num = gt_inf.num
            self.protected_gdb_execute(f"remove-inferiors {gt_inf_num}")
            print(f"intelgt: inferior {gt_inf_num} "
                  f"({self.gdbserver_gt_binary}) has been removed.")

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
        else:
            for key, gt_connection in self.inf_dict.items():
                if gt_connection is None:
                    continue
                if event.inferior.connection_num == gt_connection:
                    self.host_inf_for_auto_remove = key

    def handle_quit_event(self, event):
        """Handler for GDB's quit event."""
        if event is None:
            return

        for host_inf in self.inf_dict:
            self.host_inf_for_auto_remove = host_inf
            self.handle_before_prompt_event()

    def handle_error(self, inf):
        """Remove the inferior and clean-up dict in case of error."""
        print("""\
intelgt: gdbserver-gt failed to start.  The environment variable
INTELGT_AUTO_ATTACH_DISABLE=1 can be used for disabling auto-attach.""")
        gdb.execute(f"inferior {inf.num}", False, True)
        # The gt inferior is the last inferior that was added.
        gt_inf = gdb.inferiors()[-1]
        gdb.execute(f"remove-inferior {gt_inf.num}")
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

        gdbserver_gt_attach_str = \
            f"{self.gdbserver_gt_binary} --multi --hostpid={inf.pid} - "

        if gdbserver_gt_path_env_var:
            gdbserver_gt_attach_str = \
              f"{gdbserver_gt_path_env_var}/{gdbserver_gt_attach_str}"

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
        # Switch to the gt inferior.  It is the most recent inferior.
        gt_inf = gdb.inferiors()[-1]
        gdb.execute(f"inferior {gt_inf.num}", False, True)
        gdb.execute("set sysroot /")
        target_remote_str = \
            " ".join((f"{ld_lib_path} {gdbserver_cmd}").split())
        target_cmd = "target extended-remote"

        gdbserver_port = \
          self.get_env_variable("INTELGT_AUTO_ATTACH_GDBSERVER_PORT")
        if gdbserver_port:
            print(f"intelgt: connecting to gdbserver on port {gdbserver_port}.")
            gdb.execute(f"{target_cmd} :{gdbserver_port}", False, True)
        else:
            gdb.execute(f"{target_cmd} | {target_remote_str}", False, True)
            print(f"intelgt: gdbserver-gt started for process {inf.pid}.")

        # Set a pending attach.
        connection_output = gdb.execute("attach 0", False, True)
        # Print the first line only.  It contains useful device id and
        # architecture info.
        print(connection_output.split("\n")[0])

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
        gt_inf = gdb.inferiors()[-1]
        gdb.execute(f"inferior {gt_inf.num}", False, True)

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

        gdb.execute(f"target extended-remote | {target_remote_str}")
        print(f"intelgt: inferior {gt_inf.num} "
              f"created remotely for process {inf.pid} "
              f"using command '{target_remote_str}'")

    @staticmethod
    def igfxdcd_is_loaded():
        """Check if the igfxdcd module is loaded."""
        # Use lsmod to see if the igfxdcd module exists.  We don't use
        # modinfo because in case the module was loaded as a .ko file
        # via insmod, modinfo may not report it.
        with subprocess.Popen(['lsmod'], stdout=subprocess.PIPE) as lsmod:
            with subprocess.Popen(['grep', "^igfxdcd "], stdin=lsmod.stdout,
                                  stdout=subprocess.DEVNULL) as grep:
                grep.communicate()
                return grep.returncode == 0

    def init_gt_inferiors(self):
        """Create/initialize inferiors with gdbserver-gt connection"""
        # get the host inferior.
        host_inf = gdb.selected_inferior()

        connection = host_inf.connection_shortname
        if connection not in ('remote', 'native'):
            print(f"intelgt: connection name '{connection}' not recognized.")
            self.handle_error(host_inf)
            return

        self.hook_bp.delete()

        if not self.igfxdcd_is_loaded():
            print("""\
intelgt: the igfxdcd module (i.e. the debug driver) is not loaded.""")
            return

        num_devices = gdb.parse_and_eval(NUM_DEVICES_FCT)
        if num_devices == 0:
            print("""intelgt: no GPU device is available for debug.""")
            return

        if host_inf not in self.inf_dict or self.inf_dict[host_inf] is None:
            if not self.is_nonstop:
                gdb.execute("set schedule-multiple off")
            gdb.execute("add-inferior -hidden -no-connection", False, True)

            # attach gdbserver-gt to gt inferior.
            # This represents the first device.
            try:
                self.handle_attach_gdbserver_gt(connection, host_inf)
            except gdb.error:
                self.handle_error(host_inf)
                return

            self.inf_dict[host_inf] = gdb.inferiors()[-1].connection_num

            # switch to host inferior.
            gdb.execute(f"inferior {host_inf.num}", False, True)
            if not self.is_nonstop:
                gdb.execute("set schedule-multiple on")

INTELGT_AUTO_ATTACH = IntelgtAutoAttach()
