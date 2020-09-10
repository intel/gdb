/* TID parsing for GDB, the GNU debugger.

   Copyright (C) 2015-2021 Free Software Foundation, Inc.

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
#include "tid-parse.h"
#include "inferior.h"
#include "gdbthread.h"
#include <ctype.h>

/* See tid-parse.h.  */

void ATTRIBUTE_NORETURN
invalid_thread_id_error (const char *string)
{
  error (_("Invalid thread ID: %s"), string);
}

/* Wrapper for get_number_trailer that throws an error if we get back
   a negative number.  We'll see a negative value if the number is
   stored in a negative convenience variable (e.g., $minus_one = -1).
   STRING is the parser string to be used in the error message if we
   do get back a negative number.  */

static bool
get_non_negative_number_trailer (const char **pp, int *parsed_value,
				 int trailer, const char *string)
{
  bool retval = get_number_trailer (pp, parsed_value, trailer);

  if (*parsed_value < 0)
    error (_("negative value: %s"), string);

  return retval;
}

/* See tid-parse.h.  */

struct thread_info *
parse_thread_id (const char *tidstr, const char **end, int *simd_lane_num,
		 bool is_global_id)
{
  const char *number = tidstr;
  const char *dot, *p1;
  struct thread_info *tp = nullptr;
  struct inferior *inf;
  int thr_num;
  int lane_num = -1;
  int explicit_inf_id = 0;

  dot = strchr (number, '.');

  if (!is_global_id)
    {
      if (dot != nullptr)
	{
	  /* Parse number to the left of the dot.  */
	  int inf_num;

	  p1 = number;
	  if (!get_non_negative_number_trailer (&p1, &inf_num, '.', number))
	    invalid_thread_id_error (number);

	  if (inf_num == 0)
	    invalid_thread_id_error (number);

	  inf = find_inferior_id (inf_num);
	  if (inf == nullptr)
	    error (_("No inferior number '%d'"), inf_num);

	  explicit_inf_id = 1;
	  p1 = dot + 1;
	}
      else
	{
	  inf = current_inferior ();

	  p1 = number;
	}
    }
  else
    p1 = number;

  dot = strchr (p1, ':');

  bool lane_specified = (dot != NULL);
  int trailer = lane_specified ? ':' : 0;

  if (p1[0] != ':')
    {
      /* Thread number is presented.  */
      if (!get_non_negative_number_trailer (&p1, &thr_num, trailer, number))
	invalid_thread_id_error (number);

      if (thr_num == 0)
	invalid_thread_id_error (number);

      if (is_global_id)
	{
	  /* We are looking for a thread via its global ID.  */
	  tp = find_thread_global_id (thr_num);

	  if (tp == nullptr)
	    error (_("Unknown thread global ID %d."), thr_num);
	}
      else
	{
	  /* We are looking for a thread via its number within
	     the inferior.  */
	  for (thread_info *it : inf->threads ())
	    if (it->per_inf_num == thr_num)
	      {
		tp = it;
		break;
	      }
	  if (tp == nullptr)
	    {
	      if (show_inferior_qualified_tids () || explicit_inf_id)
		error (_("Unknown thread %d.%d."), inf->num, thr_num);
	      else
		error (_("Unknown thread %d."), thr_num);
	    }
	}
    }
  else
    {
      /* Only lane number is specified.  Take the current thread.  */
      tp = find_thread_ptid (current_inferior (), inferior_ptid);
      if (tp == nullptr)
	error (_("No thread selected."));
    }

  if (lane_specified)
    {
      if (!tp->has_simd_lanes ())
	error (_("Thread %s does not have SIMD lanes."), print_thread_id (tp));

      p1 = dot + 1;

      if (!get_non_negative_number_trailer (&p1, &lane_num, 0, dot))
	error (_("Incorrect SIMD lane number: %s."), dot);

      if (lane_num >= (sizeof (unsigned int)) * 8)
	error (_("Incorrect SIMD lane number: %d."), lane_num);

      if (simd_lane_num != nullptr)
	*simd_lane_num = lane_num;
      else
	error (_("SIMD lane is not supported."));
    }
  else
    {
      if (simd_lane_num != nullptr)
	*simd_lane_num = -1;
    }

  if (end != NULL)
    *end = p1;

  return tp;
}

/* See tid-parse.h.  */

thread_info *
parse_global_thread_id (const char *tidstr, int *simd_lane_num)
{
  return parse_thread_id (tidstr, nullptr, simd_lane_num, true);
}

/* See tid-parse.h.  */

tid_range_parser::tid_range_parser (const char *tidlist,
				    int default_inferior,
				    int default_thr_num)
{
  init (tidlist, default_inferior, default_thr_num);
}

/* See tid-parse.h.  */

void
tid_range_parser::init (const char *tidlist,
			int default_inferior,
			int default_thr_num)
{
  m_state = STATE_INFERIOR;
  m_cur_tok = tidlist;
  m_inf_num = 0;
  m_thr_num = 0;
  m_simd_lane_num = -1;
  m_qualified = false;
  m_default_inferior = default_inferior;
  m_default_thr_num = default_thr_num;
  m_in_thread_star_range = false;
  m_in_simd_lane_star_range = false;
}

/* See tid-parse.h.  */

bool
tid_range_parser::finished () const
{
  switch (m_state)
    {
    case STATE_INFERIOR:
      /* Parsing is finished when at end of string or null string,
	 or we are not in a range and not in front of an integer, negative
	 integer, convenience var or negative convenience var.  */
      return (*m_cur_tok == '\0'
	      || !(isdigit (*m_cur_tok)
		   || *m_cur_tok == '$'
		   || *m_cur_tok == '*'
		   || *m_cur_tok == ':'));
    case STATE_THREAD_RANGE:
      return m_range_parser.finished ();
    case STATE_SIMD_LANE_RANGE:
      return m_simd_lane_range_parser.finished ();
    }

  gdb_assert_not_reached (_("unhandled state"));
}

/* See tid-parse.h.  */

const char *
tid_range_parser::cur_tok () const
{
  switch (m_state)
    {
    case STATE_INFERIOR:
      return m_cur_tok;
    case STATE_THREAD_RANGE:
      return m_range_parser.cur_tok ();
    case STATE_SIMD_LANE_RANGE:
      return m_simd_lane_range_parser.cur_tok ();
    }

  gdb_assert_not_reached (_("unhandled state"));
}

/* See tid-parse.h.  */

bool
tid_range_parser::in_thread_state () const {
  return m_state == STATE_THREAD_RANGE;
}

/* See tid-parse.h.  */

bool
tid_range_parser::in_simd_lane_state () const
{
  return m_state == STATE_SIMD_LANE_RANGE;
}

/* See tid-parse.h.  */

void
tid_range_parser::skip_range ()
{
  gdb_assert (in_thread_state () || in_simd_lane_state ());

  if (m_range_parser.in_range ())
    m_range_parser.skip_range ();

  if (m_simd_lane_range_parser.in_range ())
    m_simd_lane_range_parser.skip_range ();

  const char *cur_tok = m_range_parser.cur_tok ();

  init (cur_tok, m_default_inferior, m_default_thr_num);
}

/* See tid-parse.h.  */

void
tid_range_parser::skip_simd_lane_range ()
{
  gdb_assert (in_simd_lane_state ());
  m_simd_lane_range_parser.skip_range ();
  m_state = STATE_THREAD_RANGE;
}

/* See tid-parse.h.  */

bool
tid_range_parser::tid_is_qualified () const
{
  return m_qualified;
}

/* See tid-parse.h.  */

bool
tid_range_parser::process_inferior_state (const char *space)
{
  const char *p = m_cur_tok;

  while (p < space && *p != '.')
    p++;

  if (p < space)
    {
      const char *dot = p;

      /* Parse number to the left of the dot.  */
      p = m_cur_tok;
      if (!get_non_negative_number_trailer (&p, &m_inf_num, '.', m_cur_tok))
	return false;

      if (m_inf_num == 0)
	error (_("Invalid thread ID 0: %s"), m_cur_tok);

      m_qualified = true;
      p = dot + 1;

      if (isspace (*p))
	return false;
    }
  else
    {
      m_inf_num = m_default_inferior;
      m_qualified = false;
      p = m_cur_tok;
    }

  m_range_parser.init (p, ':');

  m_state = STATE_THREAD_RANGE;
  if (p[0] == '*' && (p[1] == '\0' || p[1] == ':' || isspace (p[1])))
    {
      /* Setup the number range parser to return numbers in the
	 whole [1,INT_MAX] range.  */
      m_range_parser.setup_range (1, INT_MAX, skip_spaces (p + 1));
      m_in_thread_star_range = true;
    }
  else
    m_in_thread_star_range = false;

  return true;
}

/* See tid-parse.h.  */

bool
tid_range_parser::process_thread_state (const char *space)
{
  bool thread_is_parsed = m_range_parser.get_number (&m_thr_num);

  /* Even if the thread parser failed, we want to check if SIMD lane
     range is specified.  */

  if (thread_is_parsed && m_thr_num < 0)
    error (_("negative value: %s"), m_cur_tok);

  if (thread_is_parsed && m_thr_num == 0)
    error (_("Invalid thread ID 0: %s"), m_cur_tok);

  const char *colon = strchr (m_cur_tok, ':');

  if (colon != nullptr && colon < space)
    {
      /* A colon is presented in a current token before the space.
	 That means, that for the current thread range, a SIMD lane
	 range is specified.  */

      m_range_parser.set_end_ptr (skip_spaces (space));

      /* When thread ID is skipped, thread parser returns false.
	 In that case, return the default thread.  */
      if (!thread_is_parsed && m_cur_tok[0] == ':')
	m_thr_num = m_default_thr_num;

      /* Step over the colon.  */
      colon++;
      m_simd_lane_range_parser.init (colon);
      m_state = STATE_SIMD_LANE_RANGE;

      if (colon[0] == '*' && (colon[1] == '\0' || isspace (colon[1])))
	{
	  m_simd_lane_range_parser.setup_range (0, m_simd_max_len - 1,
						skip_spaces (colon + 1));
	  m_in_simd_lane_star_range = true;
	}
      else
	m_in_simd_lane_star_range = false;
    }

  return thread_is_parsed;
}

/* See tid-parse.h.  */

bool
tid_range_parser::process_simd_lane_state ()
{
  int simd_lane_num;
  if (!m_simd_lane_range_parser.get_number (&simd_lane_num))
    {
      /* SIMD lanes are specified, but its parsing failed.  */
      m_state = STATE_INFERIOR;
      return false;
    }

  if (simd_lane_num >= m_simd_max_len)
    {
      /* Too large SIMD lane number was specified.  */
      error (_("Incorrect SIMD lane number: %d."), simd_lane_num);
    }

  m_simd_lane_num = simd_lane_num;
  return true;
}

/* Helper for tid_range_parser::get_tid and
   tid_range_parser::get_tid_range.  Return the next range if THR_END
   is non-NULL, return a single thread ID otherwise.  */

bool
tid_range_parser::get_tid_or_range (int *inf_num,
				    int *thr_start, int *thr_end,
				    int *simd_lane_num)
{
  /* Only one out of thr_end and simd_lane is allowed to be specified.  */
  gdb_assert (simd_lane_num == nullptr || thr_end == nullptr);

  const char *space;

  space = skip_to_space (m_cur_tok);

  if (m_state == STATE_INFERIOR)
    {
      if (!process_inferior_state (space))
	return false;
    }

  *inf_num = m_inf_num;

  bool thread_is_parsed = false;

  if (in_thread_state ())
      thread_is_parsed = process_thread_state (space);

  if (in_thread_state () && !thread_is_parsed)
    {
      /* Thread number was not parsed successfully and SIMD lanes are
	 not specified.  */
      m_state = STATE_INFERIOR;
      return false;
    }

  if (in_simd_lane_state ())
    {
      if (!process_simd_lane_state ())
	{
	  m_state = STATE_INFERIOR;
	  return false;
	}

    }
  else
    m_simd_lane_num = -1;

  *inf_num = m_inf_num;
  *thr_start = m_thr_num;

  if (simd_lane_num != nullptr)
    *simd_lane_num = m_simd_lane_num;

  /* If SIMD lane range is finished,  check if thread range is finished.  */
  if (!in_simd_lane_state () || !m_simd_lane_range_parser.in_range ())
    {
      /* If we successfully parsed a thread number or finished parsing a
	 thread range, switch back to assuming the next TID is
	 inferior-qualified.  */
      if (!m_range_parser.in_range ())
	{
	  if (in_thread_state ())
	    {
	      /* SIMD range was not specified.  */
	      m_cur_tok = m_range_parser.cur_tok ();
	    }
	  else if (in_simd_lane_state ())
	    {
	      /* SIMD range was specified.  */
	      m_cur_tok = m_simd_lane_range_parser.cur_tok ();
	    }

	  m_state = STATE_INFERIOR;
	  m_in_thread_star_range = false;
	  m_in_simd_lane_star_range = false;

	  if (thr_end != nullptr)
	    *thr_end = *thr_start;
	}
      else
	{
	  /* Thread range is not yet finished.  Go back to the old thread
	     state.  */
	  m_state = STATE_THREAD_RANGE;
	}
    }

  /* If we're midway through a range, and the caller wants the end
     value, return it and skip to the end of the range.  */
  if (thr_end != nullptr && (in_thread_state () || in_simd_lane_state ()))
    {
      *thr_end = m_range_parser.end_value ();

      skip_range ();
    }

  return true;
}

/* See tid-parse.h.  */

bool
tid_range_parser::get_tid_range (int *inf_num, int *thr_start, int *thr_end)
{
  gdb_assert (inf_num != NULL && thr_start != NULL && thr_end != NULL);

  return get_tid_or_range (inf_num, thr_start, thr_end, nullptr);
}

/* See tid-parse.h.  */

bool
tid_range_parser::get_tid (int *inf_num, int *thr_num, int *simd_lane_num)
{
  gdb_assert (inf_num != NULL && thr_num != NULL);

  return get_tid_or_range (inf_num, thr_num, nullptr, simd_lane_num);
}

/* See tid-parse.h.  */

bool
tid_range_parser::in_thread_star_range () const
{
  return (in_thread_state () || in_simd_lane_state ())
    && m_in_thread_star_range;
}

/* See tid-parse.h.  */

bool
tid_range_parser::in_simd_lane_star_range () const
{
  return in_simd_lane_state () && m_in_simd_lane_star_range;
}

/* See tid-parse.h.  */

int
tid_is_in_list (const char *list, int default_inferior,
		int inf_num, int thr_num)
{
  if (list == NULL || *list == '\0')
    return 1;

  tid_range_parser parser (list, default_inferior, 0);
  if (parser.finished ())
    invalid_thread_id_error (parser.cur_tok ());
  while (!parser.finished ())
    {
      int tmp_inf, tmp_thr_start, tmp_thr_end;

      if (!parser.get_tid_range (&tmp_inf, &tmp_thr_start, &tmp_thr_end))
	invalid_thread_id_error (parser.cur_tok ());
      if (tmp_inf == inf_num
	  && tmp_thr_start <= thr_num && thr_num <= tmp_thr_end)
	return 1;
    }
  return 0;
}
