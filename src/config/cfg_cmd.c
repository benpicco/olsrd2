
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2011, the olsr.org team - see HISTORY file
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#ifndef WIN32
#include <alloca.h>
#else
#include <malloc.h>
#endif
#include <regex.h>
#include <stdlib.h>
#include <strings.h>

#include "common/autobuf.h"
#include "common/common_types.h"
#include "config/cfg_io.h"
#include "config/cfg_cmd.h"

struct _parsed_argument {
  char *type;
  char *name;
  char *key;
  char *value;
};

static int _do_parse_arg(struct cfg_cmd_state *state,
    char *arg, struct _parsed_argument *pa, struct autobuf *log);

/**
 * Initialize a command line parser state
 * @param state pointer to state
 */
void
cfg_cmd_add(struct cfg_cmd_state *state) {
  memset(state, 0, sizeof(*state));
}

/**
 * Cleans up a command line parser state
 * @param state pointer to state
 */
void
cfg_cmd_remove(struct cfg_cmd_state *state) {
  free(state->format);
  free(state->section_name);
  free(state->section_type);
  memset(state, 0, sizeof(*state));
}

/**
 * Implements the 'set' command for the command line
 * @param db pointer to cfg_db to be modified
 * @param state pointer to parser state
 * @param arg argument of command
 * @param log pointer for logging
 * @return 0 if succeeded, -1 otherwise
 */
int
cfg_cmd_handle_set(struct cfg_db *db,
    struct cfg_cmd_state *state, const char *arg, struct autobuf *log) {
  struct _parsed_argument pa;
  char *ptr;

  ptr = alloca(strlen(arg)+1);
  strcpy(ptr, arg);

  if (_do_parse_arg(state, ptr, &pa, log)) {
    return -1;
  }

  if (pa.value != NULL) {
    if (NULL == cfg_db_set_entry(db, state->section_type, state->section_name, pa.key, pa.value, true)) {
      cfg_append_printable_line(log, "Cannot create entry: '%s'\n", arg);
      return -1;
    }
    return 0;
  }

  if (pa.key != NULL) {
    cfg_append_printable_line(log, "Key without value is not allowed for set command: %s", arg);
    return -1;
  }

  /* set section */
  if (NULL == _cfg_db_add_section(db, state->section_type, state->section_name)) {
    cfg_append_printable_line(log, "Cannot create section: '%s'\n", arg);
    return -1;
  }
  return 0;
}

/**
 * Implements the 'remove' command for the command line
 * @param db pointer to cfg_db to be modified
 * @param state pointer to parser state
 * @param arg argument of command
 * @param log pointer for logging
 * @return 0 if succeeded, -1 otherwise
 */
int
cfg_cmd_handle_remove(struct cfg_db *db,
    struct cfg_cmd_state *state, const char *arg, struct autobuf *log) {
  struct _parsed_argument pa;
  char *ptr;

  ptr = alloca(strlen(arg)+1);
  strcpy(ptr, arg);

  if (_do_parse_arg(state, ptr, &pa, log)) {
    return -1;
  }

  if (pa.value != NULL) {
    cfg_append_printable_line(log, "Value is not allowed for remove command: %s", arg);
    return -1;
  }

  if (pa.key != NULL) {
    if (cfg_db_remove_entry(db, state->section_type, state->section_name, pa.key)) {
      cfg_append_printable_line(log, "Cannot remove entry: '%s'\n", arg);
      return -1;
    }
    return 0;
  }

  if (state->section_name) {
    if (cfg_db_remove_namedsection(db, state->section_type, state->section_name)) {
      cfg_append_printable_line(log, "Cannot remove section: '%s'\n", arg);
      return -1;
    }
  }

  if (state->section_type) {
    if (cfg_db_remove_sectiontype(db, state->section_type)) {
      cfg_append_printable_line(log, "Cannot remove section: '%s'\n", arg);
      return -1;
    }
  }
  return 0;
}

/**
 * Implements the 'view' command for the command line
 * @param db pointer to cfg_db to be modified
 * @param state pointer to parser state
 * @param arg argument of command
 * @param log pointer for logging
 * @return 0 if succeeded, -1 otherwise
 */
int
cfg_cmd_handle_get(struct cfg_db *db,
    struct cfg_cmd_state *state, const char *arg, struct autobuf *log) {
  struct cfg_section_type *type, *type_it;
  struct cfg_named_section *named, *named_it;
  struct cfg_entry *entry, *entry_it;
  struct _parsed_argument pa;
  char *ptr;

  if (arg == NULL || *arg == 0) {
    cfg_append_printable_line(log, "Section types in database:");

    CFG_FOR_ALL_SECTION_TYPES(db, type, type_it) {
      cfg_append_printable_line(log, "%s", type->type);
    }
    return 0;
  }

  ptr = alloca(strlen(arg)+1);
  strcpy(ptr, arg);

  if (_do_parse_arg(state, ptr, &pa, log)) {
    return -1;
  }

  if (pa.value != NULL) {
    cfg_append_printable_line(log, "Value is not allowed for view command: %s", arg);
    return -1;
  }

  if (pa.key != NULL) {
    if (NULL == (entry = cfg_db_find_entry(db, state->section_type, state->section_name, pa.key))) {
      cfg_append_printable_line(log, "Cannot find data for entry: '%s'\n", arg);
      return -1;
    }

    cfg_append_printable_line(log, "Key '%s' has value:", arg);
    CFG_FOR_ALL_STRINGS(&entry->val, ptr) {
      cfg_append_printable_line(log, "%s", ptr);
    }
    return 0;
  }

  if (pa.name == NULL) {
    type = cfg_db_find_sectiontype(db, pa.type);
    if (type == NULL || type->names.count == 0) {
      cfg_append_printable_line(log, "Cannot find data for section type: %s", arg);
      return -1;
    }

    named = avl_first_element(&type->names, named, node);
    if (cfg_db_is_named_section(named)) {
      cfg_append_printable_line(log, "Named sections in section type: %s", pa.type);
      CFG_FOR_ALL_SECTION_NAMES(type, named, named_it) {
        cfg_append_printable_line(log, "%s", named->name);
      }
      return 0;
    }
  }

  named = cfg_db_find_namedsection(db, pa.type, pa.name);
  if (named == NULL) {
    cfg_append_printable_line(log, "Cannot find data for section: %s", arg);
    return -1;
  }

  cfg_append_printable_line(log, "Entry keys for section '%s':", arg);
  CFG_FOR_ALL_ENTRIES(named, entry, entry_it) {
    cfg_append_printable_line(log, "%s", entry->name);
  }
  return 0;
}

/**
 * Implements the 'load' command for the command line
 * @param db pointer to cfg_db to be modified
 * @param state pointer to parser state
 * @param arg argument of command
 * @param log pointer for logging
 * @return 0 if succeeded, -1 otherwise
 */
int
cfg_cmd_handle_load(struct cfg_db *db,
    struct cfg_cmd_state *state, const char *arg, struct autobuf *log) {
  struct cfg_db *temp_db;

  temp_db = cfg_io_load_parser(arg, state->format, log);
  if (temp_db != NULL) {
    cfg_db_copy(db, temp_db);
    cfg_db_remove(temp_db);
  }
  return temp_db != NULL ? 0 : -1;
}

/**
 * Implements the 'save' command for the command line
 * @param db pointer to cfg_db to be modified
 * @param state pointer to parser state
 * @param arg argument of command
 * @param log pointer for logging
 * @return 0 if succeeded, -1 otherwise
 */
int
cfg_cmd_handle_save(struct cfg_db *db,
    struct cfg_cmd_state *state, const char *arg, struct autobuf *log) {
  return cfg_io_save_parser(arg, state->format, db, log);
}

/**
 * Implements the 'format' command for the command line
 * @param state pointer to parser state
 * @param arg argument of command
 * @return 0 if succeeded, -1 otherwise
 */
int
cfg_cmd_handle_format(struct cfg_cmd_state *state, const char *arg) {
  free (state->format);

  if (strcasecmp(arg, "auto") == 0) {
    state->format = NULL;
  }
  else {
    state->format = strdup(arg);
  }
  return 0;
}

/**
 * Implements the 'schema' command for the configuration system
 * @param db pointer to cfg_db to be modified
 * @param arg argument of command
 * @param log pointer for logging
 * @return 0 if succeeded, -1 otherwise
 */
int
cfg_cmd_handle_schema(struct cfg_db *db,
    const char *arg, struct autobuf *log) {
  struct cfg_schema_section *s_section, *s_section_it;
  struct cfg_schema_entry *s_entry, *s_entry_it;
  char *copy, *ptr;

  if (db->schema == NULL) {
    abuf_puts(log, "Internal error, database not connected to schema\n");
    return 1;
  }

  if (arg == NULL) {
    abuf_puts(log, "List of section types:\n"
        "(use this command with the types as parameter for more information)\n");

    CFG_FOR_ALL_SCHEMA_SECTIONS(db->schema, s_section, s_section_it) {
      cfg_append_printable_line(log, "    %s%s%s%s%s",
          s_section->t_type,
          s_section->t_named ? " (named)" : "",
          s_section->t_mandatory ? " (mandatory)" : "",
          s_section->t_help ? ": " : "",
          s_section->t_help ? s_section->t_help : "");
    }
    return 0;
  }

  /* copy string */
  copy = alloca(strlen(arg) + 1);
  strcpy(copy, arg);

  ptr = strchr(copy, '.');
  if (ptr) {
    *ptr++ = 0;
  }

  s_section = avl_find_element(&db->schema->sections, copy, s_section, node);
  if (s_section == NULL) {
    cfg_append_printable_line(log, "Unknown section type '%s'", copy);
    return 1;
  }

  if (ptr == NULL) {
    cfg_append_printable_line(log, "List of entries in section type '%s':", copy);
    abuf_puts(log, "(use this command with 'type.name' as parameter for more information)\n");
    CFG_FOR_ALL_SCHEMA_ENTRIES(s_section, s_entry, s_entry_it) {
      cfg_append_printable_line(log, "    %s%s%s%s%s",
          s_entry->t_name,
          s_entry->t_default == NULL ? " (mandatory)" : "",
          s_entry->t_list ? " (list)" : "",
          s_entry->t_help ? ": " : "",
          s_entry->t_help ? s_entry->t_help : "");
    }
    return 0;
  }

  s_entry = avl_find_element(&s_section->entries, ptr, s_entry, node);
  if (s_entry == NULL) {
    cfg_append_printable_line(log, "Unknown entry name '%s' in section type '%s'",
        ptr, copy);
    return 1;
  }

  cfg_append_printable_line(log, "%s.%s%s%s%s%s",
      s_section->t_type,
      s_entry->t_name,
      s_entry->t_default == NULL ? " (mandatory)" : "",
      s_entry->t_list ? " (list)" : "",
      s_entry->t_help ? ": " : "",
      s_entry->t_help ? s_entry->t_help : "");

  if (s_entry->t_default) {
    cfg_append_printable_line(log, "    Default value: '%s'", s_entry->t_default);
  }
  if (s_entry->t_validate) {
    s_entry->t_validate(s_entry, NULL, NULL, log);
  }
  return 0;
}

/**
 * Parse the parameter string for most commands
 * @param db pointer to cfg_db to be modified
 * @param state pointer to parser state
 * @param arg argument of command
 * @param set true if command should set a new entry
 * @param remove true if command should remove an existing value
 * @param log pointer for logging
 * @return 0 if succeeded, negative otherwise
 */
static int
_do_parse_arg(struct cfg_cmd_state *state,
    char *arg, struct _parsed_argument *pa, struct autobuf *log) {
  static const char *pattern = "^(([a-zA-Z_][a-zA-Z_0-9]*)(\\[([a-zA-Z_][a-zA-Z_0-9]*)\\])?\\.)?([a-zA-Z_][a-zA-Z_0-9]*)?(=(.*))?$";
  regex_t regexp;
  regmatch_t matchers[8];

  if (regcomp(&regexp, pattern, REG_EXTENDED)) {
    /* error in regexp implementation */
    cfg_append_printable_line(log, "Error while formatting regular expression for parsing.");
    return -2;
  }

  if (regexec(&regexp, arg, ARRAYSIZE(matchers), matchers, 0)) {
    cfg_append_printable_line(log, "Illegal input for command: %s", arg);
    regfree(&regexp);
    return -1;
  }

  memset(pa, 0, sizeof(*pa));
  if (matchers[2].rm_so != -1) {
    pa->type = &arg[matchers[2].rm_so];
    arg[matchers[2].rm_eo] = 0;

    free (state->section_type);
    state->section_type = strdup(pa->type);

    /* remove name */
    free (state->section_name);
    state->section_name = NULL;
  }
  if (matchers[4].rm_so != -1) {
    pa->name = &arg[matchers[4].rm_so];
    arg[matchers[4].rm_eo] = 0;

    /* name has already been deleted by section type code */
    state->section_name = strdup(pa->name);
  }
  if (matchers[5].rm_so != -1) {
    pa->key = &arg[matchers[5].rm_so];
    arg[matchers[5].rm_eo] = 0;
  }
  if (matchers[7].rm_so != -1) {
    pa->value = &arg[matchers[7].rm_so];
  }

  regfree(&regexp);
  return 0;
}
