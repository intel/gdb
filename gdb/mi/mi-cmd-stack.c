/* MI Command Set - stack commands.
   Copyright (C) 2000-2024 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions (a Red Hat company).

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

#include "event-top.h"
#include "target.h"
#include "frame.h"
#include "value.h"
#include "mi-cmds.h"
#include "ui-out.h"
#include "symtab.h"
#include "block.h"
#include "stack.h"
#include "dictionary.h"
#include "language.h"
#include "valprint.h"
#include "utils.h"
#include "mi-getopt.h"
#include "extension.h"
#include <ctype.h>
#include "mi-parse.h"
#include <optional>
#include "gdbsupport/gdb-safe-ctype.h"
#include "inferior.h"
#include "observable.h"
#include "gdbarch.h"
#include "gdbcore.h"
#include "arch-utils.h"
#include <unordered_set>

enum what_to_list { locals, arguments, all };

static void list_args_or_locals (const frame_print_options &fp_opts,
				 enum what_to_list what,
				 enum print_values values,
				 const frame_info_ptr &fi,
				 int skip_unavailable);

/* True if we want to allow Python-based frame filters.  */
static int frame_filters = 0;

void
mi_cmd_enable_frame_filters (const char *command, const char *const *argv,
			     int argc)
{
  if (argc != 0)
    error (_("-enable-frame-filters: no arguments allowed"));
  frame_filters = 1;
}

/* Like apply_ext_lang_frame_filter, but take a print_values */

static enum ext_lang_bt_status
mi_apply_ext_lang_frame_filter (const frame_info_ptr &frame,
				frame_filter_flags flags,
				enum print_values print_values,
				struct ui_out *out,
				int frame_low, int frame_high)
{
  /* ext_lang_frame_args's MI options are compatible with MI print
     values.  */
  return apply_ext_lang_frame_filter (frame, flags,
				      (enum ext_lang_frame_args) print_values,
				      out,
				      frame_low, frame_high);
}

/* Print a list of the stack frames.  Args can be none, in which case
   we want to print the whole backtrace, or a pair of numbers
   specifying the frame numbers at which to start and stop the
   display.  If the two numbers are equal, a single frame will be
   displayed.  */

void
mi_cmd_stack_list_frames (const char *command, const char *const *argv,
			  int argc)
{
  int frame_low;
  int frame_high;
  int i;
  frame_info_ptr fi;
  enum ext_lang_bt_status result = EXT_LANG_BT_ERROR;
  int raw_arg = 0;
  int oind = 0;
  enum opt
    {
      NO_FRAME_FILTERS
    };
  static const struct mi_opt opts[] =
    {
      {"-no-frame-filters", NO_FRAME_FILTERS, 0},
      { 0, 0, 0 }
    };

  /* Parse arguments.  In this instance we are just looking for
     --no-frame-filters.  */
  while (1)
    {
      const char *oarg;
      int opt = mi_getopt ("-stack-list-frames", argc, argv,
			   opts, &oind, &oarg);
      if (opt < 0)
	break;
      switch ((enum opt) opt)
	{
	case NO_FRAME_FILTERS:
	  raw_arg = oind;
	  break;
	}
    }

  /* After the last option is parsed, there should either be low -
     high range, or no further arguments.  */
  if ((argc - oind != 0) && (argc - oind != 2))
    error (_("-stack-list-frames: Usage: [--no-frame-filters] [FRAME_LOW FRAME_HIGH]"));

  /* If there is a range, set it.  */
  if (argc - oind == 2)
    {
      frame_low = atoi (argv[0 + oind]);
      frame_high = atoi (argv[1 + oind]);
    }
  else
    {
      /* Called with no arguments, it means we want the whole
	 backtrace.  */
      frame_low = -1;
      frame_high = -1;
    }

  /* Let's position fi on the frame at which to start the
     display. Could be the innermost frame if the whole stack needs
     displaying, or if frame_low is 0.  */
  for (i = 0, fi = get_current_frame ();
       fi && i < frame_low;
       i++, fi = get_prev_frame (fi));

  if (fi == NULL)
    error (_("-stack-list-frames: Not enough frames in stack."));

  ui_out_emit_list list_emitter (current_uiout, "stack");

  if (! raw_arg && frame_filters)
    {
      frame_filter_flags flags = PRINT_LEVEL | PRINT_FRAME_INFO;
      int py_frame_low = frame_low;

      /* We cannot pass -1 to frame_low, as that would signify a
      relative backtrace from the tail of the stack.  So, in the case
      of frame_low == -1, assign and increment it.  */
      if (py_frame_low == -1)
	py_frame_low++;

      result = apply_ext_lang_frame_filter (get_current_frame (), flags,
					    NO_VALUES,  current_uiout,
					    py_frame_low, frame_high);
    }

  /* Run the inbuilt backtrace if there are no filters registered, or
     if "--no-frame-filters" has been specified from the command.  */
  if (! frame_filters || raw_arg  || result == EXT_LANG_BT_NO_FILTERS)
    {
      /* Now let's print the frames up to frame_high, or until there are
	 frames in the stack.  */
      for (;
	   fi && (i <= frame_high || frame_high == -1);
	   i++, fi = get_prev_frame (fi))
	{
	  QUIT;
	  if (skip_trampoline_functions)
	    {
	      for (int j = 0; (SAFE_TRAMPOLINE_CHAIN (j, fi)
			       && in_trampoline_frame (fi)); ++j)
		fi = get_prev_frame (fi);
	    }

	  /* Print the location and the address always, even for level 0.
	     If args is 0, don't print the arguments.  */
	  print_frame_info (user_frame_print_options,
			    fi, 1, LOC_AND_ADDRESS, 0 /* args */, 0);
	}
    }
}

void
mi_cmd_stack_info_depth (const char *command, const char *const *argv,
			 int argc)
{
  int frame_high;
  int i;
  frame_info_ptr fi;

  if (argc > 1)
    error (_("-stack-info-depth: Usage: [MAX_DEPTH]"));

  if (argc == 1)
    frame_high = atoi (argv[0]);
  else
    /* Called with no arguments, it means we want the real depth of
       the stack.  */
    frame_high = -1;

  for (i = 0, fi = get_current_frame ();
       fi && (i < frame_high || frame_high == -1);
       i++, fi = get_prev_frame (fi))
    QUIT;

  current_uiout->field_signed ("depth", i);
}

/* Print a list of the locals for the current frame.  With argument of
   0, print only the names, with argument of 1 print also the
   values.  */

void
mi_cmd_stack_list_locals (const char *command, const char *const *argv,
			  int argc)
{
  frame_info_ptr frame;
  int raw_arg = 0;
  enum ext_lang_bt_status result = EXT_LANG_BT_ERROR;
  enum print_values print_value;
  int oind = 0;
  int skip_unavailable = 0;

  if (argc > 1)
    {
      enum opt
      {
	NO_FRAME_FILTERS,
	SKIP_UNAVAILABLE,
      };
      static const struct mi_opt opts[] =
	{
	  {"-no-frame-filters", NO_FRAME_FILTERS, 0},
	  {"-skip-unavailable", SKIP_UNAVAILABLE, 0},
	  { 0, 0, 0 }
	};

      while (1)
	{
	  const char *oarg;
	  /* Don't parse 'print-values' as an option.  */
	  int opt = mi_getopt ("-stack-list-locals", argc - 1, argv,
			       opts, &oind, &oarg);

	  if (opt < 0)
	    break;
	  switch ((enum opt) opt)
	    {
	    case NO_FRAME_FILTERS:
	      raw_arg = oind;
	      break;
	    case SKIP_UNAVAILABLE:
	      skip_unavailable = 1;
	      break;
	    }
	}
    }

  /* After the last option is parsed, there should be only
     'print-values'.  */
  if (argc - oind != 1)
    error (_("-stack-list-locals: Usage: [--no-frame-filters] "
	     "[--skip-unavailable] PRINT_VALUES"));

  frame = get_selected_frame (NULL);
  print_value = mi_parse_print_values (argv[oind]);

   if (! raw_arg && frame_filters)
     {
       frame_filter_flags flags = PRINT_LEVEL | PRINT_LOCALS;

       result = mi_apply_ext_lang_frame_filter (frame, flags, print_value,
						current_uiout, 0, 0);
     }

   /* Run the inbuilt backtrace if there are no filters registered, or
      if "--no-frame-filters" has been specified from the command.  */
   if (! frame_filters || raw_arg  || result == EXT_LANG_BT_NO_FILTERS)
     {
       list_args_or_locals (user_frame_print_options,
			    locals, print_value, frame,
			    skip_unavailable);
     }
}

/* Print a list of the arguments for the current frame.  With argument
   of 0, print only the names, with argument of 1 print also the
   values.  */

void
mi_cmd_stack_list_args (const char *command, const char *const *argv, int argc)
{
  int frame_low;
  int frame_high;
  int i;
  frame_info_ptr fi;
  enum print_values print_values;
  struct ui_out *uiout = current_uiout;
  int raw_arg = 0;
  int oind = 0;
  int skip_unavailable = 0;
  enum ext_lang_bt_status result = EXT_LANG_BT_ERROR;
  enum opt
  {
    NO_FRAME_FILTERS,
    SKIP_UNAVAILABLE,
  };
  static const struct mi_opt opts[] =
    {
      {"-no-frame-filters", NO_FRAME_FILTERS, 0},
      {"-skip-unavailable", SKIP_UNAVAILABLE, 0},
      { 0, 0, 0 }
    };

  while (1)
    {
      const char *oarg;
      int opt = mi_getopt_allow_unknown ("-stack-list-args", argc, argv,
					 opts, &oind, &oarg);

      if (opt < 0)
	break;
      switch ((enum opt) opt)
	{
	case NO_FRAME_FILTERS:
	  raw_arg = oind;
	  break;
	case SKIP_UNAVAILABLE:
	  skip_unavailable = 1;
	  break;
	}
    }

  if (argc - oind != 1 && argc - oind != 3)
    error (_("-stack-list-arguments: Usage: "	\
	     "[--no-frame-filters] [--skip-unavailable] "
	     "PRINT_VALUES [FRAME_LOW FRAME_HIGH]"));

  if (argc - oind == 3)
    {
      frame_low = atoi (argv[1 + oind]);
      frame_high = atoi (argv[2 + oind]);
    }
  else
    {
      /* Called with no arguments, it means we want args for the whole
	 backtrace.  */
      frame_low = -1;
      frame_high = -1;
    }

  print_values = mi_parse_print_values (argv[oind]);

  /* Let's position fi on the frame at which to start the
     display. Could be the innermost frame if the whole stack needs
     displaying, or if frame_low is 0.  */
  for (i = 0, fi = get_current_frame ();
       fi && i < frame_low;
       i++, fi = get_prev_frame (fi));

  if (fi == NULL)
    error (_("-stack-list-arguments: Not enough frames in stack."));

  ui_out_emit_list list_emitter (uiout, "stack-args");

  if (! raw_arg && frame_filters)
    {
      frame_filter_flags flags = PRINT_LEVEL | PRINT_ARGS;
      if (user_frame_print_options.print_raw_frame_arguments)
	flags |= PRINT_RAW_FRAME_ARGUMENTS;
      int py_frame_low = frame_low;

      /* We cannot pass -1 to frame_low, as that would signify a
      relative backtrace from the tail of the stack.  So, in the case
      of frame_low == -1, assign and increment it.  */
      if (py_frame_low == -1)
	py_frame_low++;

      result = mi_apply_ext_lang_frame_filter (get_current_frame (), flags,
					       print_values, current_uiout,
					       py_frame_low, frame_high);
    }

     /* Run the inbuilt backtrace if there are no filters registered, or
      if "--no-frame-filters" has been specified from the command.  */
   if (! frame_filters || raw_arg  || result == EXT_LANG_BT_NO_FILTERS)
     {
      /* Now let's print the frames up to frame_high, or until there are
	 frames in the stack.  */
      for (;
	   fi && (i <= frame_high || frame_high == -1);
	   i++, fi = get_prev_frame (fi))
	{
	  QUIT;
	  if (skip_trampoline_functions)
	    {
	      for (int j = 0; (SAFE_TRAMPOLINE_CHAIN (j, fi)
			       && in_trampoline_frame (fi)); ++j)
		fi = get_prev_frame (fi);
	    }

	  ui_out_emit_tuple tuple_emitter (uiout, "frame");
	  uiout->field_signed ("level", i);
	  list_args_or_locals (user_frame_print_options,
			       arguments, print_values, fi, skip_unavailable);
	}
    }
}

/* Print a list of the local variables (including arguments) for the 
   current frame.  ARGC must be 1 and ARGV[0] specify if only the names,
   or both names and values of the variables must be printed.  See 
   parse_print_value for possible values.  */

void
mi_cmd_stack_list_variables (const char *command, const char *const *argv,
			     int argc)
{
  frame_info_ptr frame;
  int raw_arg = 0;
  enum ext_lang_bt_status result = EXT_LANG_BT_ERROR;
  enum print_values print_value;
  int oind = 0;
  int skip_unavailable = 0;

  if (argc > 1)
    {
      enum opt
      {
	NO_FRAME_FILTERS,
	SKIP_UNAVAILABLE,
      };
      static const struct mi_opt opts[] =
	{
	  {"-no-frame-filters", NO_FRAME_FILTERS, 0},
	  {"-skip-unavailable", SKIP_UNAVAILABLE, 0},
	  { 0, 0, 0 }
	};

      while (1)
	{
	  const char *oarg;
	  /* Don't parse 'print-values' as an option.  */
	  int opt = mi_getopt ("-stack-list-variables", argc - 1,
			       argv, opts, &oind, &oarg);
	  if (opt < 0)
	    break;
	  switch ((enum opt) opt)
	    {
	    case NO_FRAME_FILTERS:
	      raw_arg = oind;
	      break;
	    case SKIP_UNAVAILABLE:
	      skip_unavailable = 1;
	      break;
	    }
	}
    }

  /* After the last option is parsed, there should be only
     'print-values'.  */
  if (argc - oind != 1)
    error (_("-stack-list-variables: Usage: [--no-frame-filters] " \
	     "[--skip-unavailable] PRINT_VALUES"));

   frame = get_selected_frame (NULL);
   print_value = mi_parse_print_values (argv[oind]);

   if (! raw_arg && frame_filters)
     {
       frame_filter_flags flags = PRINT_LEVEL | PRINT_ARGS | PRINT_LOCALS;
       if (user_frame_print_options.print_raw_frame_arguments)
	 flags |= PRINT_RAW_FRAME_ARGUMENTS;

       result = mi_apply_ext_lang_frame_filter (frame, flags,
						print_value,
						current_uiout, 0, 0);
     }

   /* Run the inbuilt backtrace if there are no filters registered, or
      if "--no-frame-filters" has been specified from the command.  */
   if (! frame_filters || raw_arg  || result == EXT_LANG_BT_NO_FILTERS)
     {
       list_args_or_locals (user_frame_print_options,
			    all, print_value, frame,
			    skip_unavailable);
     }
}

/* Print single local or argument.  ARG must be already read in.  For
   WHAT and VALUES see list_args_or_locals.

   Errors are printed as if they would be the parameter value.  Use
   zeroed ARG iff it should not be printed according to VALUES.  If
   SKIP_UNAVAILABLE is true, only print ARG if it is available.  */

static void
list_arg_or_local (const struct frame_arg *arg, enum what_to_list what,
		   enum print_values values, int skip_unavailable,
		   const frame_print_options &fp_opts,
		   std::unordered_set<std::string> &collected_vars)
{
  struct ui_out *uiout = current_uiout;

  gdb_assert (!arg->val || !arg->error);
  gdb_assert ((values == PRINT_NO_VALUES && arg->val == NULL
	       && arg->error == NULL)
	      || values == PRINT_SIMPLE_VALUES
	      || (values == PRINT_ALL_VALUES
		  && (arg->val != NULL || arg->error != NULL)));
  gdb_assert (arg->entry_kind == print_entry_values_no
	      || (arg->entry_kind == print_entry_values_only
		  && (arg->val || arg->error)));

  if (skip_unavailable && arg->val != NULL
      && (arg->val->entirely_unavailable ()
	  /* A scalar object that does not have all bits available is
	     also considered unavailable, because all bits contribute
	     to its representation.  */
	  || (val_print_scalar_type_p (arg->val->type ())
	      && !arg->val->bytes_available (arg->val->embedded_offset (),
					     arg->val->type ()->length ()))))
    return;

  std::optional<ui_out_emit_tuple> tuple_emitter;
  if (values != PRINT_NO_VALUES || what == all)
    tuple_emitter.emplace (uiout, nullptr);

  string_file stb;
  bool already_collected
    = collected_vars.find (arg->sym->print_name ()) != collected_vars.end ();

  /* In case of rust language it is possible to declare variable with
     same name multiple times and only latest declaration of variable
     is accessible.  So print only the first instance and there is no
     need of printing duplicates.  */
  if (current_language->la_language == language_rust && already_collected)
    return;

  stb.puts (arg->sym->print_name ());
  if (arg->entry_kind == print_entry_values_only)
    stb.puts ("@entry");
  uiout->field_stream ("name", stb);

  if (what == all && arg->sym->is_argument ())
    uiout->field_signed ("arg", 1);

  /* Only for C/C++/Fortran languages, in case of variables shadowing
     print shadowed field after the superblock variable.  Iteration of
     block starts from inner block so collected_vars variable keeps
     track of the variables in the innerblock.  */
  if ((current_language->la_language == language_c
       || current_language->la_language == language_cplus
       || current_language->la_language == language_fortran)
       && !(values == PRINT_NO_VALUES && what == locals)
       && already_collected)
    {
      if (arg->sym->m_line > 0)
	uiout->field_unsigned ("shadowed_loc", arg->sym->m_line);
      else
	uiout->field_string ("shadowed_loc", "NA");
    }
  else
    collected_vars.insert (arg->sym->print_name ());

  if (values == PRINT_SIMPLE_VALUES)
    {
      check_typedef (arg->sym->type ());
      type_print (arg->sym->type (), "", &stb, -1);
      uiout->field_stream ("type", stb);
    }

  if (arg->val || arg->error)
    {
      if (arg->error)
	stb.printf (_("<error reading variable: %s>"), arg->error.get ());
      else
	{
	  try
	    {
	      struct value_print_options opts;

	      get_no_prettyformat_print_options (&opts);
	      opts.deref_ref = true;
	      if (arg->sym->is_argument ())
		opts.raw = fp_opts.print_raw_frame_arguments;
	      common_val_print (arg->val, &stb, 0, &opts,
				language_def (arg->sym->language ()));
	    }
	  catch (const gdb_exception_error &except)
	    {
	      stb.printf (_("<error reading variable: %s>"),
			  except.what ());
	    }
	}
      uiout->field_stream ("value", stb);
    }
}

/* Print a list of the objects for the frame FI in a certain form,
   which is determined by VALUES.  The objects can be locals,
   arguments or both, which is determined by WHAT.  If SKIP_UNAVAILABLE
   is true, only print the arguments or local variables whose values
   are available.  */

static void
list_args_or_locals (const frame_print_options &fp_opts,
		     enum what_to_list what, enum print_values values,
		     const frame_info_ptr &fi, int skip_unavailable)
{
  const struct block *block;
  const char *name_of_result;
  struct ui_out *uiout = current_uiout;
  std::unordered_set<std::string> collected_vars;

  block = get_frame_block (fi, 0);

  switch (what)
    {
    case locals:
      name_of_result = "locals";
      break;
    case arguments:
      name_of_result = "args";
      break;
    case all:
      name_of_result = "variables";
      break;
    default:
      internal_error ("unexpected what_to_list: %d", (int) what);
    }

  ui_out_emit_list list_emitter (uiout, name_of_result);

  while (block != 0)
    {
      for (struct symbol *sym : block_iterator_range (block))
	{
	  int print_me = 0;

	  switch (sym->aclass ())
	    {
	    default:
	    case LOC_UNDEF:	/* catches errors        */
	    case LOC_CONST:	/* constant              */
	    case LOC_TYPEDEF:	/* local typedef         */
	    case LOC_LABEL:	/* local label           */
	    case LOC_BLOCK:	/* local function        */
	    case LOC_CONST_BYTES:	/* loc. byte seq.        */
	    case LOC_UNRESOLVED:	/* unresolved static     */
	    case LOC_OPTIMIZED_OUT:	/* optimized out         */
	      print_me = 0;
	      break;

	    case LOC_ARG:	/* argument              */
	    case LOC_REF_ARG:	/* reference arg         */
	    case LOC_REGPARM_ADDR:	/* indirect register arg */
	    case LOC_LOCAL:	/* stack local           */
	    case LOC_STATIC:	/* static                */
	    case LOC_REGISTER:	/* register              */
	    case LOC_COMPUTED:	/* computed location     */
	      if (what == all)
		print_me = 1;
	      else if (what == locals)
		print_me = !sym->is_argument ();
	      else
		print_me = sym->is_argument ();
	      break;
	    }
	  if (print_me)
	    {
	      struct symbol *sym2;
	      struct frame_arg arg, entryarg;

	      if (sym->is_argument ())
		sym2 = (lookup_symbol_search_name
			(sym->search_name (),
			 block, SEARCH_VAR_DOMAIN).symbol);
	      else
		sym2 = sym;
	      gdb_assert (sym2 != NULL);

	      arg.sym = sym2;
	      arg.entry_kind = print_entry_values_no;
	      entryarg.sym = sym2;
	      entryarg.entry_kind = print_entry_values_no;

	      switch (values)
		{
		case PRINT_SIMPLE_VALUES:
		  if (!mi_simple_type_p (sym2->type ()))
		    break;
		  [[fallthrough]];

		case PRINT_ALL_VALUES:
		  if (sym->is_argument ())
		    read_frame_arg (fp_opts, sym2, fi, &arg, &entryarg);
		  else
		    read_frame_local (sym2, fi, &arg);
		  break;
		}

	      if (arg.entry_kind != print_entry_values_only)
		list_arg_or_local (&arg, what, values, skip_unavailable,
				   fp_opts, collected_vars);
	      if (entryarg.entry_kind != print_entry_values_no)
		list_arg_or_local (&entryarg, what, values, skip_unavailable,
				   fp_opts, collected_vars);
	    }
	}

      if (block->function ())
	break;
      else
	block = block->superblock ();
    }
}

/* Read a frame specification from FRAME_EXP and return the selected frame.
   Call error() if the specification is in any way invalid (so this
   function never returns NULL).

   The frame specification is usually an integer level number, however if
   the number does not match a valid frame level then it will be treated as
   a frame address.  The frame address will then be used to find a matching
   frame in the stack.  If no matching frame is found then a new frame will
   be created.

   The use of FRAME_EXP as an address is undocumented in the GDB user
   manual, this feature is supported here purely for backward
   compatibility.  */

static frame_info_ptr
parse_frame_specification (const char *frame_exp)
{
  gdb_assert (frame_exp != NULL);

  /* NOTE: Parse and evaluate expression, but do not use
     functions such as parse_and_eval_long or
     parse_and_eval_address to also extract the value.
     Instead value_as_long and value_as_address are used.
     This avoids problems with expressions that contain
     side-effects.  */
  struct value *arg = parse_and_eval (frame_exp);

  /* Assume ARG is an integer, and try using that to select a frame.  */
  frame_info_ptr fid;
  int level = value_as_long (arg);

  fid = find_relative_frame (get_current_frame (), &level);
  if (level == 0)
    /* find_relative_frame was successful.  */
    return fid;

  /* Convert the value into a corresponding address.  */
  CORE_ADDR addr = value_as_address (arg);

  /* Assume that ADDR is an address, use that to identify a frame with a
     matching ID.  */
  struct frame_id id = frame_id_build_wild (addr);

  /* If (s)he specifies the frame with an address, he deserves
     what (s)he gets.  Still, give the highest one that matches.
     (NOTE: cagney/2004-10-29: Why highest, or outer-most, I don't
     know).  */
  for (fid = get_current_frame ();
       fid != NULL;
       fid = get_prev_frame (fid))
    {
      if (id == get_frame_id (fid))
	{
	  frame_info_ptr prev_frame;

	  while (1)
	    {
	      prev_frame = get_prev_frame (fid);
	      if (!prev_frame
		  || id != get_frame_id (prev_frame))
		break;
	      fid = prev_frame;
	    }
	  return fid;
	}
    }

  /* We couldn't identify the frame as an existing frame, but
     perhaps we can create one with a single argument.  */
  return create_new_frame (addr, 0);
}

/* Implement the -stack-select-frame MI command.  */

void
mi_cmd_stack_select_frame (const char *command, const char *const *argv,
			   int argc)
{
  if (argc == 0 || argc > 1)
    error (_("-stack-select-frame: Usage: FRAME_SPEC"));
  select_frame (parse_frame_specification (argv[0]));
}

void
mi_cmd_stack_info_frame (const char *command, const char *const *argv,
			 int argc)
{
  if (argc > 0)
    error (_("-stack-info-frame: No arguments allowed"));

  print_frame_info (user_frame_print_options,
		    get_selected_frame (NULL), 1, LOC_AND_ADDRESS, 0, 1);
}

/* Parse arguments of -shadow-stack-list-frames command and set FRAME_LOW
   and FRAME_HIGH accordingly.  Throw an error in case the arguments are
   invalid.  */
static void
mi_cmd_shadow_stack_list_frames_parse_args (const char *const *argv,
					    int argc, int &frame_low,
					    int &frame_high)
{
  /* There should either be low - high range, or no arguments.  */
  if ((argc != 0) && (argc != 2))
    error (_("-shadow-stack-list-frames: Usage: [FRAME_LOW FRAME_HIGH]"));

  /* If there is a range, set it.  */
  if (argc == 2)
    {
      frame_low = atoi (argv[0]);
      frame_high = atoi (argv[1]);
      std::string err_str;
      if (frame_low < 0)
	{
	  err_str = "``" + std::to_string (frame_low) + "''";
	  if (frame_high < 0)
	    err_str += " and ``" + std::to_string (frame_high) + "''";
	}
      else if (frame_high < 0)
	err_str = "``" + std::to_string (frame_high) + "''";

      if (!err_str.empty ())
	{
	  err_str = "-shadow-stack-list-frames: Invalid option " + err_str;
	  error (_("%s."), err_str.c_str ());
	}
    }
  else
    {
      /* No arguments, print the whole shadow stack backtrace.  */
      frame_low = -1;
      frame_high = -1;
    }
}

/* Print a list of the shadow stack frames.  Args can be none, in which
   case we want to print the whole shadow stack backtrace, or a pair of
   numbers specifying the frame numbers at which to start and stop the
   display.  If the two numbers are equal, a single frame will be
   displayed.  */

void
mi_cmd_shadow_stack_list_frames (const char *command,
				 const char *const *argv,
				 int argc)
{
  int frame_low;
  int frame_high;

  mi_cmd_shadow_stack_list_frames_parse_args (argv, argc, frame_low,
					      frame_high);

  if (!target_has_stack ())
    error (_("-shadow-stack-list-frames: No shadow stack."));

  gdbarch *gdbarch = get_current_arch ();
  if (!gdbarch_address_in_shadow_stack_memory_range_p (gdbarch))
    error (_("-shadow-stack-list-frames: Printing of shadow stack \
	      backtrace is not supported for the current target."));

  std::optional<CORE_ADDR> start_ssp
    = gdbarch_get_shadow_stack_pointer (gdbarch);
  if (!start_ssp.has_value ())
    error (_("-shadow-stack-list-frames: Shadow stack is not enabled for \
	      the current target."));

  ui_out_emit_list list_emitter (current_uiout, "shadow-stack");

  /* Check if START_SSP points to a shadow stack memory range and use
     the returned range to determine when to stop unwinding.
     Note that a shadow stack memory range can change, due to shadow stack
     switches for instance on x86 for an inter-privilege far call or when
     calling an interrupt/exception handler at a higher privilege level.
     Shadow stack for userspace is supported for amd64 linux starting with
     Linux kernel v6.6.  However, shadow stack switches are not supported
     due to missing kernel space support.  We therefore implement this
     command without support for shadow stack switches for now.  */
  std::pair<CORE_ADDR, CORE_ADDR> range;
  if (!gdbarch_address_in_shadow_stack_memory_range (gdbarch, *start_ssp,
						     &range))
    {
      /* If the current shadow stack pointer does not point to shadow
	 stack memory, the shadow stack is empty.  */
      return;
    }

  std::optional<shadow_stack_frame_info> curr;
  CORE_ADDR new_value;
  const int addr_size_byte = gdbarch_addr_bit (gdbarch) / 8;
  const bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  if (!safe_read_memory_unsigned_integer (*start_ssp, addr_size_byte,
					  byte_order, &new_value))
    error (_("-shadow-stack-list-frames: Cannot read shadow stack memory."));

  curr = {*start_ssp, new_value, 0, ssp_unwind_stop_reason::no_error};

  /* Let's position curr on the shadow stack frame at which to start the
     display.  This could be the innermost frame if the whole shadow stack
     needs displaying, or if frame_low is 0.  */
  int frame_num = 0;
  for (; curr.has_value () && frame_num < frame_low; frame_num++)
    curr = curr->unwind_prev_shadow_stack_frame_info (gdbarch, range);

  if (!curr.has_value ())
    error (_("-shadow-stack-list-frames: Not enough frames on the shadow \
	      stack."));

  shadow_stack_print_options print_options;
  print_options.print_frame_info
    = user_frame_print_options.print_frame_info;

  /* Now let's print the shadow stack frames up to frame_high, or until
     the bottom of the shadow stack.  */
  for (; curr.has_value () && (frame_num <= frame_high || frame_high == -1);
       frame_num++)
    {
      QUIT;
      print_shadow_stack_frame_info (gdbarch, print_options, *curr,
				     LOCATION);
      curr = curr->unwind_prev_shadow_stack_frame_info (gdbarch, range);
    }
}
