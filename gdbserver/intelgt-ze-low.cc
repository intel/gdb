/* Target interface for Intel GT based on level-zero for gdbserver.

   Copyright (C) 2020-2022 Free Software Foundation, Inc.

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

#include "server.h"
#include "ze-low.h"


/* FIXME make into a target method?  */
int using_threads = 1;

/* Target op definitions for Intel GT target based on level-zero.  */

class intelgt_ze_target : public ze_target
{
public:
  const gdb_byte *sw_breakpoint_from_kind (int kind, int *size) override;

  bool supports_stopped_by_sw_breakpoint () override { return true; }
  bool stopped_by_sw_breakpoint () override;
  bool supports_run_command () override;

  CORE_ADDR read_pc (regcache *regcache) override;
  void write_pc (regcache *regcache, CORE_ADDR pc) override;
};

bool
intelgt_ze_target::supports_run_command ()
{
  return false;
}

const gdb_byte *
intelgt_ze_target::sw_breakpoint_from_kind (int kind, int *size)
{
  /* We do not support breakpoint instructions.

     Use gdbarch methods that use read/write memory target operations for
     setting s/w breakopints.  */
  *size = 0;
  return nullptr;
}

bool
intelgt_ze_target::stopped_by_sw_breakpoint ()
{
  error (_("%s: tbd"), __FUNCTION__);
}

CORE_ADDR
intelgt_ze_target::read_pc (regcache *regcache)
{
  error (_("%s: tbd"), __FUNCTION__);
}

void
intelgt_ze_target::write_pc (regcache *regcache, CORE_ADDR pc)
{
  error (_("%s: tbd"), __FUNCTION__);
}


/* The Intel GT target ops object.  */

static intelgt_ze_target the_intelgt_ze_target;

extern void initialize_low ();
void
initialize_low ()
{
  /* Delayed initialization of level-zero targets.  See ze-low.h.  */
  the_intelgt_ze_target.init ();
  set_target_ops (&the_intelgt_ze_target);
}
