# Copyright (C) 2019-2022 Free Software Foundation, Inc.

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
'gdbserver-gt' or 'gdbserver-ze' to listen to debug events from the
GPU.  This is done to ensure debuggability in case the kernel is
offloaded to GPU.  The auto-attach feature is turned on by default,
and it is harmless to the debugging capability on the CPU device.

The auto-attach feature can be disabled by setting the OS environment
variable 'INTELGT_AUTO_ATTACH_DISABLE' to a non-zero value.  It can be
also set or unset in the inferior's environment, so that the user can
disable or enable auto-attach in the same gdb session.

By default, it is expected that the 'gdbserver-gt' executable exists
in the PATH.  Below are customization options via environment
variables.

 * To use a 'gdbserver-gt' that resides in a specific directory <DIR>,
   set the INTELGT_AUTO_ATTACH_GDBSERVER_GT_PATH environment variable
   to <DIR>.

 * The content of the INTELGT_AUTO_ATTACH_IGFXDBG_LIB_PATH environment
   variable is prepended to the LD_LIBRARY_PATH environment variable
   when spawning 'gdbserver-gt'.

 * To enable developer debug logs of the python script set the
   INTELGT_AUTO_ATTACH_VERBOSE_LOG environment variable to the
   desired value.
"""

import re
import subprocess
import gdb

# Breakpoint function for executing the auto-attach.
BP_FCT = "igfxdbgxchgDebuggerHook"
BP_ZET = "zeContextCreate"

# Function call to query the number of devices.
NUM_DEVICES_FCT = "(int)igfxdbgxchgNumDevices()"

class DebugLogger:
    """Class for convenient debug logs"""
    @staticmethod
    def debug_log_is_enabled():
        """Helper function to check if debug logs are enabled
        via os environment variable or in inferior's environment."""
        env_value = IntelgtAutoAttach.get_env_variable(
            "INTELGT_AUTO_ATTACH_VERBOSE_LOG")
        return not(env_value is None or env_value == "0")

    @staticmethod
    def log_call(func):
        """"Decorative function to mark the corresponding class
        to log calls when debug logs are enabled."""
        match = re.search(
            f"(.+).{func.__name__}", func.__qualname__)
        self_name = match.group(1) if match else ""
        def wrapper(*args, **kwargs):
            if DebugLogger.debug_log_is_enabled():
                arg_repr = []
                for arg in args:
                    match = re.search(
                        f"<(?:{str(func.__module__)}.)?(.+) "
                        "object at 0x[0-9a-fA-F]*>", repr(arg))
                    if match and self_name in match.group(1):
                        arg_repr.append("self")
                    else:
                        arg_repr.append(
                            match.group(1) if match else repr(arg))
                for arg_name, arg_value in kwargs.items():
                    match = re.search(
                        "<(.+) object at 0x[0-9a-fA-F]*>", repr(arg_value))
                    arg_repr.append(
                        f"{arg_name}="
                        f"{match.group(1) if match else repr(arg_value)}")
                str_args = ", ".join(arg_repr)
                print(f"intelgt: calling {func.__name__}({str_args}).")
            return func(*args, **kwargs)
        return wrapper

    @staticmethod
    def log(msg):
        """Helper function to log debug messages when debug logs
        are enabled."""
        if DebugLogger.debug_log_is_enabled():
            print(f"intelgt: {msg}")

# pylint: disable-next=too-many-instance-attributes
class IntelgtAutoAttach:
    """Class to debug kernels offloaded to Intel GPU"""
    def __init__(self):
        """Registration of new loaded object file handler event."""
        gdb.events.new_objfile.connect(self.handle_new_objfile_event)
        gdb.events.gdb_exiting.connect(self.handle_gdb_exiting_event)
        gdb.events.exited.connect(self.handle_exited_event)
        gdb.events.before_prompt.connect(self.handle_before_prompt_event)
        self.inf_dict = {}
        self.host_inf_for_auto_remove = None
        self.is_nonstop = False
        self.hook_bp = None
        self.the_bp = None
        env_value = self.get_env_variable("ZET_ENABLE_PROGRAM_DEBUGGING")
        self.use_dcd = env_value is None or env_value == "0"
        if self.use_dcd:
            self.gdbserver_gt_binary = "gdbserver-gt"
        else:
            self.gdbserver_gt_binary = "gdbserver-ze"
        self.enable_schedule_multiple_at_gt_removal = False
        # Env variable to pass custom flags to gdbserver such as
        # "--debug" and "--remote-debug" for debugging purposes.
        self.gdbserver_args = self.get_env_variable(
            "INTELGT_AUTO_ATTACH_GDBSERVER_ARGS", "")
        if self.gdbserver_args != "":
            self.gdbserver_args = " " + self.gdbserver_args

    @staticmethod
    def get_env_variable(var, default=None):
        """Helper function to get environment variable value."""
        env_var = gdb.execute(f"show env {var}", to_string=True)
        if env_var.find(f"Environment variable \"{var}\" " \
                         "not defined.") != -1:
            return default
        return env_var.replace(f"{var} = ", "", 1).strip()

    def handle_new_objfile_event(self, event):
        """Handler for a new object file load event.  If auto-attach has
        not been disabled, set a breakpoint at location 'BP_FCT'."""
        if event is None:
            return

        # For some OSes, .gnu_debugdata section makes GDB create an in-memory
        # debug object file.  Ignore it and wait for the actual object file.
        # See gdb/minidebug.c.
        if event.new_objfile.filename.startswith(".gnu_debugdata for "):
            DebugLogger.log(
                f"Skipping hook breakpoint for {event.new_objfile.filename} loaded event.")
            return

        if not 'libigfxdbgxchg64.so' in event.new_objfile.filename:
            return

        if ('libze_intel_gpu.so' in event.new_objfile.filename
            and not self.use_dcd):
            DebugLogger.log(
                f"received {event.new_objfile.filename} loaded event.")
            if gdb.selected_inferior().was_attached:
                # We just learnt about the library event and the inferior
                # was attached.  It is possible that the level-zero backend
                # is already initialized.  Let us try to initialize the
                # GT inferiors.  If this does not work, it means level-zero
                # initialization has not happened yet.  In that case we try
                # again later via the hook BP.
                DebugLogger.log("context might already be initialized.")
                self.init_gt_inferiors()
            return

        if ('libigfxdbgxchg64.so' in event.new_objfile.filename
            and self.use_dcd):
            self.the_bp = BP_FCT
        elif ('libze_loader.so' in event.new_objfile.filename
              and not self.use_dcd):
            self.the_bp = BP_ZET
        else:
            return

        DebugLogger.log(
            f"received {event.new_objfile.filename} loaded event. "
            "Setting up bp hook.")
        self.setup_hook_bp()

    def setup_hook_bp(self):
        """Set a breakpoint at the location of BP_FCT or BP_ZET
        indicated by self.the_bp.  If not set, return and do nothing."""
        if not self.the_bp:
            DebugLogger.log("BP location for the hook is not set.")
            return

        self.hook_bp = gdb.Breakpoint(self.the_bp, type=gdb.BP_BREAKPOINT,
                                      internal=1, temporary=0)
        self.hook_bp.silent = True
        commands = ("python gdb.function.intelgt_auto_attach" +
                    ".INTELGT_AUTO_ATTACH.init_gt_inferiors()")
        # Find out if we are in non-stop mode.  It is safe to do it
        # here and only once, because the setting cannot be
        # changed after the program starts executing.
        nonstop_info = gdb.execute("show non-stop", False, True)
        self.is_nonstop = nonstop_info.endswith("on.\n")

        if self.is_nonstop:
            command_suffix = "\ncontinue -a"
        else:
            command_suffix = "\ncontinue"

        eclipse = self.get_env_variable("ECLIPSE")
        if eclipse is not None and eclipse.endswith("1"):
            self.hook_bp.silent = False
            command_suffix = ""

        self.hook_bp.commands = commands + command_suffix

    @DebugLogger.log_call
    def remove_gt_inf_for(self, host_inf):
        """First detach from, then remove the gt inferiors that were created
        for HOST_INF."""
        gt_pair = self.inf_dict[host_inf]
        if gt_pair is None:
            return
        (gt_inf, gt_connection) = gt_pair

        cli_suppressed = self.set_suppress_notifications("on")

        # Find all the gt infs whose connection is the same as 'gt_connection'.
        gt_infs = []
        for an_inf in gdb.inferiors():
            if an_inf.connection_num == gt_connection:
                gt_infs.append(an_inf)

        # With the --attach scenario, the gt_inf may have lost its connection.
        # Double-check.
        if not gt_inf in gt_infs:
            gt_infs.append(gt_inf)

        # First, detach all gt inferiors.
        for gt_inf in gt_infs:
            if gt_inf.pid != 0:
                gdb.execute(f"detach inferiors {gt_inf.num}", False, False)

        # gdbserver-gt uses the --multi mode whereas gdbserver-ze uses
        # --attach.  We need to explicitly make gdbserver-gt quit.
        if self.use_dcd:
            # Terminate gdbserver.  We should do this from a gt inf.
            if gt_infs and gdb.selected_inferior() not in gt_infs:
                cmd = f"inferior {gt_infs[0].num}"
                self.protected_gdb_execute(cmd, True)

            # Check if switching to gt inf was successful.
            # Otherwise, we cannot send 'monitor exit' command.
            if gdb.selected_inferior() in gt_infs:
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

        self.set_suppress_notifications("on" if cli_suppressed else "off")

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

    @DebugLogger.log_call
    def remove_gt_inf_if_stored_for_removal(self):
        """If an inferior was marked for removal, detach and then remove all
        the gt inferiors that were created for the marked HOST_INF."""
        if self.host_inf_for_auto_remove is None:
            DebugLogger.log("no inferior is stored for removal.")
            return

        host_inf = self.host_inf_for_auto_remove
        self.remove_gt_inf_for(host_inf)
        self.host_inf_for_auto_remove = None

        if self.enable_schedule_multiple_at_gt_removal:
            gdb.execute("set schedule-multiple on")
            self.enable_schedule_multiple_at_gt_removal = False

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
        self.remove_gt_inf_if_stored_for_removal()

    @DebugLogger.log_call
    def handle_exited_event(self, event):
        """Indication that an inferior's process has exited.
        Save the host inferior to remove later the GT inferior that
        was associated with it."""
        if event is None:
            return

        if not self.host_inf_for_auto_remove is None:
            return

        if event.inferior in self.inf_dict.keys():
            if not self.inf_dict[event.inferior] is None:
                self.host_inf_for_auto_remove = event.inferior
                # Turn off schedule-multiple if it was enabled before the
                # exit of this host inferior.  From now on, sending
                # commands like vCont to gt inferiors associated with this
                # host inferior is invalid (since likely the kernel has
                # exited as well).  This happens for example when we
                # ended up here after GDB issued a run command for the host
                # inferior killing the programm + kernel - it will start a
                # new host inferior and issue a continue command for
                # possibly all inferiors (if we leave schedule-multiple
                # set).  We reset the schedule-multiple the next time we
                # remove the gt inferiors stored for removal.
                info = gdb.execute("show schedule-multiple", False, True)
                if info.endswith("on.\n"):
                    gdb.execute("set schedule-multiple off")
                    self.enable_schedule_multiple_at_gt_removal = True
        else:
            for key, value in self.inf_dict.items():
                if value is None:
                    continue
                (gt_inf, gt_connection) = value
                if event.inferior.connection_num == gt_connection:
                    self.host_inf_for_auto_remove = key
                elif event.inferior == gt_inf:
                    self.host_inf_for_auto_remove = key

    @DebugLogger.log_call
    def handle_gdb_exiting_event(self, event):
        """Handler for GDB's exiting event."""
        if event is None:
            return

        for host_inf in self.inf_dict:
            self.host_inf_for_auto_remove = host_inf
            self.remove_gt_inf_if_stored_for_removal()

    @DebugLogger.log_call
    def handle_error(self, inf, details=None):
        """Remove the inferior and clean-up dict in case of error."""
        if details is not None:
            print(details)
        gdb.execute(f"inferior {inf.num}", False, True)
        # The gt inferior is the last inferior that was added.
        gt_inf = gdb.inferiors()[-1]
        gdb.execute(f"remove-inferior {gt_inf.num}")
        self.inf_dict[inf] = None

    def display_auto_attach_error_msg(self):
        """Display auto attach error message."""
        print(f"""\
intelgt: {self.gdbserver_gt_binary} failed to start.  The environment variable
INTELGT_AUTO_ATTACH_DISABLE=1 can be used for disabling auto-attach.""")

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

    @DebugLogger.log_call
    def handle_attach_gdbserver_gt(self, inf):
        """Attach gdbserver to gt inferior either locally or remotely."""

        # User may disable the auto attach from GDB command prompt.
        if self.get_env_variable("INTELGT_AUTO_ATTACH_DISABLE", "0") == "1":
            return False

        # Check 'INTELGT_AUTO_ATTACH_GDBSERVER_GT_PATH' in the inferior's
        # environment.
        gdbserver_gt_path_env_var = \
          self.get_env_variable("INTELGT_AUTO_ATTACH_GDBSERVER_GT_PATH")

        binary = self.gdbserver_gt_binary + self.gdbserver_args
        if self.use_dcd:
            gdbserver_gt_attach_str = f"{binary} --multi --hostpid={inf.pid} -"
        else:
            gdbserver_gt_attach_str = f"{binary} --attach - {inf.pid}"

        if gdbserver_gt_path_env_var:
            gdbserver_gt_attach_str = \
              f"{gdbserver_gt_path_env_var}/{gdbserver_gt_attach_str}"

        # Check 'INTELGT_AUTO_ATTACH_IGFXDBG_LIB_PATH' in the inferior's
        # environment.
        igfxdbg_lib_path_env_var = \
          self.get_env_variable("INTELGT_AUTO_ATTACH_IGFXDBG_LIB_PATH")
        ld_lib_path_str = ""
        if igfxdbg_lib_path_env_var:
            ld_lib_path_str = \
                f"LD_LIBRARY_PATH={igfxdbg_lib_path_env_var}:$LD_LIBRARY_PATH"

        try:
            self.make_native_gdbserver(inf, ld_lib_path_str,
                                       gdbserver_gt_attach_str)
        except gdb.error as ex:
            """Explicitly raise exception to 'init_gt_inferior'.  This
            otherwise results in undhandled exception in 'init_gt_inferior'
            if ctrl-c is used whilst attaching to inferior.  """
            raise ex

    @DebugLogger.log_call
    def make_native_gdbserver(self, inf, ld_lib_path, gdbserver_cmd):
        """Spawn and connect to a native instance of gdbserver."""
        # Switch to the gt inferior.  It is the most recent inferior.
        gt_inf = gdb.inferiors()[-1]
        gdb.execute(f"inferior {gt_inf.num}", False, True)
        gdb.execute("set sysroot /")
        target_remote_str = \
            " ".join((f"{ld_lib_path} {gdbserver_cmd}").split())

        if self.use_dcd:
            target_cmd = "target extended-remote"
        else:
            target_cmd = "target remote"

        # If the user set args for gdbserver, do not capture and suppress
        # the output, because the user wants to see the full output.
        capture_output = (self.gdbserver_args == "")

        gdbserver_port = \
          self.get_env_variable("INTELGT_AUTO_ATTACH_GDBSERVER_PORT")
        if gdbserver_port:
            print(f"intelgt: connecting to {self.gdbserver_gt_binary} "
                  f"on port {gdbserver_port}.")
            gdb.execute(f"{target_cmd} :{gdbserver_port}", False,
                        capture_output)
        else:
            gdb.execute(f"{target_cmd} | {target_remote_str}", False,
                        capture_output)
            print(f"intelgt: {self.gdbserver_gt_binary} started for "
                  f"process {inf.pid}.")

        # gdbserver-gt uses the --multi mode.  We need to make a pending attach.
        if self.use_dcd:
            # Set a pending attach.
            connection_output = gdb.execute("attach 0", False, True)
            # Print the first line only.  It contains useful device id and
            # architecture info.
            print(connection_output.split("\n")[0])

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

    @DebugLogger.log_call
    def init_gt_inferiors(self):
        """Create/initialize inferiors with gdbserver connection"""

        # If we somehow got here after an inferior was deleted but without a
        # prompt event in-between (e.g. after a 'run' command with an already
        # running process), delete the old gt inferior first.  Otherwise we
        # cannot create a new one further down.
        self.remove_gt_inf_if_stored_for_removal()

        # Get the host inferior.
        host_inf = gdb.selected_inferior()

        connection = host_inf.connection.type
        if connection != 'native':
            print(f"intelgt: connection name '{connection}' not recognized.")
            self.handle_error(host_inf)
            self.display_auto_attach_error_msg()
            return

        self.hook_bp.delete()

        if self.use_dcd:
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

            cli_suppressed = self.set_suppress_notifications("on")
            # Attach gdbserver to gt inferior.
            # This represents the first device.
            try:
                self.handle_attach_gdbserver_gt(host_inf)
                # Get the most recent gt inferior.
                gt_inf = gdb.inferiors()[-1]
                # For the --attach scenario we use gt_inf; for the
                # --multi scenario we use gt_inf's connection num.
                self.inf_dict[host_inf] = (gt_inf, gt_inf.connection_num)
                # Switch to the host inferior.
                gdb.execute(f"inferior {host_inf.num}", False, True)
            # Fix ctrl-c while attaching to gt inferior.
            except KeyboardInterrupt:
                self.handle_error(host_inf)
                self.display_auto_attach_error_msg()
            except gdb.error as ex:
                # If auto attach fails due to uninitialized context, it
                # should be retried by setting up the hook bp.
                if "attempting to attach too early" in str(ex):
                    self.handle_error(host_inf)
                    self.setup_hook_bp()
                else:
                    self.handle_error(host_inf, details=str(ex))
                    self.display_auto_attach_error_msg()

            if not self.is_nonstop:
                gdb.execute("set schedule-multiple on")

            self.set_suppress_notifications("on" if cli_suppressed else "off")
            return

    @staticmethod
    def set_suppress_notifications(new_value):
        """Set the suppress_notification state for CLI.
        Return the previous value."""

        info = gdb.execute("show suppress-cli-notifications", False, True)
        was_cli_suppressed = info.endswith("on.\n")
        gdb.execute("set suppress-cli-notifications " + new_value)

        return was_cli_suppressed

AUTO_ATTACH_DISABLED = IntelgtAutoAttach.get_env_variable(
        "INTELGT_AUTO_ATTACH_DISABLE", "0")
if AUTO_ATTACH_DISABLED != "1":
    INTELGT_AUTO_ATTACH = IntelgtAutoAttach()
