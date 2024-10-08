# Copyright 2023-2024 Free Software Foundation, Inc.

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

import gdb

# This is deprecated in 3.9, but required in older versions.
from typing import Optional

from .server import capability, request
from .sources import decode_source
from .startup import in_gdb_thread


@in_gdb_thread
def _find_lines(source, start_line, end_line):
    filename = decode_source(source)
    lines = set()
    for entry in gdb.execute_mi("-symbol-list-lines", filename)["lines"]:
        line = entry["line"]
        if line >= start_line and line <= end_line:
            lines.add(line)
    return {"breakpoints": [{"line": x} for x in sorted(lines)]}


# Note that the spec says that the arguments to this are optional.
# However, calling this without arguments is nonsensical.  This is
# discussed in:
#   https://github.com/microsoft/debug-adapter-protocol/issues/266
# This points out that fixing this would be an incompatibility but
# goes on to propose "if arguments property is missing, debug adapters
# should return an error".
@request("breakpointLocations")
@capability("supportsBreakpointLocationsRequest")
def breakpoint_locations(*, source, line: int, endLine: Optional[int] = None, **extra):
    if endLine is None:
        endLine = line
    return _find_lines(source, line, endLine)
