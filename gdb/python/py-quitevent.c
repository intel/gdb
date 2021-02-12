/* Python interface to inferior continue events.

   Copyright (C) 2020-2021 Free Software Foundation, Inc.

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

#include "defs.h"
#include "py-event.h"

static gdbpy_ref<>
create_quit_event_object ()
{
  gdbpy_ref<> quit_event = create_event_object (&quit_event_object_type);

  return quit_event;
}

/* Callback that is used when an quit event occurs.  This function
   will create a new Python quit event object.  */

int
emit_quit_event ()
{
  if (evregpy_no_listeners_p (gdb_py_events.quit))
    return 0;

  gdbpy_ref<> event = create_quit_event_object ();

  if (event != NULL)
    return evpy_emit_event (event.get (), gdb_py_events.quit);

  return -1;
}
