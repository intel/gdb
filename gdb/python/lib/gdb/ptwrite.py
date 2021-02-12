# Ptwrite utilities.
# Copyright (C) 2018-2021 Free Software Foundation, Inc.

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

"""Utilities for working with ptwrite listeners."""

import gdb
from copy import deepcopy


def default_listener(payload, ip):
    """Default listener that is active upon starting GDB."""
    return "payload: {:#x}".format(payload)

# This dict contains the per thread copies of the listener function and the
# global template listener, from which the copies are created.
_ptwrite_listener = {"global" : default_listener}


def _update_listener_dict(thread_list):
    """Helper function to update the listener dict.

    Discards listener copies of threads that already exited and registers
    copies of the listener for new threads."""
    # thread_list[x].ptid returns the tuple (pid, lwp, tid)
    lwp_list = [i.ptid[1] for i in thread_list]

    # clean-up old listeners
    for key in _ptwrite_listener.keys():
      if key not in lwp_list and key != "global":
        _ptwrite_listener.pop(key)

    # Register listener for new threads
    for key in lwp_list:
        if key not in _ptwrite_listener.keys():
            _ptwrite_listener[key] = deepcopy(_ptwrite_listener["global"])


def _clear_traces(thread_list):
    """Helper function to clear the trace of all threads."""
    current_thread = gdb.selected_thread()

    for thread in thread_list:
        thread.switch()
        gdb.current_recording().clear_trace()

    current_thread.switch()


def register_listener(listener):
    """Register the ptwrite listener function."""
    if listener is not None and not callable(listener):
        raise TypeError("Listener must be callable!")

    thread_list = gdb.Inferior.threads(gdb.selected_inferior())
    _clear_traces(thread_list)

    _ptwrite_listener.clear()
    _ptwrite_listener["global"] = listener

    _update_listener_dict(thread_list)


def get_listener():
    """Returns the listeners of the current thread."""
    thread_list = gdb.Inferior.threads(gdb.selected_inferior())
    _update_listener_dict(thread_list)

    return _ptwrite_listener[gdb.selected_thread().ptid[1]]
