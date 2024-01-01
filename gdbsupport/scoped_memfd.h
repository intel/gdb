/* Copyright (C) 2026 Free Software Foundation, Inc.

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

#ifndef GDBSUPPORT_SCOPED_MEMFD_H
#define GDBSUPPORT_SCOPED_MEMFD_H

#include <unistd.h>
#include <errno.h>
#include "scoped_fd.h"

#ifdef __linux__
#include <sys/mman.h>
#endif

/* A smart-pointer-like class for a memory-backed file descriptor.
   Uses memfd_create to create a real file descriptor backed by memory.
   Automatically closes on destruction via base class.  */

class scoped_memfd : public scoped_fd
{
public:
  scoped_memfd () noexcept : scoped_fd (-1) {};


  /* Read BUF with SIZE and create an anonymous file FILENAME in volatile
     memory.

     The returned file descriptor behaves like a regular file, i.e., it can
     be modified, truncated and so on.  */

  scoped_memfd (const void *buf, size_t size, const char *filename) noexcept
    : scoped_fd (-1)
  {
#ifdef __linux__
    if (buf == nullptr)
      return;

    m_fd = memfd_create (filename, MFD_CLOEXEC);
    if (m_fd < 0)
      return;

    /* Write BUF to the in-memory file.  */
    size_t total_written = 0;
    while (total_written < size)
      {
	const char *offset = static_cast<const char *> (buf) + total_written;
	ssize_t written = write (m_fd, offset, size - total_written);

	/* Error Handling.  */
	if (written == -1)
	  {
	    /* The call was interrupted by a signal before any data was
	       written.  */
	    if (errno == EINTR)
	      continue;

	    close (m_fd);
	    m_fd = -1;
	    return;
	  }

	total_written += written;
      }

    /* Seek back to the beginning for reading.  */
    if (lseek (m_fd, 0, SEEK_SET) != 0)
      {
	close (m_fd);
	m_fd = -1;
      }

#else
    warning (_("Opening a file descriptor from memory is not supported."));
#endif /* __linux__ */
  }

  DISABLE_COPY_AND_ASSIGN (scoped_memfd);
};

#endif /* GDBSUPPORT_SCOPED_MEMFD_H */
