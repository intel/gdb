/* Inferior process information for the remote server for GDB.
   Copyright (C) 2002-2024 Free Software Foundation, Inc.

   Contributed by MontaVista Software.

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

#include "gdbsupport/common-inferior.h"
#include "gdbsupport/owning_intrusive_list.h"
#include "gdbthread.h"
#include "dll.h"

owning_intrusive_list<process_info> all_processes;

/* The current process.  */
static process_info *current_process_;

/* The current thread.  This is either a thread of CURRENT_PROCESS, or
   NULL.  */
struct thread_info *current_thread;

/* The current working directory used to start the inferior.

   Empty if not specified.  */
static std::string current_inferior_cwd;

struct thread_info *
add_thread (ptid_t thread_id, void *target_data)
{
  process_info *process = find_process_pid (thread_id.pid ());
  gdb_assert (process != nullptr);

  auto &new_thread = process->thread_list ().emplace_back (thread_id,
							   target_data);
  bool inserted
    = process->thread_map ().insert ({thread_id, &new_thread}).second;

  /* A thread with this ptid should not exist in the map yet.  */
  gdb_assert (inserted);

  if (current_thread == NULL)
    switch_to_thread (&new_thread);

  return &new_thread;
}

/* See gdbthread.h.  */

struct thread_info *
get_first_thread (void)
{
  return find_thread ([] (thread_info *thread)
    {
      return true;
    });
}

struct thread_info *
find_thread_ptid (ptid_t ptid)
{
  process_info *process = find_process_pid (ptid.pid ());
  if (process == nullptr)
    return nullptr;

  auto &thread_map = process->thread_map ();

  if (auto it = thread_map.find (ptid);
      it != thread_map.end ())
    return it->second;

  return nullptr;
}

/* Find a thread associated with the given PROCESS, or NULL if no
   such thread exists.  */

static struct thread_info *
find_thread_process (const struct process_info *const process)
{
  return find_any_thread_of_pid (process->pid);
}

/* See gdbthread.h.  */

struct thread_info *
find_any_thread_of_pid (int pid)
{
  return find_thread (pid, [] (thread_info *thread) {
    return true;
  });
}

void
remove_thread (struct thread_info *thread)
{
  if (thread->btrace != NULL)
    target_disable_btrace (thread->btrace);

  discard_queued_stop_replies (ptid_of (thread));
  process_info *process = get_thread_process (thread);
  gdb_assert (process != nullptr);

  if (current_thread == thread)
    switch_to_thread (nullptr);

  /* We should not try to remove a thread that was not added.  */
  int num_erased = process->thread_map ().erase (thread->id);
  gdb_assert (num_erased > 0);

  process->thread_list ().erase (process->thread_list ().iterator_to (*thread));
}

void *
thread_target_data (struct thread_info *thread)
{
  return thread->target_data;
}

struct regcache *
thread_regcache_data (struct thread_info *thread)
{
  return thread->regcache_data;
}

void
set_thread_regcache_data (struct thread_info *thread, struct regcache *data)
{
  thread->regcache_data = data;
}

void
clear_inferiors (void)
{
  for_each_process ([] (process_info *process)
    {
      process->thread_list ().clear ();
      process->thread_map ().clear ();
    });

  clear_dlls ();

  switch_to_thread (nullptr);
  current_process_ = nullptr;
}

struct process_info *
add_process (int pid, int attached)
{
  return &all_processes.emplace_back (pid, attached);
}

/* Remove a process from the common process list and free the memory
   allocated for it.
   The caller is responsible for freeing private data first.  */

void
remove_process (struct process_info *process)
{
  clear_symbol_cache (&process->symbol_cache);
  free_all_breakpoints (process);
  gdb_assert (find_thread_process (process) == NULL);
  if (current_process () == process)
    switch_to_process (nullptr);

  all_processes.erase (all_processes.iterator_to (*process));
}

process_info *
find_process_pid (int pid)
{
  return find_process ([&] (process_info *process) {
    return process->pid == pid;
  });
}

/* Get the first process in the process list, or NULL if the list is empty.  */

process_info *
get_first_process (void)
{
  if (!all_processes.empty ())
    return &all_processes.front ();
  else
    return NULL;
}

/* Return non-zero if there are any inferiors that we have created
   (as opposed to attached-to).  */

int
have_started_inferiors_p (void)
{
  return find_process ([] (process_info *process) {
    return !process->attached;
  }) != NULL;
}

/* Return non-zero if there are any inferiors that we have attached to.  */

int
have_attached_inferiors_p (void)
{
  return find_process ([] (process_info *process) {
    return process->attached;
  }) != NULL;
}

struct process_info *
get_thread_process (const struct thread_info *thread)
{
  return find_process_pid (thread->id.pid ());
}

struct process_info *
current_process (void)
{
  return current_process_;
}

bool
has_current_process ()
{
  return current_process_ != nullptr;
}

/* See inferiors.h.  */

void
for_each_process (gdb::function_view<void (process_info *)> func)
{
  owning_intrusive_list<process_info>::iterator next, cur
    = all_processes.begin ();

  while (cur != all_processes.end ())
    {
      next = cur;
      next++;
      func (&*cur);
      cur = next;
    }
}

/* See inferiors.h.  */

process_info *
find_process (gdb::function_view<bool (process_info *)> func)
{
  for (process_info &process : all_processes)
    if (func (&process))
      return &process;

  return NULL;
}

/* See inferiors.h.  */

thread_info *
process_info::find_thread (gdb::function_view<bool (thread_info *)> func)
{
  for (thread_info &thread : m_thread_list)
    if (func (&thread))
      return &thread;

  return nullptr;
}

/* See gdbthread.h.  */

thread_info *
find_thread (gdb::function_view<bool (thread_info *)> func)
{
  for (process_info &process : all_processes)
    if (thread_info *thread = process.find_thread (func);
	thread != nullptr)
      return thread;

  return NULL;
}

/* See gdbthread.h.  */

thread_info *
find_thread (int pid, gdb::function_view<bool (thread_info *)> func)
{
  process_info *process = find_process_pid (pid);
  if (process == nullptr)
    return nullptr;

  return process->find_thread (func);
}

/* See gdbthread.h.  */

thread_info *
find_thread (ptid_t filter, gdb::function_view<bool (thread_info *)> func)
{
  if (filter == minus_one_ptid)
    return find_thread (func);

  process_info *process = find_process_pid (filter.pid ());
  if (process == nullptr)
    return nullptr;

  if (filter.is_pid ())
    return process->find_thread (func);

  auto &thread_map = process->thread_map ();

  if (auto it = thread_map.find (filter);
      it != thread_map.end () && func (it->second))
    return it->second;

  return nullptr;
}

/* See gdbthread.h.  */

void
for_each_thread (gdb::function_view<void (thread_info *)> func)
{
  for_each_process ([&] (process_info *proc)
    {
      proc->for_each_thread (func);
    });
}

/* See inferiors.h.  */

void
process_info::for_each_thread (gdb::function_view<void (thread_info *)> func)
{
  owning_intrusive_list<thread_info>::iterator next, cur
    = m_thread_list.begin ();

  while (cur != m_thread_list.end ())
    {
      /* FUNC may alter the current iterator.  */
      next = cur;
      next++;
      func (&*cur);
      cur = next;
    }
}

/* See gdbthread.h.  */

void
for_each_thread (int pid, gdb::function_view<void (thread_info *)> func)
{
  process_info *process = find_process_pid (pid);
  if (process == nullptr)
    return;

  process->for_each_thread (func);
}

/* See gdbthread.h.  */

void
for_each_thread (ptid_t ptid, gdb::function_view<void (thread_info *)> func)
{
  if (ptid == minus_one_ptid)
    for_each_thread (func);
  else if (ptid.is_pid ())
    for_each_thread (ptid.pid (), func);
  else
    find_thread (ptid, [func] (thread_info *thread)
      {
	func (thread);
	return false;
      });
}

/* See gdbthread.h.  */

thread_info *
find_thread_in_random (ptid_t ptid,
		       gdb::function_view<bool (thread_info *)> func)
{
  int count = 0;
  int random_selector;

  /* First count how many interesting entries we have.  */
  for_each_thread (ptid, [&] (thread_info *thread)
    {
      if (func (thread))
	count++;
    });

  if (count == 0)
    return nullptr;

  /* Now randomly pick an entry out of those.  */
  random_selector = (int)
    ((count * (double) rand ()) / (RAND_MAX + 1.0));

  thread_info *thread = find_thread (ptid, [&] (thread_info *thr_arg)
    {
      return func (thr_arg) && (random_selector-- == 0);
    });

  gdb_assert (thread != NULL);

  return thread;
}

/* See gdbthread.h.  */

thread_info *
find_thread_in_random (gdb::function_view<bool (thread_info *)> func)
{
  return find_thread_in_random (minus_one_ptid, func);
}

/* See gdbsupport/common-gdbthread.h.  */

void
switch_to_thread (process_stratum_target *ops, ptid_t ptid)
{
  gdb_assert (ptid != minus_one_ptid);
  switch_to_thread (find_thread_ptid (ptid));
}

/* See gdbthread.h.  */

void
switch_to_thread (thread_info *thread)
{
  if (thread != nullptr)
    current_process_ = get_thread_process (thread);
  else
    current_process_ = nullptr;
  current_thread = thread;
}

/* See inferiors.h.  */

void
switch_to_process (process_info *proc)
{
  current_process_ = proc;
  current_thread = nullptr;
}

/* See gdbsupport/common-inferior.h.  */

const std::string &
get_inferior_cwd ()
{
  return current_inferior_cwd;
}

/* See inferiors.h.  */

void
set_inferior_cwd (std::string cwd)
{
  current_inferior_cwd = std::move (cwd);
}

scoped_restore_current_thread::scoped_restore_current_thread ()
{
  m_process = current_process_;
  m_thread = current_thread;
}

scoped_restore_current_thread::~scoped_restore_current_thread ()
{
  if (m_dont_restore)
    return;

  if (m_thread != nullptr)
    switch_to_thread (m_thread);
  else
    switch_to_process (m_process);
}
