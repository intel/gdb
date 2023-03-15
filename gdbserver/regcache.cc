/* Register support routines for the remote server for GDB.
   Copyright (C) 2001-2022 Free Software Foundation, Inc.

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
#include "regdef.h"
#include "gdbthread.h"
#include "tdesc.h"
#include "gdbsupport/rsp-low.h"
#ifndef IN_PROCESS_AGENT

struct regcache *
get_thread_regcache (struct thread_info *thread, bool fetch)
{
  struct regcache *regcache;

  regcache = thread_regcache_data (thread);

  /* Threads' regcaches are created lazily, because biarch targets add
     the main thread/lwp before seeing it stop for the first time, and
     it is only after the target sees the thread stop for the first
     time that the target has a chance of determining the process's
     architecture.  IOW, when we first add the process's main thread
     we don't know which architecture/tdesc its regcache should
     have.  */
  if (regcache == NULL)
    {
      struct process_info *proc = get_thread_process (thread);

      gdb_assert (proc->tdesc != NULL);

      regcache = new struct regcache (proc->tdesc);
      set_thread_regcache_data (thread, regcache);
      regcache->thread = thread;
    }

  if (fetch)
    regcache->fetch ();

  return regcache;
}

void
regcache::fetch ()
{
  if (!registers_fetched)
    {
      scoped_restore_current_thread restore_thread;
      gdb_assert (this->thread != nullptr);
      switch_to_thread (this->thread);

      /* If there are individually-fetched dirty registers, first
	 store them, then fetch all.  We prefer this to doing
	 individual fetch for each registers, if needed, because it is
	 more likely that very few registers are individually-fetched
	 at this moment and that fetching all in one go is more
	 efficient than fetching each reg one by one.  */
      for (int i = 0; i < tdesc->reg_defs.size (); ++i)
	{
	  if (register_status[i] == REG_DIRTY)
	    store_inferior_registers (this, i);
	}

      /* Invalidate all registers, to prevent stale left-overs.  */
      discard ();
      fetch_inferior_registers (this, -1);
      registers_fetched = true;

      /* Make sure that the registers that could not be fetched are
	 now unavailable.  */
      for (int i = 0; i < tdesc->reg_defs.size (); ++i)
	{
	  if (register_status[i] == REG_UNKNOWN)
	    set_register_status (i, REG_UNAVAILABLE);
	}
    }
}

/* See gdbsupport/common-regcache.h.  */

struct regcache *
get_thread_regcache_for_ptid (ptid_t ptid)
{
  return get_thread_regcache (find_thread_ptid (ptid));
}

void
regcache_invalidate_thread (struct thread_info *thread)
{
  struct regcache *regcache;

  regcache = thread_regcache_data (thread);

  if (regcache != nullptr)
    regcache->invalidate ();
}

void
regcache::invalidate ()
{
  scoped_restore_current_thread restore_thread;
  gdb_assert (this->thread != nullptr);
  switch_to_thread (this->thread);

  /* Store dirty registers individually.  We prefer this to a
     store-all, because it is more likely that a small number of
     registers have changed.  */
  for (int i = 0; i < tdesc->reg_defs.size (); ++i)
    {
      if (register_status[i] == REG_DIRTY)
	store_inferior_registers (this, i);
    }

  discard ();
}

/* See regcache.h.  */

void
regcache_invalidate_pid (int pid)
{
  /* Only invalidate the regcaches of threads of this process.  */
  for_each_thread (pid, regcache_invalidate_thread);
}

/* See regcache.h.  */

void
regcache_invalidate (void)
{
  /* Only update the threads of the current process.  */
  int pid = current_thread->id.pid ();

  regcache_invalidate_pid (pid);
}

#endif

void
regcache::discard ()
{
  memset (registers, 0, tdesc->registers_size);
#ifndef IN_PROCESS_AGENT
  memset ((void *) register_status, REG_UNKNOWN, tdesc->reg_defs.size ());
#endif
  registers_fetched = false;
}

void
regcache::initialize (const target_desc *tdesc,
		      unsigned char *regbuf)
{
  if (regbuf == NULL)
    {
#ifndef IN_PROCESS_AGENT
      this->tdesc = tdesc;
      this->registers
	= (unsigned char *) xmalloc (tdesc->registers_size);
      this->registers_owned = true;
      this->register_status
	= (enum register_status *) xmalloc (tdesc->reg_defs.size ());

      /* Make sure to zero-initialize the register cache when it is
	 created, in case there are registers the target never
	 fetches.  This way they'll read as zero instead of
	 garbage.  */
      discard ();
#else
      gdb_assert_not_reached ("can't allocate memory from the heap");
#endif
    }
  else
    {
      this->tdesc = tdesc;
      this->registers = regbuf;
      this->registers_owned = false;
#ifndef IN_PROCESS_AGENT
      this->register_status = nullptr;
#endif
    }

  this->registers_fetched = false;
}

#ifndef IN_PROCESS_AGENT

regcache::regcache (const target_desc *tdesc)
{
  gdb_assert (tdesc->registers_size != 0);
  initialize (tdesc, nullptr);
}

regcache::~regcache ()
{
  if (registers_owned)
    free (registers);
  free (register_status);
}

#endif

void
regcache::copy_from (regcache *src)
{
  gdb_assert (src != nullptr);
  gdb_assert (src->tdesc == this->tdesc);
  gdb_assert (src != this);

  memcpy (this->registers, src->registers, src->tdesc->registers_size);
#ifndef IN_PROCESS_AGENT
  if (this->register_status != nullptr && src->register_status != nullptr)
    memcpy (this->register_status, src->register_status,
	    src->tdesc->reg_defs.size ());
#endif
  this->registers_fetched = src->registers_fetched;
}

/* Return a reference to the description of register N.  */

static const struct gdb::reg &
find_register_by_number (const struct target_desc *tdesc, int n)
{
  return tdesc->reg_defs[n];
}

#ifndef IN_PROCESS_AGENT

void
regcache::registers_to_string (char *buf)
{
  unsigned char *regs = registers;
  for (int i = 0; i < tdesc->reg_defs.size (); ++i)
    {
      if (register_status[i] == REG_VALID
	  || register_status[i] == REG_DIRTY)
	{
	  bin2hex (regs, buf, register_size (tdesc, i));
	  buf += register_size (tdesc, i) * 2;
	}
      else
	{
	  memset (buf, 'x', register_size (tdesc, i) * 2);
	  buf += register_size (tdesc, i) * 2;
	}
      regs += register_size (tdesc, i);
    }
  *buf = '\0';
}

void
regcache::registers_from_string (const char *buf)
{
  int len = strlen (buf);

  if (len != tdesc->registers_size * 2)
    {
      warning ("Wrong sized register packet (expected %d bytes, got %d)",
	       2 * tdesc->registers_size, len);
      if (len > tdesc->registers_size * 2)
	len = tdesc->registers_size * 2;
    }

  unsigned char *new_regs =
    (unsigned char *) alloca (tdesc->registers_size);

  hex2bin (buf, new_regs, len / 2);
  supply_regblock (new_regs);
}

int
find_regno (const struct target_desc *tdesc, const char *name)
{
  for (int i = 0; i < tdesc->reg_defs.size (); ++i)
    {
      if (strcmp (name, find_register_by_number (tdesc, i).name) == 0)
	return i;
    }
  internal_error (__FILE__, __LINE__, "Unknown register %s requested",
		  name);
}

static void
free_register_cache_thread (struct thread_info *thread)
{
  struct regcache *regcache = thread_regcache_data (thread);

  if (regcache != NULL)
    {
      regcache_invalidate_thread (thread);
      delete regcache;
      set_thread_regcache_data (thread, NULL);
    }
}

void
regcache_release (void)
{
  /* Flush and release all pre-existing register caches.  */
  for_each_thread (free_register_cache_thread);
}
#endif

int
register_cache_size (const struct target_desc *tdesc)
{
  return tdesc->registers_size;
}

int
register_size (const struct target_desc *tdesc, int n)
{
  return find_register_by_number (tdesc, n).size / 8;
}

/* See gdbsupport/common-regcache.h.  */

int
regcache_register_size (const struct regcache *regcache, int n)
{
  return register_size (regcache->tdesc, n);
}

unsigned char *
regcache::register_data (int regnum) const
{
  return (registers
	  + find_register_by_number (tdesc, regnum).offset / 8);
}

void
supply_register (struct regcache *regcache, int n, const void *buf)
{
  return regcache->raw_supply (n, buf);
}

/* See gdbsupport/common-regcache.h.  */

void
regcache::raw_supply (int n, const void *buf)
{
  if (buf)
    {
      memcpy (register_data (n), buf, register_size (tdesc, n));
#ifndef IN_PROCESS_AGENT
      bump_register_status (n);
#endif
    }
  else
    {
      memset (register_data (n), 0, register_size (tdesc, n));
#ifndef IN_PROCESS_AGENT
      set_register_status (n, REG_UNAVAILABLE);
#endif
    }
}

/* Supply register N with value zero to REGCACHE.  */

void
supply_register_zeroed (struct regcache *regcache, int n)
{
  memset (regcache->register_data (n), 0,
	  register_size (regcache->tdesc, n));
#ifndef IN_PROCESS_AGENT
  regcache->bump_register_status (n);
#endif
}

#ifndef IN_PROCESS_AGENT

/* Supply register called NAME with value zero to REGCACHE.  */

void
supply_register_by_name_zeroed (struct regcache *regcache,
				const char *name)
{
  supply_register_zeroed (regcache, find_regno (regcache->tdesc, name));
}

#endif

void
regcache::supply_regblock (const void *buf)
{
  gdb_assert (buf != nullptr);

#ifndef IN_PROCESS_AGENT
  /* First, update the statuses.  Mark dirty only those that have
     changed.  */
  unsigned char *regs = registers;
  unsigned char *new_regs = (unsigned char *) buf;
  for (int i = 0; i < tdesc->reg_defs.size (); ++i)
    {
      int size = register_size (tdesc, i);
      bool first_time = (get_register_status (i) == REG_UNKNOWN);
      bool valid = (get_register_status (i) == REG_VALID);

      if (first_time
	  || (valid && (memcmp (new_regs, regs, size) != 0)))
	bump_register_status (i);

      regs += size;
      new_regs += size;
    }
#endif
  memcpy (registers, buf, tdesc->registers_size);
}

#ifndef IN_PROCESS_AGENT

void
supply_register_by_name (struct regcache *regcache,
			 const char *name, const void *buf)
{
  supply_register (regcache, find_regno (regcache->tdesc, name), buf);
}

#endif

void
collect_register (struct regcache *regcache, int n, void *buf)
{
#ifndef IN_PROCESS_AGENT
  if (regcache->get_register_status (n) == REG_UNKNOWN)
    {
      /* This register has not been fetched from the target, yet.
	 Do it now.  */
      fetch_inferior_registers (regcache, n);
    }
#endif

  regcache->raw_collect (n, buf);
}

/* See gdbsupport/common-regcache.h.  */

void
regcache::raw_collect (int n, void *buf) const
{
  memcpy (buf, register_data (n), register_size (tdesc, n));
}

enum register_status
regcache_raw_read_unsigned (struct regcache *regcache, int regnum,
			    ULONGEST *val)
{
  int size;

  gdb_assert (regcache != NULL);
  gdb_assert (regnum >= 0
	      && regnum < regcache->tdesc->reg_defs.size ());

  size = register_size (regcache->tdesc, regnum);

  if (size > (int) sizeof (ULONGEST))
    error (_("That operation is not available on integers of more than"
	    "%d bytes."),
	  (int) sizeof (ULONGEST));

  *val = 0;
  collect_register (regcache, regnum, val);

  return regcache->get_register_status (regnum);
}

#ifndef IN_PROCESS_AGENT

/* See regcache.h.  */

ULONGEST
regcache_raw_get_unsigned_by_name (struct regcache *regcache,
				   const char *name)
{
  return regcache_raw_get_unsigned (regcache,
				    find_regno (regcache->tdesc, name));
}

void
collect_register_as_string (struct regcache *regcache, int n, char *buf)
{
  bin2hex (regcache->register_data (n), buf,
	   register_size (regcache->tdesc, n));
}

void
collect_register_by_name (struct regcache *regcache,
			  const char *name, void *buf)
{
  collect_register (regcache, find_regno (regcache->tdesc, name), buf);
}

/* Special handling for register PC.  */

CORE_ADDR
regcache_read_pc (struct regcache *regcache)
{
  return the_target->read_pc (regcache);
}

void
regcache_write_pc (struct regcache *regcache, CORE_ADDR pc)
{
  the_target->write_pc (regcache, pc);
}

#endif

/* See gdbsupport/common-regcache.h.  */

enum register_status
regcache::get_register_status (int regnum) const
{
#ifndef IN_PROCESS_AGENT
  gdb_assert (regnum >= 0 && regnum < tdesc->reg_defs.size ());
  if (register_status != nullptr)
    return register_status[regnum];
  else
    return REG_VALID;
#else
  return REG_VALID;
#endif
}

void
regcache::set_register_status (int regnum, enum register_status status)
{
#ifndef IN_PROCESS_AGENT
  gdb_assert (regnum >= 0 && regnum < tdesc->reg_defs.size ());
  if (register_status != nullptr)
    register_status[regnum] = status;
#endif
}

void
regcache::bump_register_status (int regnum)
{
#ifndef IN_PROCESS_AGENT
  if (register_status == nullptr)
    return;
#endif

  switch (get_register_status (regnum))
    {
    case REG_UNKNOWN:
      set_register_status (regnum, REG_VALID);
      break;

    case REG_VALID:
      set_register_status (regnum, REG_DIRTY);
      break;

    default:
      break;
    }
}

/* See gdbsupport/common-regcache.h.  */

bool
regcache::raw_compare (int regnum, const void *buf, int offset) const
{
  gdb_assert (buf != NULL);

  const unsigned char *regbuf = register_data (regnum);
  int size = register_size (tdesc, regnum);
  gdb_assert (size >= offset);

  return (memcmp (buf, regbuf + offset, size - offset) == 0);
}
