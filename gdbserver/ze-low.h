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

#ifndef GDBSERVER_LEVEL_ZERO_LOW_H
#define GDBSERVER_LEVEL_ZERO_LOW_H

#include "target.h"

/* Target op definitions for level-zero based targets.  */

class ze_target : public process_stratum_target
{
public:
  /* Initialize the level-zero target.

     We cannot do this inside the ctor since zeInit() would generate a
     worker thread that would inherit the uninitialized async I/O
     state.

     Postpone initialization until after async I/O has been
     initialized.  */
  void init ();

  bool supports_multi_process () override { return true; }
  bool supports_non_stop () override { return true; }
  int start_non_stop (bool enable) override { async (enable); return 0; }

  bool async (bool enable) override;

  int create_inferior (const char *program,
		       const std::vector<char *> &argv) override;

  int attach (unsigned long pid) override;
  int detach (process_info *proc) override;

  int kill (process_info *proc) override;
  void mourn (process_info *proc) override;
  void join (int pid) override;

  void resume (thread_resume *resume_info, size_t n) override;
  ptid_t wait (ptid_t ptid, target_waitstatus *status,
	       target_wait_flags options) override;

  void fetch_registers (regcache *regcache, int regno) override;
  void store_registers (regcache *regcache, int regno) override;

  int read_memory (CORE_ADDR memaddr, unsigned char *myaddr,
		   int len, unsigned int addr_space = 0) override;

  int write_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
		    int len, unsigned int addr_space = 0) override;

  /* We model h/w threads - they do not exit.  */
  bool thread_alive (ptid_t ptid) override { return true; }

  void request_interrupt () override;

  void pause_all (bool freeze) override;
  void unpause_all (bool unfreeze) override;
};

#endif /* GDBSERVER_LEVEL_ZERO_LOW_H */
