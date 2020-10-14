/* Target interface for level-zero based targets for gdbserver.
   See https://github.com/oneapi-src/level-zero.git.

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

#include <level_zero/zet_api.h>


void
ze_target::init ()
{
  ze_result_t status = zeInit (0);
  switch (status)
    {
    case ZE_RESULT_SUCCESS:
      break;

    default:
      error (_("Failed to initialize level-zero: %x"), status);
    }
}

int
ze_target::create_inferior (const char *program,
			    const std::vector<char *> &argv)
{
  /* Level-zero does not support creating inferiors.  */
  return -1;
}

int
ze_target::attach (unsigned long pid)
{
  error (_("%s: tbd"), __FUNCTION__);
  return -1;
}

int
ze_target::detach (process_info *proc)
{
  error (_("%s: tbd"), __FUNCTION__);
  return -1;
}

int
ze_target::kill (process_info *proc)
{
  /* Level-zero does not support killing inferiors.  */
  return -1;
}

void
ze_target::mourn (process_info *proc)
{
  error (_("%s: tbd"), __FUNCTION__);
}

void
ze_target::join (int pid)
{
  error (_("%s: tbd"), __FUNCTION__);
}

void
ze_target::resume (thread_resume *resume_info, size_t n)
{
  error (_("%s: tbd"), __FUNCTION__);
}

ptid_t
ze_target::wait (ptid_t ptid, target_waitstatus *status,
		 target_wait_flags options)
{
  error (_("%s: tbd"), __FUNCTION__);
}

void
ze_target::fetch_registers (regcache *regcache, int regno)
{
  error (_("%s: tbd"), __FUNCTION__);
}

void
ze_target::store_registers (regcache *regcache, int regno)
{
  error (_("%s: tbd"), __FUNCTION__);
}

int
ze_target::read_memory (CORE_ADDR memaddr, unsigned char *myaddr, int len,
			unsigned int addr_space)
{
  error (_("%s: tbd"), __FUNCTION__);
}

int
ze_target::write_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
			 int len, unsigned int addr_space)
{
  error (_("%s: tbd"), __FUNCTION__);
}

void
ze_target::request_interrupt ()
{
  error (_("%s: tbd"), __FUNCTION__);
}

void
ze_target::pause_all (bool freeze)
{
  error (_("%s: tbd"), __FUNCTION__);
}

void
ze_target::unpause_all (bool unfreeze)
{
  error (_("%s: tbd"), __FUNCTION__);
}
