/* Definitions for targets which report shared library events.

   Copyright (C) 2007-2023 Free Software Foundation, Inc.

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
#include "objfiles.h"
#include "solist.h"
#include "symtab.h"
#include "symfile.h"
#include "target.h"
#include "solib-target.h"
#include "gdbsupport/filestuff.h"
#include "gdb_bfd.h"
#include <vector>
#include "inferior.h"

/* The location of a loaded library.  */

enum lm_location_t
{
  lm_on_disk,
  lm_in_memory
};

/* Private data for each loaded library.  */
struct lm_info_target : public lm_info_base
{
  /* The library's location.  */
  lm_location_t location;

  /* The library's name.  The name is normally kept in the struct
     so_list; it is only here during XML parsing.

     This is only valid if location == lm_on_disk.  */
  std::string name;

  /* The library's begin and end memory addresses.

     This is only valid if location == lm_in_memory.  */
  CORE_ADDR begin = 0ull, end = 0ull;

  /* A flag saying whether library load and unload need to be acknowledged
     to the target after processing the library and placing/removing
     breakpoints.  */
  bool need_ack = false;

  /* The target can either specify segment bases or section bases, not
     both.  */

  /* The base addresses for each independently relocatable segment of
     this shared library.  */
  std::vector<CORE_ADDR> segment_bases;

  /* The base addresses for each independently allocatable,
     relocatable section of this shared library.  */
  std::vector<CORE_ADDR> section_bases;

  /* The cached offsets for each section of this shared library,
     determined from SEGMENT_BASES, or SECTION_BASES.  */
  section_offsets offsets;
};

typedef std::vector<std::unique_ptr<lm_info_target>> lm_info_vector;

#if !defined(HAVE_LIBEXPAT)

static lm_info_vector
solib_target_parse_libraries (const char *library)
{
  static int have_warned;

  if (!have_warned)
    {
      have_warned = 1;
      warning (_("Can not parse XML library list; XML support was disabled "
		 "at compile time"));
    }

  return lm_info_vector ();
}

#else /* HAVE_LIBEXPAT */

#include "xml-support.h"

/* Handle the start of a <segment> element.  */

static void
library_list_start_segment (struct gdb_xml_parser *parser,
			    const struct gdb_xml_element *element,
			    void *user_data,
			    std::vector<gdb_xml_value> &attributes)
{
  lm_info_vector *list = (lm_info_vector *) user_data;
  lm_info_target *last = list->back ().get ();
  ULONGEST *address_p
    = (ULONGEST *) xml_find_attribute (attributes, "address")->value.get ();
  CORE_ADDR address = (CORE_ADDR) *address_p;

  if (!last->section_bases.empty ())
    gdb_xml_error (parser,
		   _("Library list with both segments and sections"));

  last->segment_bases.push_back (address);
}

static void
library_list_start_section (struct gdb_xml_parser *parser,
			    const struct gdb_xml_element *element,
			    void *user_data,
			    std::vector<gdb_xml_value> &attributes)
{
  lm_info_vector *list = (lm_info_vector *) user_data;
  lm_info_target *last = list->back ().get ();
  ULONGEST *address_p
    = (ULONGEST *) xml_find_attribute (attributes, "address")->value.get ();
  CORE_ADDR address = (CORE_ADDR) *address_p;

  if (!last->segment_bases.empty ())
    gdb_xml_error (parser,
		   _("Library list with both segments and sections"));

  last->section_bases.push_back (address);
}

/* Handle the 'ack' attribute of <library> and <in-memory-library>.  */

static void
library_ack (lm_info_target &item, std::vector<gdb_xml_value> &attributes)
{
  gdb_xml_value *ack = xml_find_attribute (attributes, "ack");
  if (ack != nullptr)
    {
      const char *value = (const char *) ack->value.get ();
      if (strcmp (value, "yes") == 0)
	item.need_ack = true;
      else if (strcmp (value, "no") == 0)
	item.need_ack = false;
      else
	warning (_("bad attribute value for library:ack"));
    }
}

/* Handle the start of a <library> element.  */

static void
library_list_start_library (struct gdb_xml_parser *parser,
			    const struct gdb_xml_element *element,
			    void *user_data,
			    std::vector<gdb_xml_value> &attributes)
{
  lm_info_vector *list = (lm_info_vector *) user_data;
  lm_info_target *item = new lm_info_target;
  item->location = lm_on_disk;
  item->name
    = (const char *) xml_find_attribute (attributes, "name")->value.get ();

  library_ack (*item, attributes);

  list->emplace_back (item);
}

/* Handle the start of a <in-memory-library> element.  */

static void
in_memory_library_list_start_library (struct gdb_xml_parser *parser,
				      const struct gdb_xml_element *element,
				      void *user_data,
				      std::vector<gdb_xml_value> &attributes)
{
  lm_info_vector *list = (lm_info_vector *) user_data;
  lm_info_target *item = new lm_info_target;
  item->location = lm_in_memory;
  item->begin = (CORE_ADDR) *(ULONGEST *)
    xml_find_attribute (attributes, "begin")->value.get ();
  item->end = (CORE_ADDR) *(ULONGEST *)
    xml_find_attribute (attributes, "end")->value.get ();

  library_ack (*item, attributes);

  list->emplace_back (item);
}

static void
library_list_end_library (struct gdb_xml_parser *parser,
			  const struct gdb_xml_element *element,
			  void *user_data, const char *body_text)
{
  lm_info_vector *list = (lm_info_vector *) user_data;
  lm_info_target *lm_info = list->back ().get ();

  if (lm_info->segment_bases.empty () && lm_info->section_bases.empty ())
    gdb_xml_error (parser, _("No segment or section bases defined"));
}


/* Handle the start of a <library-list> element.  */

static void
library_list_start_list (struct gdb_xml_parser *parser,
			 const struct gdb_xml_element *element,
			 void *user_data,
			 std::vector<gdb_xml_value> &attributes)
{
  struct gdb_xml_value *version = xml_find_attribute (attributes, "version");

  /* #FIXED attribute may be omitted, Expat returns NULL in such case.  */
  if (version != NULL)
    {
      const char *string = (const char *) version->value.get ();

      if ((strcmp (string, "1.0") != 0) && (strcmp (string, "1.1") != 0)
	  && (strcmp (string, "1.2") != 0))
	gdb_xml_error (parser,
		       _("Library list has unsupported version \"%s\""),
		       string);
    }
}

/* The allowed elements and attributes for an XML library list.
   The root element is a <library-list>.  */

static const struct gdb_xml_attribute segment_attributes[] = {
  { "address", GDB_XML_AF_NONE, gdb_xml_parse_attr_ulongest, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_attribute section_attributes[] = {
  { "address", GDB_XML_AF_NONE, gdb_xml_parse_attr_ulongest, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_element library_children[] = {
  { "segment", segment_attributes, NULL,
    GDB_XML_EF_REPEATABLE | GDB_XML_EF_OPTIONAL,
    library_list_start_segment, NULL },
  { "section", section_attributes, NULL,
    GDB_XML_EF_REPEATABLE | GDB_XML_EF_OPTIONAL,
    library_list_start_section, NULL },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

static const struct gdb_xml_attribute library_attributes[] = {
  { "name", GDB_XML_AF_NONE, NULL, NULL },
  { "ack", GDB_XML_AF_OPTIONAL, NULL, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_attribute in_memory_library_attributes[] = {
  { "begin", GDB_XML_AF_NONE, gdb_xml_parse_attr_ulongest, NULL },
  { "end", GDB_XML_AF_NONE, gdb_xml_parse_attr_ulongest, NULL },
  { "ack", GDB_XML_AF_OPTIONAL, NULL, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_element library_list_children[] = {
  { "library", library_attributes, library_children,
    GDB_XML_EF_REPEATABLE | GDB_XML_EF_OPTIONAL,
    library_list_start_library, library_list_end_library },
  { "in-memory-library", in_memory_library_attributes, library_children,
    GDB_XML_EF_REPEATABLE | GDB_XML_EF_OPTIONAL,
    in_memory_library_list_start_library, library_list_end_library },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

static const struct gdb_xml_attribute library_list_attributes[] = {
  { "version", GDB_XML_AF_OPTIONAL, NULL, NULL },
  { NULL, GDB_XML_AF_NONE, NULL, NULL }
};

static const struct gdb_xml_element library_list_elements[] = {
  { "library-list", library_list_attributes, library_list_children,
    GDB_XML_EF_NONE, library_list_start_list, NULL },
  { NULL, NULL, NULL, GDB_XML_EF_NONE, NULL, NULL }
};

static lm_info_vector
solib_target_parse_libraries (const char *library)
{
  lm_info_vector result;

  if (gdb_xml_parse_quick (_("target library list"), "library-list.dtd",
			   library_list_elements, library, &result) == 0)
    {
      /* Parsed successfully.  */
      return result;
    }

  result.clear ();
  return result;
}
#endif

static struct so_list *
solib_target_current_sos (void)
{
  struct so_list *new_solib, *start = NULL, *last = NULL;

  /* Fetch the list of shared libraries.  */
  gdb::optional<gdb::char_vector> library_document
    = target_read_stralloc (current_inferior ()->top_target (),
			    TARGET_OBJECT_LIBRARIES, NULL);
  if (!library_document)
    return NULL;

  /* Parse the list.  */
  lm_info_vector library_list
    = solib_target_parse_libraries (library_document->data ());

  if (library_list.empty ())
    return NULL;

  /* Build a struct so_list for each entry on the list.  */
  for (auto &&info : library_list)
    {
      new_solib = XCNEW (struct so_list);
      switch (info->location)
	{
	case lm_on_disk:
	  strncpy (new_solib->so_name, info->name.c_str (),
		   SO_NAME_MAX_PATH_SIZE - 1);
	  new_solib->so_name[SO_NAME_MAX_PATH_SIZE - 1] = '\0';
	  strncpy (new_solib->so_original_name, info->name.c_str (),
		   SO_NAME_MAX_PATH_SIZE - 1);
	  new_solib->so_original_name[SO_NAME_MAX_PATH_SIZE - 1] = '\0';

	  /* We no longer need this copy of the name.  */
	  info->name.clear ();
	  break;

	case lm_in_memory:
	  {
	    if (info->end <= info->begin)
	      error (_("bad in-memory-library location: begin=%s, end=%s"),
		     core_addr_to_string_nz (info->begin),
		     core_addr_to_string_nz (info->end));

	    /* Give it a name although this isn't really needed.  */
	    std::string orig_name
	      = std::string ("in-memory-")
	      + core_addr_to_string_nz (info->begin)
	      + "-"
	      + core_addr_to_string_nz (info->end);

	    strncpy (new_solib->so_original_name, orig_name.c_str (),
		     SO_NAME_MAX_PATH_SIZE - 1);
	    new_solib->so_original_name[SO_NAME_MAX_PATH_SIZE - 1] = '\0';

	    new_solib->begin = info->begin;
	    new_solib->end = info->end;
	  }
	  break;
	}

      new_solib->lm_info = info.release ();

      /* Add it to the list.  */
      if (!start)
	last = start = new_solib;
      else
	{
	  last->next = new_solib;
	  last = new_solib;
	}
    }

  return start;
}

static void
solib_target_solib_create_inferior_hook (int from_tty)
{
  /* Nothing needed.  */
}

static void
solib_target_clear_solib (void)
{
  /* Nothing needed.  */
}

static void
solib_target_free_so (struct so_list *so)
{
  lm_info_target *li = (lm_info_target *) so->lm_info;

  gdb_assert (li->name.empty ());

  delete li;
}

static void
solib_target_relocate_section_addresses (struct so_list *so,
					 struct target_section *sec)
{
  CORE_ADDR offset;
  lm_info_target *li = (lm_info_target *) so->lm_info;

  /* Build the offset table only once per object file.  We can not do
     it any earlier, since we need to open the file first.  */
  if (li->offsets.empty ())
    {
      int num_sections = gdb_bfd_count_sections (so->abfd);

      li->offsets.assign (num_sections, 0);

      if (!li->section_bases.empty ())
	{
	  int i;
	  asection *sect;
	  int num_alloc_sections = 0;

	  for (i = 0, sect = so->abfd->sections;
	       sect != NULL;
	       i++, sect = sect->next)
	    if ((bfd_section_flags (sect) & SEC_ALLOC))
	      num_alloc_sections++;

	  if (num_alloc_sections != li->section_bases.size ())
	    warning (_("\
Could not relocate shared library \"%s\": wrong number of ALLOC sections"),
		     so->so_name);
	  else
	    {
	      int bases_index = 0;
	      int found_range = 0;

	      so->addr_low = ~(CORE_ADDR) 0;
	      so->addr_high = 0;
	      for (i = 0, sect = so->abfd->sections;
		   sect != NULL;
		   i++, sect = sect->next)
		{
		  if (!(bfd_section_flags (sect) & SEC_ALLOC))
		    continue;
		  if (bfd_section_size (sect) > 0)
		    {
		      CORE_ADDR low, high;

		      low = li->section_bases[i];
		      high = low + bfd_section_size (sect) - 1;

		      if (low < so->addr_low)
			so->addr_low = low;
		      if (high > so->addr_high)
			so->addr_high = high;
		      gdb_assert (so->addr_low <= so->addr_high);
		      found_range = 1;
		    }
		  li->offsets[i] = li->section_bases[bases_index];
		  bases_index++;
		}
	      if (!found_range)
		so->addr_low = so->addr_high = 0;
	      gdb_assert (so->addr_low <= so->addr_high);
	    }
	}
      else if (!li->segment_bases.empty ())
	{
	  symfile_segment_data_up data
	    = get_symfile_segment_data (so->abfd);

	  if (data == NULL)
	    warning (_("\
Could not relocate shared library \"%s\": no segments"), so->so_name);
	  else
	    {
	      ULONGEST orig_delta;
	      int i;

	      if (!symfile_map_offsets_to_segments (so->abfd, data.get (),
						    li->offsets,
						    li->segment_bases.size (),
						    li->segment_bases.data ()))
		warning (_("\
Could not relocate shared library \"%s\": bad offsets"), so->so_name);

	      /* Find the range of addresses to report for this library in
		 "info sharedlibrary".  Report any consecutive segments
		 which were relocated as a single unit.  */
	      gdb_assert (li->segment_bases.size () > 0);
	      orig_delta = li->segment_bases[0] - data->segments[0].base;

	      for (i = 1; i < data->segments.size (); i++)
		{
		  /* If we have run out of offsets, assume all
		     remaining segments have the same offset.  */
		  if (i >= li->segment_bases.size ())
		    continue;

		  /* If this segment does not have the same offset, do
		     not include it in the library's range.  */
		  if (li->segment_bases[i] - data->segments[i].base
		      != orig_delta)
		    break;
		}

	      so->addr_low = li->segment_bases[0];
	      so->addr_high = (data->segments[i - 1].base
			       + data->segments[i - 1].size
			       + orig_delta);
	      gdb_assert (so->addr_low <= so->addr_high);
	    }
	}
    }

  offset = li->offsets[gdb_bfd_section_index (sec->the_bfd_section->owner,
					      sec->the_bfd_section)];
  sec->addr += offset;
  sec->endaddr += offset;
}

static int
solib_target_open_symbol_file_object (int from_tty)
{
  /* We can't locate the main symbol file based on the target's
     knowledge; the user has to specify it.  */
  return 0;
}

static int
solib_target_in_dynsym_resolve_code (CORE_ADDR pc)
{
  /* We don't have a range of addresses for the dynamic linker; there
     may not be one in the program's address space.  So only report
     PLT entries (which may be import stubs).  */
  return in_plt_section (pc);
}

static void
solib_target_ack_library (so_list *so)
{
  lm_info_target *lm = (lm_info_target *) so->lm_info;
  if (!lm->need_ack)
    return;

  /* Try only once, whether we succeed or not.  */
  lm->need_ack = false;
  switch (lm->location)
    {
    case lm_on_disk:
      target_ack_library (so->so_original_name);
      return;

    case lm_in_memory:
      target_ack_in_memory_library (lm->begin, lm->end);
      return;
    }

  warning (_("bad solib location '%d' for %s."), lm->location,
	   so->so_original_name);
}

const struct target_so_ops solib_target_so_ops =
{
  solib_target_relocate_section_addresses,
  solib_target_free_so,
  nullptr,
  solib_target_clear_solib,
  solib_target_solib_create_inferior_hook,
  solib_target_current_sos,
  solib_target_open_symbol_file_object,
  solib_target_in_dynsym_resolve_code,
  solib_bfd_open,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  gdb_bfd_open_from_target_memory,
  solib_target_ack_library,
};
