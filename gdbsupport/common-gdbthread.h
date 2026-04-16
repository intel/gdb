/* Common multi-process/thread control defs for GDB and gdbserver.
   Copyright (C) 1987-2026 Free Software Foundation, Inc.
   Copyright (C) 2019-2026 Intel Corporation.

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

#ifndef GDBSUPPORT_COMMON_GDBTHREAD_H
#define GDBSUPPORT_COMMON_GDBTHREAD_H

#include "inttypes.h"

/* 64-bits is sufficient for all known architectures.  */
typedef uint64_t lanes_mask_t;
#define PRI_lanes_mask PRIx64

struct process_stratum_target;

/* Switch from one thread to another.  */
extern void switch_to_thread (process_stratum_target *proc_target,
			      ptid_t ptid);

#endif /* GDBSUPPORT_COMMON_GDBTHREAD_H */
