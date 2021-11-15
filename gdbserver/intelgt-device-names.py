#!/usr/bin/env python3

# Generate the intelgt-device-names.inl file with the newest
# set of device names.
#
# Copyright (C) 2022 Free Software Foundation, Inc.
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
# along with this program.  If not, see <http://www.gnu.org/licenses/>

# Running this file will create an updated version of the
# intelgt-device-names.inl file in the folder the script is executed in.  The
# generated file is used to map Intel (R) graphics technology device ID's to
# readable names.  Typically, one would execute this file from the gdbserver
# directory to update all names whenever new devices become available.

import textwrap
import urllib.request
import re

# Define the copyright notice.
copyright = """/* *INDENT-OFF* */ /* THIS FILE IS GENERATED -*- buffer-read-only: t -*- */
/* vi:set ro: */

/* Intel (R) graphics technology device names for for GDB, the GNU debugger.
   Copyright (C) 2022 Free Software Foundation, Inc.
   This file is part of GDB.
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* This file was created with the aid of ``intelgt-device-names.py''.  */
"""

intelgt_names_url = "https://raw.githubusercontent.com/intel/compute-runtime/master/shared/source/dll/devices/devices_base.inl"

names_url_request = urllib.request.Request(intelgt_names_url)
with urllib.request.urlopen(names_url_request) as names_url_response: # nosec
    intelgt_names_file = names_url_response.read()

    intelgt_names_file = intelgt_names_file.decode('utf-8').split('\n')

    device_pattern = 'NAMEDDEVICE\( (0[xX][0-9a-fA-F]+), .*\"(.*)\" \)'

    with open('intelgt-device-names.inl', 'w') as f:
        print(copyright, file=f)

        # Match device_id and device_name for all named devices.
        for line in intelgt_names_file:
            m = re.search(device_pattern, line)
            if m:
                print(f'{{ {m.group(1)}, \"{m.group(2)}\" }},', file=f)
