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

#ifndef TID_PARSE_H
#define TID_PARSE_H

#include "cli/cli-utils.h"

struct thread_info;

/* Issue an invalid thread ID error, pointing at STRING, the invalid
   ID.  */
extern void ATTRIBUTE_NORETURN invalid_thread_id_error (const char *string);

/* Parse TIDSTR as a per-inferior thread ID, in either INF_NUM.THR_NUM
   or THR_NUM form.  In the latter case, the missing INF_NUM is filled
   in from the current inferior.  If ENDPTR is not NULL,
   parse_thread_id stores the address of the first character after the
   thread ID.  Either a valid thread is returned, or an error is
   thrown.  If SIMD lane number is specified in TIDSTR and SIMD_LANE_NUM is
   not a nullptr, then parsed SIMD lane number is stored in SIMD_LANE_NUM.
   If IS_GLOBAL_ID, then the parsed value is a global ID of a thread.  */
struct thread_info *parse_thread_id (const char *tidstr, const char **end,
				     int *simd_lane_num = nullptr,
				     bool is_global_id = false);

/* Parse TIDSTR as a global thread ID and an optional SIMD lane number.
   If SIMD lane number is specified in TIDSTR and SIMD_LANE_NUM is
   not a nullptr, then parsed SIMD lane number is stored in SIMD_LANE_NUM.  */
thread_info * parse_global_thread_id (const char *tidstr,
				      int *simd_lane_num);

/* Parse a thread ID or a thread range list.

   A range will be of the form

     <inferior_num>.<thread_number1>-<thread_number2>

   and will represent all the threads of inferior INFERIOR_NUM with
   number between THREAD_NUMBER1 and THREAD_NUMBER2, inclusive.
   <inferior_num> can also be omitted, as in

     <thread_number1>-<thread_number2>

   in which case GDB infers the inferior number from the default
   passed to the constructor or to the last call to the init
   function.  */
class tid_range_parser
{
public:
  tid_range_parser () = delete;

  /* Calls init automatically.  See init for description of
     parameters.  */
  tid_range_parser (const char *tidlist, int default_inferior,
		    int default_thr_num);

  /* Reinitialize a tid_range_parser.  TIDLIST is the string to be
     parsed.  DEFAULT_INFERIOR is the inferior number to assume if a
     non-qualified thread ID is found.  DEFAULT_THR_NUM is the thread
     number to assume if no thread ID is found.  */
  void init (const char *tidlist, int default_inferior,
	     int default_thr_num);

  /* Parse a thread ID or a thread range list.  Optionally, parse
     the SIMD lane.

     This function is designed to be called iteratively.  While
     processing a thread ID range list, at each call it will return
     (in the INF_NUM and THR_NUM output parameters) the next thread ID
     in the range (irrespective of whether the thread actually
     exists).

     At the beginning of parsing a thread range, the char pointer
     PARSER->m_cur_tok will be advanced past <thread_number1> and left
     pointing at the '-' token.  Subsequent calls will not advance the
     pointer until the range is completed.  The call that completes
     the range will advance the pointer past <thread_number2>.

     A thread range in a thread range list can be accompanied by a SIMD
     lane range list.  If this is the case, then the SIMD range is parsed
     for every thread in the thread range.  If SIMD_LANE_NUM is specified,
     the next SIMD lane number is returned there.

     This function advances through the input string for as long you
     call it.  Once the end of the input string is reached, a call to
     finished returns false (see below).

     E.g., with list: "1.2 3.4-6:3-4":

     1st call: *INF_NUM=1; *THR_NUM=2 *SIMD_LANE_NUM=-1 (finished==0)
     2nd call: *INF_NUM=3; *THR_NUM=4 *SIMD_LANE_NUM=3 (finished==0)
     3nd call: *INF_NUM=3; *THR_NUM=4 *SIMD_LANE_NUM=4 (finished==0)
     4rd call: *INF_NUM=3; *THR_NUM=5 *SIMD_LANE_NUM=3 (finished==0)
     5rd call: *INF_NUM=3; *THR_NUM=5 *SIMD_LANE_NUM=4 (finished==0)
     6th call: *INF_NUM=3; *THR_NUM=6 *SIMD_LANE_NUM=3 (finished==0)
     7th call: *INF_NUM=3; *THR_NUM=6 *SIMD_LANE_NUM=4 (finished==1)

     Returns true if a thread/range is parsed successfully, false
     otherwise.  */
  bool get_tid (int *inf_num, int *thr_num, int *simd_lane_num);

  /* Like get_tid, but return a thread ID range per call, rather then
     a single thread ID.

     If the next element in the list is a single thread ID, then
     *THR_START and *THR_END are set to the same value.

     E.g.,. with list: "1.2 3.4-6"

     1st call: *INF_NUM=1; *THR_START=2; *THR_END=2 (finished==0)
     2nd call: *INF_NUM=3; *THR_START=4; *THR_END=6 (finished==1)

     Returns true if parsed a thread/range successfully, false
     otherwise.  */
  bool get_tid_range (int *inf_num, int *thr_start, int *thr_end);

  /* Returns true if processing a thread star wildcard (e.g., "1.*")
     range.  */
  bool in_thread_star_range () const;

  /* Returns true if processing a thread range (e.g., 1.2-3 or 1.*).  */
  bool in_thread_state () const;

  /* Returns true if processing a star wildcard (e.g., "1.2:*")
     SIMD lane ID range.  */
  bool in_simd_lane_star_range () const;

  /* Returns true if simd lane range part is parsed now.  */
  bool in_simd_lane_state () const;

 /* Returns true if parsing has completed.  */
  bool finished () const;

  /* Return the current token being parsed.  When parsing has
     finished, this points past the last parsed token.  */
  const char *cur_tok () const;

  /* When parsing a range, advance past the final token in the
     range.  */
  void skip_range ();

  /* Skip parsing SIMD part for the just parsed thread.  Switch back
     to parsing the thread part.  */
  void skip_simd_lane_range ();

  /* True if the TID last parsed was explicitly inferior-qualified.
     IOW, whether the spec specified an inferior number
     explicitly.  */
  bool tid_is_qualified () const;

private:
  /* No need for these.  They are intentionally not defined anywhere.  */
  tid_range_parser (const tid_range_parser &);
  tid_range_parser &operator= (const tid_range_parser &);

  /* Process the inferior state.  */
  bool process_inferior_state (const char *space);
  /* Process the thread state.  */
  bool process_thread_state (const char *space);
  /* Process the SIMD lane state.  */
  bool process_simd_lane_state ();
  bool get_tid_or_range (int *inf_num, int *thr_start, int *thr_end,
			 int *simd_lane);
  const size_t m_simd_max_len = (sizeof (unsigned int)) * 8;

  /* The possible states of the tid range parser's state machine,
     indicating what sub-component are we expecting.  */
  enum
    {
      /* Parsing the inferior number.  */
      STATE_INFERIOR,

      /* Parsing the thread number or thread number range.  */
      STATE_THREAD_RANGE,

      /* Parsing a SIMD lane range.  */
      STATE_SIMD_LANE_RANGE,
    } m_state;

  /* Shows whether we are parsing a thread star range right now.  */
  bool m_in_thread_star_range;

  /* Shows whether we are parsing a SIMD lane star range right now.  */
  bool m_in_simd_lane_star_range;

  /* The string being parsed.  When parsing has finished, this points
     past the last parsed token.  */
  const char *m_cur_tok;

  /* The range parser state when we're parsing the thread number
     sub-component.  */
  number_or_range_parser m_range_parser;

  /* The SIMD lane range parser.  */
  number_or_range_parser m_simd_lane_range_parser;

  /* Last inferior number returned.  */
  int m_inf_num;
  /* Last thread number returned.  */
  int m_thr_num;
  /* Last SIMD lane num returned.  */
  int m_simd_lane_num;

  /* True if the TID last parsed was explicitly inferior-qualified.
     IOW, whether the spec specified an inferior number
     explicitly.  */
  bool m_qualified;

  /* The inferior number to assume if the TID is not qualified.  */
  int m_default_inferior;
  int m_default_thr_num;
};


/* Accept a string-form list of thread IDs such as is accepted by
   tid_range_parser.  Return true if the INF_NUM.THR.NUM thread is in
   the list.  DEFAULT_INFERIOR is the inferior number to assume if a
   non-qualified thread ID is found in the list.

   By definition, an empty list includes all threads.  This is to be
   interpreted as typing a command such as "info threads" with no
   arguments.  */
extern int tid_is_in_list (const char *list, int default_inferior,
			   int inf_num, int thr_num);

#endif /* TID_PARSE_H */
