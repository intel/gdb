/* CLI utilities.

   Copyright (C) 2011-2025 Free Software Foundation, Inc.
   Copyright (C) 2019-2025 Intel Corporation

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

#include "cli/cli-utils.h"
#include "value.h"
#include "algorithm"

#include <ctype.h>

/* See documentation in cli-utils.h.  */

ULONGEST
get_ulongest (const char **pp, int trailer)
{
  LONGEST retval = 0;	/* default */
  const char *p = *pp;

  if (*p == '$')
    {
      value *val = value_from_history_ref (p, &p);

      if (val != NULL)	/* Value history reference */
	{
	  if (val->type ()->code () == TYPE_CODE_INT)
	    retval = value_as_long (val);
	  else
	    error (_("History value must have integer type."));
	}
      else	/* Convenience variable */
	{
	  /* Internal variable.  Make a copy of the name, so we can
	     null-terminate it to pass to lookup_internalvar().  */
	  const char *start = ++p;
	  while (isalnum (*p) || *p == '_')
	    p++;
	  std::string varname (start, p - start);
	  if (!get_internalvar_integer (lookup_internalvar (varname.c_str ()),
				       &retval))
	    error (_("Convenience variable $%s does not have integer value."),
		   varname.c_str ());
	}
    }
  else
    {
      const char *end = p;
      retval = strtoulst (p, &end, 0);
      if (p == end)
	{
	  /* There is no number here.  (e.g. "cond a == b").  */
	  error (_("Expected integer at: %s"), p);
	}
      p = end;
    }

  if (!(isspace (*p) || *p == '\0' || *p == trailer))
    error (_("Trailing junk at: %s"), p);
  p = skip_spaces (p);
  *pp = p;
  return retval;
}

/* See documentation in cli-utils.h.  */

bool
get_number_trailer (const char **pp, int *parsed_value, int trailer)
{
  return NUMBER_OK == get_number_trailer_overflow (pp, parsed_value, trailer);
}

/* See documentation in cli-utils.h.  */

get_number_status
get_number_trailer_overflow (const char **pp, int *num, int trailer)
{
  get_number_status retval = NUMBER_OK;	/* Default.  */
  int parsed_value = 0;
  const char *p = *pp;
  bool negative = false;

  if (*p == '-')
    {
      ++p;
      negative = true;
    }

  if (*p == '$')
    {
      struct value *val = value_from_history_ref (p, &p);

      if (val)	/* Value history reference */
	{
	  if (val->type ()->code () == TYPE_CODE_INT)
	    {
	      parsed_value = value_as_long (val);
	      retval = NUMBER_OK;
	    }
	  else
	    {
	      gdb_printf (_("History value must have integer type.\n"));
	      retval = NUMBER_ERROR;
	    }
	}
      else	/* Convenience variable */
	{
	  /* Internal variable.  Make a copy of the name, so we can
	     null-terminate it to pass to lookup_internalvar().  */
	  char *varname;
	  const char *start = ++p;
	  LONGEST longest_val;

	  while (isalnum (*p) || *p == '_')
	    p++;
	  varname = (char *) alloca (p - start + 1);
	  strncpy (varname, start, p - start);
	  varname[p - start] = '\0';
	  if (get_internalvar_integer (lookup_internalvar (varname),
				       &longest_val))
	    {
	      parsed_value = (int) longest_val;
	      retval = NUMBER_OK;
	    }
	  else
	    {
	      gdb_printf (_("Convenience variable must "
			    "have integer value.\n"));
	      retval = NUMBER_ERROR;
	    }
	}
    }
  else
    {
      const char *p1 = p;
      while (*p >= '0' && *p <= '9')
	++p;
      if (p == p1)
	{
	  /* Skip non-numeric token.  */
	  while (*p && !isspace((int) *p))
	    ++p;
	  /* Return zero, which caller must interpret as error.  */
	  retval = NUMBER_ERROR;
	}
      else
	{
	  long val = strtol (p1, nullptr, 10);
	  /* Value here is always positive, we stripped a potential negative
	     sign.  Tell the caller about that but also verify a valid int
	     representation.  */
	  parsed_value = 0;
	  if ((val == LONG_MAX && errno == ERANGE) || val > INT_MAX)
	    retval = NUMBER_CONVERSION_ERROR;
	  else
	    {
	      parsed_value = (int)val;
	      retval = NUMBER_OK;
	    }
	}
    }
  if (!(isspace (*p) || *p == '\0' || *p == trailer))
    {
      /* Trailing junk: return 0 and let caller print error msg.  */
      while (!(isspace (*p) || *p == '\0' || *p == trailer))
	++p;
      retval = NUMBER_ERROR;
    }
  p = skip_spaces (p);
  *pp = p;

  if (num != nullptr)
    *num = negative ? -parsed_value : parsed_value;
  return retval;
}

/* See documentation in cli-utils.h.  */

bool
get_number (const char **pp, int *num)
{
  return NUMBER_OK == get_number_overflow (pp, num);
}

/* See documentation in cli-utils.h.  */

get_number_status
get_number_overflow (const char **pp, int *num)
{
  return get_number_trailer_overflow (pp, num, '\0');
}

/* See documentation in cli-utils.h.  */

bool
get_number (char **pp, int *num)
{
  return NUMBER_OK == get_number_overflow (pp, num);
}

/* See documentation in cli-utils.h.  */

get_number_status
get_number_overflow (char **pp, int *num)
{
  const char *p = *pp;
  get_number_status result = get_number_trailer_overflow (&p, num, '\0');
  *pp = (char *) p;
  return result;
}

/* See documentation in cli-utils.h.  */

void
report_unrecognized_option_error (const char *command, const char *args)
{
  std::string option = extract_arg (&args);

  error (_("Unrecognized option '%s' to %s command.  "
	   "Try \"help %s\"."), option.c_str (),
	 command, command);
}

/* See documentation in cli-utils.h.  */

const char *
info_print_args_help (const char *prefix,
		      const char *entity_kind,
		      bool document_n_flag)
{
  return xstrprintf (_("\
%sIf NAMEREGEXP is provided, only prints the %s whose name\n\
matches NAMEREGEXP.\n\
If -t TYPEREGEXP is provided, only prints the %s whose type\n\
matches TYPEREGEXP.  Note that the matching is done with the type\n\
printed by the 'whatis' command.\n\
By default, the command might produce headers and/or messages indicating\n\
why no %s can be printed.\n\
The flag -q disables the production of these headers and messages.%s"),
		     prefix, entity_kind, entity_kind, entity_kind,
		     (document_n_flag ? _("\n\
By default, the command will include non-debug symbols in the output;\n\
these can be excluded using the -n flag.") : "")).release ();
}

/* See documentation in cli-utils.h.  */

number_or_range_parser::number_or_range_parser (const char *string)
{
  init (string);
}

/* See documentation in cli-utils.h.  */

void
number_or_range_parser::init (const char *string, int end_trailer)
{
  m_cur_tok = string;
  m_last_retval = 0;
  m_end_value = 0;
  m_end_ptr = NULL;
  m_in_range = false;
  m_in_set = false;
  m_end_trailer = end_trailer;
}

/* See documentation in cli-utils.h.  */

bool
number_or_range_parser::get_number (int *num)
{
  return NUMBER_OK == get_number_overflow (num);
}

/* See documentation in cli-utils.h.  */

enum get_number_status
number_or_range_parser::get_number_overflow (int *num)
{
  get_number_status retval = NUMBER_ERROR;

  if (m_in_range)
    {
      /* All number-parsing has already been done.  Return the next
	 integer value (one greater than the saved previous value).
	 Do not advance the token pointer until the end of range is
	 reached.  */

      retval = NUMBER_OK;

      if (++m_last_retval == m_end_value)
	{
	  /* End of range reached; advance token pointer.  */
	  m_cur_tok = m_end_ptr;
	  m_in_range = false;

	  /* Reset state->m_in_set if range is enclosed in
	     square brackets.  */
	  if (m_cur_tok[-1] == ']')
	    m_in_set = false;
	}
    }
  else if (*m_cur_tok != '-')
    {
      if (*m_cur_tok == '[')
	{
	  /* Return error if multiple opening brackets are used.  */
	  if (m_in_set)
	    return retval;

	  m_in_set = true;
	  m_cur_tok++;
	}

      const char *restore_tok = m_cur_tok;
      /* Default case: state->m_cur_tok is pointing either to a solo
	 number, or to the first number of a range.  */
      retval = ::get_number_trailer_overflow (&m_cur_tok, &m_last_retval, '-');
      if (NUMBER_OK != retval)
	{

	  /* Make another attempt to check for the last number of a set.  */
	  if (m_in_set)
	    {
	      m_cur_tok = restore_tok;
	      retval = ::get_number_trailer_overflow (&m_cur_tok,
						      &m_last_retval, ']');
	    }

	  if (NUMBER_OK != retval)
	    {
	      /* Make another attempt only if there is a custom end trailer.  */
	      if (m_end_trailer == 0)
		return retval;

	      m_cur_tok = restore_tok;
	      retval = ::get_number_trailer_overflow (&m_cur_tok,
						      &m_last_retval,
						      m_end_trailer);

	      if (NUMBER_OK != retval)
		return retval;
	    }
	}

      /* If get_number_trailer_overflow has found a '-' preceded by a space, it
	 might be the start of a command option.  So, do not parse a
	 range if the '-' is followed by an alpha or another '-'.  We
	 might also be completing something like
	 "frame apply level 0 -" and we prefer treating that "-" as an
	 option rather than an incomplete range, so check for end of
	 string as well.  */
      if (m_cur_tok[0] == '-'
	  && !(isspace (m_cur_tok[-1])
	       && (isalpha (m_cur_tok[1])
		   || m_cur_tok[1] == '-'
		   || m_cur_tok[1] == '\0')))
	{
	  const char **temp;

	  /* This is the start of a range (<number1> - <number2>).
	     Skip the '-', parse and remember the second number,
	     and also remember the end of the final token.  */

	  temp = &m_end_ptr;
	  m_end_ptr = skip_spaces (m_cur_tok + 1);

	  if (m_in_set)
	    retval = ::get_number_trailer_overflow (temp, &m_end_value,
						    ']');
	  else
	    retval = ::get_number_trailer_overflow (temp, &m_end_value,
						    m_end_trailer);

	  if (NUMBER_OK != retval)
	    {
	      /* Advance the token pointer behind the failed range.  */
	      m_cur_tok = m_end_ptr;

	      return retval;
	    }

	  if (m_end_value < m_last_retval)
	    {
	      error (_("inverted range"));
	    }
	  else if (m_end_value == m_last_retval)
	    {
	      /* Degenerate range (number1 == number2).  Advance the
		 token pointer so that the range will be treated as a
		 single number.  */
	      m_cur_tok = m_end_ptr;
	    }
	  else
	    m_in_range = true;
	}
    }
  else
    {
      if (isdigit (*(m_cur_tok + 1)))
	error (_("negative value"));
      if (*(m_cur_tok + 1) == '$')
	{
	  /* Convenience variable.  */
	  retval = ::get_number_overflow (&m_cur_tok, &m_last_retval);
	  if (NUMBER_OK != retval)
	    return retval;

	  if (m_last_retval < 0)
	    error (_("negative value"));
	}
    }

  /* Advance token pointer if the last element of a set is processed.  */
  if (*m_cur_tok == ']')
    {
      if (!m_in_set)
	return NUMBER_CONVERSION_ERROR;
      else
	m_in_set = false;

      m_cur_tok++;
      m_cur_tok = skip_spaces (m_cur_tok);
    }

  if (num != nullptr)
    *num = m_last_retval;

  return retval;
}

/* See documentation in cli-utils.h.  */

void
number_or_range_parser::setup_range (int start_value, int end_value,
				     const char *end_ptr)
{
  gdb_assert (start_value >= 0);

  m_in_range = true;
  m_end_ptr = end_ptr;
  m_last_retval = start_value - 1;
  m_end_value = end_value;
}

void
number_or_range_parser::set_end_ptr (const char *end_ptr)
{
  m_end_ptr = end_ptr;
}

/* See documentation in cli-utils.h.  */

bool
number_or_range_parser::finished () const
{
  /* Parsing is finished when at end of string or null string,
     or we are not in a range and not in front of an integer, negative
     integer, convenience var or negative convenience var.  */

  if (m_cur_tok == nullptr || *m_cur_tok == '\0')
    return true;

  bool is_digit = isdigit (*m_cur_tok) || *m_cur_tok == '$';
  bool is_range = *m_cur_tok == '-' && (isdigit (m_cur_tok[1])
					|| m_cur_tok[1] == '$');

  return !m_in_set && !m_in_range && !is_digit && !is_range;
}

/* Accept a number and a string-form list of numbers such as is 
   accepted by get_number_or_range.  Return TRUE if the number is
   in the list.

   By definition, an empty list includes all numbers.  This is to 
   be interpreted as typing a command such as "delete break" with 
   no arguments.  */

int
number_is_in_list (const char *list, int number)
{
  if (list == NULL || *list == '\0')
    return 1;

  number_or_range_parser parser (list);

  if (parser.finished ())
    error (_("Arguments must be numbers or '$' variables."));
  while (!parser.finished ())
    {
      int gotnum = 0;

      if (!parser.get_number (&gotnum))
	error (_("Arguments must be numbers or '$' variables."));
      else if (gotnum == 0)
	error (_("Zero is not a valid index."));
      if (gotnum == number)
	return 1;
    }
  return 0;
}

/* See documentation in cli-utils.h.  */

const char *
remove_trailing_whitespace (const char *start, const char *s)
{
  while (s > start && isspace (*(s - 1)))
    --s;

  return s;
}

/* See documentation in cli-utils.h.  */

const char*
skip_to_next (const char *chp)
{
  if (chp == nullptr)
    return nullptr;

  bool opening_bracket = false;

  while (*chp)
    {
      if (*chp == '[')
	opening_bracket = true;
      else if (isspace (*chp) && !opening_bracket)
	break;
      else if (*chp == ']')
	opening_bracket = false;

      chp++;
    }

  return skip_spaces (chp);
}

/* See documentation in cli-utils.h.  */

std::string
extract_arg (const char **arg)
{
  const char *result;

  if (!*arg)
    return std::string ();

  /* Find the start of the argument.  */
  *arg = skip_spaces (*arg);
  if (!**arg)
    return std::string ();
  result = *arg;

  /* Find the end of the argument.  */
  *arg = skip_to_space (*arg + 1);

  if (result == *arg)
    return std::string ();

  return std::string (result, *arg - result);
}

/* See documentation in cli-utils.h.  */

std::string
extract_arg (char **arg)
{
  const char *arg_const = *arg;
  std::string result;

  result = extract_arg (&arg_const);
  *arg += arg_const - *arg;
  return result;
}

/* See documentation in cli-utils.h.  */

int
check_for_argument (const char **str, const char *arg, int arg_len)
{
  if (strncmp (*str, arg, arg_len) == 0
      && ((*str)[arg_len] == '\0' || isspace ((*str)[arg_len])))
    {
      *str += arg_len;
      *str = skip_spaces (*str);
      return 1;
    }
  return 0;
}

/* See documentation in cli-utils.h.  */

void
validate_flags_qcs (const char *which_command, qcs_flags *flags)
{
  if (flags->cont && flags->silent)
    error (_("%s: -c and -s are mutually exclusive"), which_command);
}

/* See documentation in cli-utils.h.  */

std::string
make_ranges_from_mask (unsigned long mask, int current)
{
  std::string result;

  /* Pre-allocate memory for the resulting output.  In worst case,
     we have a mask with alternating bits set, e.g., 0xAAAA AAAA.  */
  result.reserve (64);
  bool has_brackets = false;

  auto print_range = ([&] (const int start, const int end)
    {
      if (!result.empty ())
	{
	  result += " ";
	  has_brackets = true;
	}

      result += std::to_string (start);
      if (start < end)
	{
	  result += "-" + std::to_string (end);
	  has_brackets = true;
	}
    });

  int start = -1;
  int prev = -1;
  for (int bitnum = 0; mask != 0; mask >>= 1, bitnum++)
    {
      if ((mask & 0x1) == 0x0)
	continue;

      if (start == -1)
	{
	  /* The active lane is the current lane.  */
	  if (bitnum == current)
	    {
	      if (!result.empty ())
		result += " ";
	      result += "*" + std::to_string (bitnum);
	    }
	  else
	    start = bitnum;
	}
      else if (bitnum == current)
	{
	  print_range (start, prev);
	  result += " *" + std::to_string (bitnum);

	  /* 'print_range' may not update 'has_brackets' if 'start == prev'
	     and 'lane_mask' does not contain any output yet.  */
	  has_brackets = true;

	  /* Reset to start a new range.  */
	  start = -1;
	}
      else if ((bitnum - prev) > 1)
	{
	  print_range (start, prev);
	  start = bitnum;
	}

      prev = bitnum;
    }

  /* Print last lane.  */
  if (start != -1)
    print_range (start, prev);

  if (has_brackets)
    result = "[" + result + "]";

  return result;
}

#if GDB_SELF_TEST
#include "gdbsupport/selftest.h"

namespace selftests {

/* Test 'make_ranges_from_mask'.  */

static void
test_make_ranges_from_mask ()
{
  /* Test with empty mask.  */
  SELF_CHECK (make_ranges_from_mask (0) == "");
  SELF_CHECK (make_ranges_from_mask (0, 1) == "");

  /* Test with one bit set in mask.  */
  SELF_CHECK (make_ranges_from_mask (1, -1) == "0");
  SELF_CHECK (make_ranges_from_mask (1, 0) == "*0");
  SELF_CHECK (make_ranges_from_mask (1, 1) == "0");
  SELF_CHECK (make_ranges_from_mask (2, -1) == "1");
  SELF_CHECK (make_ranges_from_mask (2, 1) == "*1");

  /* Test with two bits set in mask.  */
  SELF_CHECK (make_ranges_from_mask (3, -1) == "[0-1]");
  SELF_CHECK (make_ranges_from_mask (3, 0) == "[*0 1]");
  SELF_CHECK (make_ranges_from_mask (3, 1) == "[0 *1]");
  SELF_CHECK (make_ranges_from_mask (3, 2) == "[0-1]");

  /* Test with four alternating bits set.  */
  SELF_CHECK (make_ranges_from_mask (0xAA) == "[1 3 5 7]");
  SELF_CHECK (make_ranges_from_mask (0xAA, 0) == "[1 3 5 7]");

  /* Tests with odd bits set.  */
  SELF_CHECK (make_ranges_from_mask (0xAA, 1) == "[*1 3 5 7]");
  SELF_CHECK (make_ranges_from_mask (0xAA, 3) == "[1 *3 5 7]");
  SELF_CHECK (make_ranges_from_mask (0xAA, 7) == "[1 3 5 *7]");

  /* Tests with consecutive blocks of bits set.  */
  SELF_CHECK (make_ranges_from_mask (0xF0F0) == "[4-7 12-15]");
  SELF_CHECK (make_ranges_from_mask (0xF0F0, 0) == "[4-7 12-15]");
  SELF_CHECK (make_ranges_from_mask (0xF0F0, 3) == "[4-7 12-15]");
  SELF_CHECK (make_ranges_from_mask (0xF0F0, 4) == "[*4 5-7 12-15]");
  SELF_CHECK (make_ranges_from_mask (0xF0F0, 6) == "[4-5 *6 7 12-15]");
  SELF_CHECK (make_ranges_from_mask (0xF0F0, 12) == "[4-7 *12 13-15]");
  SELF_CHECK (make_ranges_from_mask (0xF0F0, 15) == "[4-7 12-14 *15]");

  /* Test some more complicated patterns.  */
  SELF_CHECK (make_ranges_from_mask (0x3DB3) == "[0-1 4-5 7-8 10-13]");
  SELF_CHECK (make_ranges_from_mask (0x3DB3, 11)
	      == "[0-1 4-5 7-8 10 *11 12-13]");
  SELF_CHECK (make_ranges_from_mask (0x3DB3, 12)
	      == "[0-1 4-5 7-8 10-11 *12 13]");
  SELF_CHECK (make_ranges_from_mask (0x3DB3, 7) == "[0-1 4-5 *7 8 10-13]");
  SELF_CHECK (make_ranges_from_mask (0x3DB3, 2) == "[0-1 4-5 7-8 10-13]");
  SELF_CHECK (make_ranges_from_mask (0x3DB3, 3) == "[0-1 4-5 7-8 10-13]");
  SELF_CHECK (make_ranges_from_mask (0x3DB3, 9) == "[0-1 4-5 7-8 10-13]");
  SELF_CHECK (make_ranges_from_mask (0x3DB3, 20) == "[0-1 4-5 7-8 10-13]");
}

}

#endif

void _initialize_cli_utils ();
void
_initialize_cli_utils ()
{
#if GDB_SELF_TEST
  selftests::register_test ("make_ranges_from_mask",
			    selftests::test_make_ranges_from_mask);
#endif
}
