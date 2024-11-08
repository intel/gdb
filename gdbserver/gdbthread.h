/* Multi-thread control defs for remote server for GDB.
   Copyright (C) 1993-2024 Free Software Foundation, Inc.
   Copyright (C) 2021-2024 Intel Corporation

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

#ifndef GDBSERVER_GDBTHREAD_H
#define GDBSERVER_GDBTHREAD_H

#include "gdbsupport/common-gdbthread.h"
#include "inferiors.h"

#include <list>

struct btrace_target_info;
struct regcache;

struct thread_info
{
  thread_info (ptid_t id, void *target_data)
    : id (id), target_data (target_data)
  {}

  ~thread_info ()
  {
    delete this->regcache_data;
  }

  /* The id of this thread.  */
  ptid_t id;

  void *target_data;
  struct regcache *regcache_data = nullptr;

  /* The last resume GDB requested on this thread.  */
  enum resume_kind last_resume_kind = resume_continue;

  /* The last wait status reported for this thread.  */
  struct target_waitstatus last_status;

  /* True if LAST_STATUS hasn't been reported to GDB yet.  */
  int status_pending_p = 0;

  /* Given `while-stepping', a thread may be collecting data for more
     than one tracepoint simultaneously.  E.g.:

    ff0001  INSN1 <-- TP1, while-stepping 10 collect $regs
    ff0002  INSN2
    ff0003  INSN3 <-- TP2, collect $regs
    ff0004  INSN4 <-- TP3, while-stepping 10 collect $regs
    ff0005  INSN5

   Notice that when instruction INSN5 is reached, the while-stepping
   actions of both TP1 and TP3 are still being collected, and that TP2
   had been collected meanwhile.  The whole range of ff0001-ff0005
   should be single-stepped, due to at least TP1's while-stepping
   action covering the whole range.

   On the other hand, the same tracepoint with a while-stepping action
   may be hit by more than one thread simultaneously, hence we can't
   keep the current step count in the tracepoint itself.

   This is the head of the list of the states of `while-stepping'
   tracepoint actions this thread is now collecting; NULL if empty.
   Each item in the list holds the current step of the while-stepping
   action.  */
  struct wstep_state *while_stepping = nullptr;

  /* Branch trace target information for this thread.  */
  struct btrace_target_info *btrace = nullptr;

  /* Thread options GDB requested with QThreadOptions.  */
  gdb_thread_options thread_options = 0;

  /* Target description for this thread.  Only present if it's different
     from the one in process_info.  */
  const struct target_desc *tdesc = nullptr;
};

extern std::list<process_info *> all_processes;

void remove_thread (struct thread_info *thread);
struct thread_info *add_thread (ptid_t ptid, void *target_data);

/* Return a pointer to the first thread, or NULL if there isn't one.  */

struct thread_info *get_first_thread (void);

struct thread_info *find_thread_ptid (ptid_t ptid);

/* Find any thread of the PID process.  Returns NULL if none is
   found.  */
struct thread_info *find_any_thread_of_pid (int pid);

/* Find the first thread for which FUNC returns true, only consider
   threads in the thread list of PROCESS.  Return NULL if no thread
   that satisfies FUNC is found.  */

template <typename Func>
static thread_info *
find_thread (process_info *process, Func func)
{
  std::list<thread_info *> *thread_list = get_thread_list (process);
  std::list<thread_info *>::iterator next, cur = thread_list->begin ();

  while (cur != thread_list->end ())
    {
      /* FUNC may alter the current iterator.  */
      next = cur;
      next++;

      if (func (*cur))
	return *cur;

      cur = next;
    }

  return nullptr;
}

/* Like the above, but consider all threads of all processes.  */

template <typename Func>
static thread_info *
find_thread (Func func)
{
  for (process_info *proc : all_processes)
    {
      thread_info *thread = find_thread (proc, func);
      if (thread != nullptr)
	return thread;
    }

  return nullptr;
}

/* Like the above, but only consider threads with pid PID.  */

template <typename Func>
static thread_info *
find_thread (int pid, Func func)
{
  process_info *process = find_process_pid (pid);
  if (process == nullptr)
    return nullptr;

  return find_thread (process, func);
}

/* Find the first thread that matches FILTER for which FUNC returns true.
   Return NULL if no thread satisfying these conditions is found.  */

template <typename Func>
static thread_info *
find_thread (ptid_t filter, Func func)
{
  if (filter == minus_one_ptid)
    return find_thread (func);

  process_info *process = find_process_pid (filter.pid ());
  if (process == nullptr)
    return nullptr;

  if (filter.is_pid ())
    return find_thread (process, func);

  std::unordered_map<ptid_t, thread_info *> *thread_map
    = get_thread_map (process);
  std::unordered_map<ptid_t, thread_info *>::iterator it
    = thread_map->find (filter);
  if (it != thread_map->end () && func (it->second))
    return it->second;

  return nullptr;
}

/* Invoke FUNC for each thread in the thread list of PROCESS.  */

template <typename Func>
static void
for_each_thread (process_info *process, Func func)
{
  std::list<thread_info *> *thread_list = get_thread_list (process);
  std::list<thread_info *>::iterator next, cur
    = thread_list->begin ();

  while (cur != thread_list->end ())
    {
      /* FUNC may alter the current iterator.  */
      next = cur;
      next++;
      func (*cur);
      cur = next;
    }
}

/* Invoke FUNC for each thread.  */

template <typename Func>
static void
for_each_thread (Func func)
{
  for_each_process ([&] (process_info *proc)
    {
      for_each_thread (proc, func);
    });
}

/* Like the above, but only consider threads with pid PID.  */

template <typename Func>
static void
for_each_thread (int pid, Func func)
{
  process_info *process = find_process_pid (pid);
  if (process == nullptr)
    return;

  for_each_thread (process, func);
}

/* Like the above, but only consider threads matching PTID.  */

template <typename Func>
static void
for_each_thread (ptid_t ptid, Func func)
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

/* Find a random thread that matches PTID and FUNC (THREAD)
   returns true.  If no entry is found then return NULL.  */

template <typename Func>
static thread_info *
find_thread_in_random (ptid_t ptid, Func func)
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

/* Find the random thread for which FUNC (THREAD) returns true.  If
   no entry is found then return NULL.  */

template <typename Func>
static thread_info *
find_thread_in_random (Func func)
{
  return find_thread_in_random (minus_one_ptid, func);
}

/* Get current thread ID (Linux task ID).  */
#define current_ptid (current_thread->id)

/* Get the ptid of THREAD.  */

static inline ptid_t
ptid_of (const thread_info *thread)
{
  return thread->id;
}

/* Get the pid of THREAD.  */

static inline int
pid_of (const thread_info *thread)
{
  return thread->id.pid ();
}

/* Get the lwp of THREAD.  */

static inline long
lwpid_of (const thread_info *thread)
{
  return thread->id.lwp ();
}

/* Switch the current thread.  */

void switch_to_thread (thread_info *thread);

/* Save/restore current thread.  */

class scoped_restore_current_thread
{
public:
  scoped_restore_current_thread ();
  ~scoped_restore_current_thread ();

  DISABLE_COPY_AND_ASSIGN (scoped_restore_current_thread);

  /* Cancel restoring on scope exit.  */
  void dont_restore () { m_dont_restore = true; }

private:
  bool m_dont_restore = false;
  process_info *m_process;
  thread_info *m_thread;
};

#endif /* GDBSERVER_GDBTHREAD_H */
