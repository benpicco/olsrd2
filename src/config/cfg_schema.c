
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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/netaddr.h"

#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/string.h"

#include "config/cfg.h"
#include "config/cfg_db.h"
#include "config/cfg_schema.h"

static bool _validate_cfg_entry(struct cfg_schema_section *schema_section,
    struct cfg_db *db, struct cfg_section_type *section,
    struct cfg_named_section *named, struct cfg_entry *entry,
    const char *section_name, bool cleanup, struct autobuf *out);
static bool
_check_missing_entries(struct cfg_schema_section *schema_section,
    struct cfg_db *db, struct cfg_section_type *section,
    struct cfg_named_section *named,
    const char *section_name, struct autobuf *out);
static bool _extract_netaddr_filter(const struct cfg_schema_entry *entry,
    int *address_family_1, int *address_family_2);

const char *CFGLIST_BOOL_TRUE[] = { "true", "1", "on", "yes" };
const char *CFGLIST_BOOL[] = { "true", "1", "on", "yes", "false", "0", "off", "no" };

/**
 * Initialize a schema
 * @param schema pointer to uninitialized schema
 */
void
cfg_schema_add(struct cfg_schema *schema) {
  avl_init(&schema->sections, cfg_avlcmp_keys, true, NULL);
}

/**
 * Removes all connected objects of a schema
 * @param schema pointer to schema
 */
void
cfg_schema_remove(struct cfg_schema *schema) {
  struct cfg_schema_section *t_section, *tsec_it;

  /* kill sections of schema */
  CFG_FOR_ALL_SCHEMA_SECTIONS(schema, t_section, tsec_it) {
    cfg_schema_remove_section(schema, t_section);
  }
}

/**
 * Add a section to a schema
 * @param schema pointer to configuration schema
 * @param section pointer to section
 * @return -1 if an error happened, 0 otherwise
 */
int
cfg_schema_add_section(struct cfg_schema *schema, struct cfg_schema_section *section) {
  assert (cfg_is_allowed_key(section->type));

  section->_node.key = section->type;
  if (avl_insert(&schema->sections, &section->_node)) {
    /* name collision */
    section->_node.key = NULL;
    return -1;
  }

  avl_init(&section->_entries, cfg_avlcmp_keys, false, NULL);
  return 0;
}

/**
 * Removes a section from a schema
 * @param schema pointer to configuration schema
 * @param section pointer to section
 */
void
cfg_schema_remove_section(struct cfg_schema *schema, struct cfg_schema_section *section) {
  struct cfg_schema_entry *entry, *ent_it;

  if (section->_node.key) {
    /* kill _entries of section_schema */
    CFG_FOR_ALL_SCHEMA_ENTRIES(section, entry, ent_it) {
      cfg_schema_remove_entry(section, entry);
    }

    avl_remove(&schema->sections, &section->_node);
    section->_node.key = NULL;
  }
}

/**
 * Adds a series of _entries to a schema section
 * @param section pointer to section
 * @param _entries pointer to array of _entries
 * @param e_cnt number of array _entries
 * @return -1 if an error happened, 0 otherwise
 */
int
cfg_schema_add_entries(struct cfg_schema_section *section,
    struct cfg_schema_entry *entries, size_t e_cnt) {
  size_t i;

  for (i=0; i<e_cnt; i++) {
    if (cfg_schema_add_entry(section, &entries[i])) {
      /* error, while adding entry, remove all of them again */
      cfg_schema_remove_entries(section, entries, e_cnt);
      return -1;
    }
  }
  return 0;
}

/**
 * Adds a single entry to a schema section, section must have
 * already be added to the schema.
 * @param section pointer to section
 * @param entry pointer to entry
 * @return -1 if an error happened, 0 otherwise
 */
int
cfg_schema_add_entry(struct cfg_schema_section *section, struct cfg_schema_entry *entry) {
  assert (cfg_is_allowed_key(entry->name));
  assert (avl_is_node_added(&section->_node));

  entry->_node.key = &entry->name[0];
  if (avl_insert(&section->_entries, &entry->_node)) {
    /* name collision */
    entry->_node.key = NULL;
    return -1;
  }

  return 0;
}

/**
 * Remove an array of _entries from a schema section
 * @param section pointer to section
 * @param _entries pointer to array of _entries
 * @param e_cnt number of array _entries
 */
void
cfg_schema_remove_entries(struct cfg_schema_section *section, struct cfg_schema_entry *entries, size_t e_cnt) {
  size_t i;

  for (i=0; i<e_cnt; i++) {
    cfg_schema_remove_entry(section, &entries[i]);
  }
}

/**
 * Validates a database with a schema
 * @param db pointer to configuration database
 * @param cleanup if true, bad values will be removed from the database
 * @param ignore_unknown_sections true if the validation should skip sections
 *   in the database that have no schema.
 * @param out autobuffer for validation output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
cfg_schema_validate(struct cfg_db *db,
    bool cleanup, bool ignore_unknown_sections,
    struct autobuf *out) {
  char section_name[256];
  struct cfg_section_type *section, *section_it;
  struct cfg_named_section *named, *named_it;
  struct cfg_entry *entry, *entry_it;

  struct cfg_schema_section *schema_section, *schema_section_it;
  struct cfg_schema_section *schema_section_first, *schema_section_last;

  bool error = false;
  bool warning = false;
  bool hasName = false;

  if (db->schema == NULL) {
    return -1;
  }

  CFG_FOR_ALL_SECTION_TYPES(db, section, section_it) {
    /* check for missing schema sections */
    schema_section_first = avl_find_element(&db->schema->sections, section->type,
        schema_section_first, _node);

    if (schema_section_first == NULL) {
      if (ignore_unknown_sections) {
        continue;
      }

      cfg_append_printable_line(out,
          "Cannot find schema for section type '%s'", section->type);

      if (cleanup) {
        cfg_db_remove_sectiontype(db, section->type);
      }

      error |= true;
      continue;
    }

    schema_section_last = avl_find_le_element(&db->schema->sections, section->type,
        schema_section_last, _node);

    /* iterate over all schema for a certain section type */
    avl_for_element_range(schema_section_first, schema_section_last, schema_section, _node) {
      /* check data of named sections in db */
      CFG_FOR_ALL_SECTION_NAMES(section, named, named_it) {
        warning = false;
        hasName = cfg_db_is_named_section(named);

        if (schema_section->named && !hasName) {
          cfg_append_printable_line(out, "The section type '%s' demands a name", section->type);

          warning = true;
        }
        else if (!schema_section->named && hasName) {
          cfg_append_printable_line(out, "The section type '%s'"
              " has to be used without a name"
              " ('%s' was given as a name)", section->type, named->name);

          warning = true;
        }

        if (hasName && !cfg_is_allowed_key(named->name)) {
          cfg_append_printable_line(out, "The section name '%s' for"
              " type '%s' contains illegal characters",
              named->name, section->type);
          warning = true;
        }

        /* test abort condition */
        if (warning && cleanup) {
          /* remove bad named section */
          cfg_db_remove_namedsection(db, section->type, named->name);
        }

        error |= warning;

        if (warning) {
          continue;
        }

        /* initialize section_name field for validate */
        snprintf(section_name, sizeof(section_name), "'%s%s%s'",
            section->type, hasName ? "=" : "", hasName ? named->name : "");

        /* check for bad values */
        CFG_FOR_ALL_ENTRIES(named, entry, entry_it) {
          warning = _validate_cfg_entry(schema_section,
              db, section, named, entry, section_name,
              cleanup, out);
          error |= warning;
        }

        if (schema_section->cb_validate) {
          if (schema_section->cb_validate(schema_section, section_name, named, out)) {
            error = true;
          }
        }
        /* check for missing values */
        warning = _check_missing_entries(schema_section, db, section, named, section_name, out);
        error |= warning;
      }
    }
    if (cleanup && avl_is_empty(&section->names)) {
      /* if section type is empty, remove it too */
      cfg_db_remove_sectiontype(db, section->type);
    }
  }

  /* search for missing mandatory sections */
  CFG_FOR_ALL_SCHEMA_SECTIONS(db->schema, schema_section, schema_section_it) {
    if (!schema_section->mandatory) {
      continue;
    }

    section = cfg_db_find_sectiontype(db, schema_section->type);
    warning = section == NULL || avl_is_empty(&section->names);
    if (warning) {
      cfg_append_printable_line(out, "Missing mandatory section of type '%s'",
          schema_section->type);
    }
    error |= warning;
  }
  return error ? -1 : 0;
}

/**
 * Convert the _entries of a db section into binary representation by
 * using the mappings defined in a schema section. The function assumes
 * that the section was already validated.
 * @param target pointer to target binary buffer
 * @param named pointer to named section
 * @param _entries pointer to array of schema _entries
 * @param count number of schema _entries
 * @return 0 if conversion was successful, -1 if an error happened.
 *   An error might result in a partial initialized target buffer.
 */
int
cfg_schema_tobin(void *target, struct cfg_named_section *named,
    const struct cfg_schema_entry *entries, size_t count) {
  char *ptr;
  size_t i;
  const struct const_strarray *value;

  ptr = (char *)target;

  for (i=0; i<count; i++) {
    if (entries[i].cb_to_binary == NULL) {
      continue;
    }

    /* cleanup pointer */
    if (named) {
      value = cfg_db_get_entry_value(
          named->section_type->db,
          named->section_type->type,
          named->name,
          entries[i].name);
    }
    else {
      value = &entries[i].def;
    }

    if (entries[i].cb_to_binary(&entries[i], value, ptr + entries[i].t_offset)) {
      /* error in conversion */
      return -1;
    }
  }
  return 0;
}

/**
 * Schema entry validator for string maximum length.
 * See CFG_VALIDATE_STRING_LEN macro in cfg_schema.h
 * @param entry pointer to schema entry
 * @param section_name name of section type and name
 * @param value value of schema entry, NULL for help text generation
 * @param out pointer to autobuffer for validator output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
cfg_schema_validate_strlen(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out) {
  if ((int)strlen(value) > entry->validate_params.p_i1) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s is longer than %d characters",
        value, entry->name, section_name, entry->validate_params.p_i1);
    return 1;
  }
  return 0;
}

/**
 * Schema entry validator for strings printable characters
 * and a maximum length.
 * See CFG_VALIDATE_PRINTABLE() and CFG_VALIDATE_PRINTABLE_LEN()
 * macro in cfg_schema.h
 * @param entry pointer to schema entry
 * @param section_name name of section type and name
 * @param value value of schema entry
 * @param out pointer to autobuffer for validator output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
cfg_schema_validate_printable(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out) {
  if (cfg_schema_validate_strlen(entry, section_name, value, out)) {
    return 1;
  }
  if (!cfg_is_printable(value)) {
    /* not a printable ascii character */
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s has has non-printable characters",
        value, entry->name, section_name);
    return 1;

  }
  return 0;
}

/**
 * Schema entry validator for choice (list of possible strings)
 * List selection will be case insensitive.
 * See CFG_VALIDATE_CHOICE() macro in cfg_schema.h
 * @param entry pointer to schema entry
 * @param section_name name of section type and name
 * @param value value of schema entry
 * @param out pointer to autobuffer for validator output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
cfg_schema_validate_choice(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out) {
  const char **list = entry->validate_params.p_ptr;
  int i;

  i = cfg_get_choice_index(value, list, (size_t)entry->validate_params.p_i1);
  if (i >= 0) {
    return 0;
  }

  cfg_append_printable_line(out, "Unknown value '%s'"
      " for entry '%s' in section %s",
      value, entry->name, section_name);
  return -1;
}

/**
 * Schema entry validator for integers.
 * See CFG_VALIDATE_INT() and CFG_VALIDATE_INT_MINMAX() macro in cfg_schema.h
 * @param entry pointer to schema entry
 * @param section_name name of section type and name
 * @param value value of schema entry
 * @param out pointer to autobuffer for validator output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
cfg_schema_validate_int(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out) {
  int32_t i;
  char *endptr = NULL;

  i = strtol(value, &endptr, 10);
  if (endptr == NULL || *endptr != 0) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s is not an integer",
        value, entry->name, section_name);
    return 1;
  }
  if (i < entry->validate_params.p_i1 || i > entry->validate_params.p_i2) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s' in section %s is "
        "not between %d and %d",
        value, entry->name, section_name,
        entry->validate_params.p_i1, entry->validate_params.p_i2);
    return 1;
  }
  return 0;
}

/**
 * Schema entry validator for network addresses and prefixes.
 * See CFG_VALIDATE_NETADDR_*() macros in cfg_schema.h
 * @param entry pointer to schema entry
 * @param section_name name of section type and name
 * @param value value of schema entry
 * @param out pointer to autobuffer for validator output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
cfg_schema_validate_netaddr(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out) {
  struct netaddr addr;
  bool prefix = false;
  int af1, af2;
  uint8_t max_prefix;

  prefix = _extract_netaddr_filter(entry, &af1, &af2);

  if (netaddr_from_string(&addr, value)) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s is no valid network address",
        value, entry->name, section_name);
    return -1;
  }

  max_prefix = netaddr_get_maxprefix(&addr);

  /* check prefix length */
  if (addr.prefix_len > max_prefix) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s has an illegal prefix length",
        value, entry->name, section_name);
    return -1;
  }
  if (!prefix && addr.prefix_len != max_prefix) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s must be a single address, not a prefix",
        value, entry->name, section_name);
    return -1;
  }

  if (af1 == 0) {
    /* no further type check */
    return 0;
  }

  /* check address type */
  if ((af2 != 0 && af2 == addr.type) || (af1 == addr.type)) {
    return 0;
  }

  /* at least one condition was set, but no one matched */
  cfg_append_printable_line(out, "Value '%s' for entry '%s'"
      " in section '%s' is wrong address type",
      value, entry->name, section_name);
  return -1;
}

/**
 * Help generator for string maximum length validator.
 * See CFG_VALIDATE_STRING_LEN macro in cfg_schema.h
 * @param entry pointer to schema entry
 * @param out pointer to autobuffer for help output
 */
void
cfg_schema_help_strlen(
    const struct cfg_schema_entry *entry, struct autobuf *out) {
  if (entry->validate_params.p_i1 < INT32_MAX) {
    cfg_append_printable_line(out, "    Parameter must have a maximum length of %d characters",
        entry->validate_params.p_i1);
  }
}

/**
 * Help generator for strings printable characters
 * and a maximum length validator.
 * See CFG_VALIDATE_PRINTABLE() and CFG_VALIDATE_PRINTABLE_LEN()
 * macro in cfg_schema.h
 * @param entry pointer to schema entry
 * @param value value of schema entry
 * @param out pointer to autobuffer for validator output
 */
void
cfg_schema_help_printable(
    const struct cfg_schema_entry *entry, struct autobuf *out) {
  cfg_schema_help_printable(entry, out);
  cfg_append_printable_line(out, "    Parameter must only contain printable characters.");
}

/**
 * Help generator for choice (list of possible strings) validator
 * List selection will be case insensitive.
 * See CFG_VALIDATE_CHOICE() macro in cfg_schema.h
 * @param entry pointer to schema entry
 * @param out pointer to autobuffer for validator output
 */
void
cfg_schema_help_choice(
    const struct cfg_schema_entry *entry, struct autobuf *out) {
  const char **list = entry->validate_params.p_ptr;
  int i;

  cfg_append_printable_line(out, "    Parameter must be on of the following list:");

  abuf_puts(out, "    ");
  for (i=0; i < entry->validate_params.p_i1; i++) {
    abuf_appendf(out, "%s'%s'",
        i==0 ? "" : ", ", list[i]);
  }
  abuf_puts(out, "\n");
}

/**
 * Help generator for integer validator.
 * See CFG_VALIDATE_INT() and CFG_VALIDATE_INT_MINMAX() macro in cfg_schema.h
 * @param entry pointer to schema entry
 * @param out pointer to autobuffer for validator output
 */
void
cfg_schema_help_int(
    const struct cfg_schema_entry *entry, struct autobuf *out) {
  cfg_append_printable_line(out, "    Parameter must be an integer between %d and %d",
      entry->validate_params.p_i1, entry->validate_params.p_i2);
}

/**
 * Help generator for network addresses and prefixes validator.
 * See CFG_VALIDATE_NETADDR_*() macros in cfg_schema.h
 * @param entry pointer to schema entry
 * @param out pointer to autobuffer for validator output
 */
void
cfg_schema_help_netaddr(
    const struct cfg_schema_entry *entry, struct autobuf *out) {
  const char *p_string;
  bool prefix = false;
  int af1,af2;

  prefix = _extract_netaddr_filter(entry, &af1, &af2);
  p_string = prefix ? " with optional prefix string" : "";

  switch (af1) {
    case AF_INET:
      cfg_append_printable_line(out, "    Parameter must be an IPv4%s address%s",
          af2 == AF_INET6 ? " or IPv6" : "", p_string);
      break;
    case AF_INET6:
      cfg_append_printable_line(out, "    Parameter must be an IPv6 address%s",
          p_string);
      break;
    case AF_MAC48:
      cfg_append_printable_line(out, "    Parameter must be an MAC-48%s address%s",
          af2 == AF_EUI64 ? " or EUI64" : "", p_string);
      break;
    case AF_EUI64:
      cfg_append_printable_line(out, "    Parameter must be an EUI-64 address%s",
          p_string);
      break;
    default:
      cfg_append_printable_line(out, "    Parameter must be an IPv4, "
          "IPv6, MAC-48 or EUI-64 address%s", p_string);
      break;
  }
}

/**
 * Binary converter for string pointers. This validator will
 * allocate additional memory for the string.
 * See CFG_MAP_STRING() and CFG_MAP_STRING_LEN() macro
 * in cfg_schema.h
 * @param s_entry pointer to configuration entry schema.
 * @param value pointer to value of configuration entry.
 * @param reference pointer to binary output buffer.
 * @return 0 if conversion succeeded, -1 otherwise.
 */
int
cfg_schema_tobin_strptr(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    const struct const_strarray *value, void *reference) {
  char **ptr;

  ptr = (char **)reference;
  if (*ptr) {
    free(*ptr);
  }

  *ptr = strdup(strarray_get_first_c(value));
  return *ptr == NULL ? -1 : 0;
}

/**
 * Binary converter for string arrays.
 * See CFG_MAP_STRING_ARRAY() macro in cfg_schema.h
 * @param s_entry pointer to configuration entry schema.
 * @param value pointer to value of configuration entry.
 * @param reference pointer to binary output buffer.
 * @return 0 if conversion succeeded, -1 otherwise.
 */
int
cfg_schema_tobin_strarray(const struct cfg_schema_entry *s_entry,
    const struct const_strarray *value, void *reference) {
  char *ptr;

  ptr = (char *)reference;

  strscpy(ptr, strarray_get_first_c(value), (size_t)s_entry->validate_params.p_i1);
  return 0;
}

/**
 * Binary converter for integers chosen as an index in a predefined
 * string list.
 * See CFG_MAP_CHOICE() macro in cfg_schema.h
 * @param s_entry pointer to configuration entry schema.
 * @param value pointer to value of configuration entry.
 * @param reference pointer to binary output buffer.
 * @return 0 if conversion succeeded, -1 otherwise.
 */
int
cfg_schema_tobin_choice(const struct cfg_schema_entry *s_entry,
    const struct const_strarray *value, void *reference) {
  int *ptr;

  ptr = (int *)reference;

  *ptr = cfg_get_choice_index(strarray_get_first_c(value),
      s_entry->validate_params.p_ptr,
      (size_t)s_entry->validate_params.p_i1);
  return 0;
}

/**
 * Binary converter for integers.
 * See CFG_MAP_INT() macro in cfg_schema.h
 * @param s_entry pointer to configuration entry schema.
 * @param value pointer to value of configuration entry.
 * @param reference pointer to binary output buffer.
 * @return 0 if conversion succeeded, -1 otherwise.
 */
int
cfg_schema_tobin_int(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    const struct const_strarray *value, void *reference) {
  int *ptr;

  ptr = (int *)reference;

  *ptr = strtol(strarray_get_first_c(value), NULL, 10);
  return 0;
}

/**
 * Binary converter for netaddr objects.
 * See CFG_MAP_NETADDR_*() macros in cfg_schema.h
 * @param s_entry pointer to configuration entry schema.
 * @param value pointer to value of configuration entry.
 * @param reference pointer to binary output buffer.
 * @return 0 if conversion succeeded, -1 otherwise.
 */int
cfg_schema_tobin_netaddr(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    const struct const_strarray *value, void *reference) {
  struct netaddr *ptr;

  ptr = (struct netaddr *)reference;

  return netaddr_from_string(ptr, strarray_get_first_c(value));
}

 /**
  * Binary converter for booleans.
  * See CFG_MAP_BOOL() macro in cfg_schema.h
  * @param s_entry pointer to configuration entry schema.
  * @param value pointer to value of configuration entry.
  * @param reference pointer to binary output buffer.
  * @return 0 if conversion succeeded, -1 otherwise.
  */
int
cfg_schema_tobin_bool(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    const struct const_strarray *value, void *reference) {
  bool *ptr;

  ptr = (bool *)reference;

  *ptr = cfg_get_bool(strarray_get_first_c(value));
  return 0;
}

/**
 * Binary converter for list of strings.
 * See CFG_MAP_STRINGLIST() macro in cfg_schema.h
 * @param s_entry pointer to configuration entry schema.
 * @param value pointer to value of configuration entry.
 * @param reference pointer to binary output buffer.
 * @return 0 if conversion succeeded, -1 otherwise.
 */
int
cfg_schema_tobin_stringlist(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    const struct const_strarray *value, void *reference) {
  struct strarray *array;

  array = (struct strarray *)reference;

  return strarray_copy_c(array, value);
}

/**
 * Validates on configuration entry.
 * @param schema_section pointer to schema section
 * @param db pointer to database
 * @param section pointer to database section type
 * @param named pointer to named section
 * @param entry pointer to configuration entry
 * @param section_name name of section including type (for debug output)
 * @param cleanup true if bad _entries should be removed
 * @param out error output buffer
 * @return true if an error happened, false otherwise
 */
static bool
_validate_cfg_entry(struct cfg_schema_section *schema_section,
    struct cfg_db *db, struct cfg_section_type *section,
    struct cfg_named_section *named, struct cfg_entry *entry,
    const char *section_name, bool cleanup, struct autobuf *out) {
  struct cfg_schema_entry *schema_entry;
  bool warning = false;
  char *ptr1 = NULL;

  schema_entry = cfg_schema_find_entry(schema_section, entry->name);

  if (schema_entry == NULL) {
    cfg_append_printable_line(out, "Unknown entry '%s'"
        " for section type '%s'", entry->name, section->type);
    return true;
  }

  if (schema_entry->cb_validate == NULL) {
    return false;
  }

  /* now validate syntax */
  ptr1 = entry->val.value;
  while (ptr1 < entry->val.value + entry->val.length) {
    if (schema_entry->cb_validate(schema_entry, section_name, ptr1, out)) {
      /* warning is generated by the validate callback itself */
      warning = true;
    }

    if (warning && cleanup) {
      /* illegal entry found, remove it */
      strarray_remove_ext(&entry->val, ptr1, false);
    }
    else {
      ptr1 += strlen(ptr1) + 1;
    }

    if (!schema_entry->list) {
      /* ignore the rest */
      break;
    }
  }

  if (strarray_is_empty(&entry->val)
      || (!schema_entry->list && warning && cleanup)) {
    /* remove entry */
    cfg_db_remove_entry(db, section->type, named->name, entry->name);
  }

  return warning;
}

/**
 * Checks a database section for missing mandatory _entries
 * @param schema_section pointer to schema of section
 * @param db pointer to database
 * @param section pointer to database section type
 * @param named pointer to named section
 * @param section_name name of section including type (for debug output)
 * @param out error output buffer
 * @return true if an error happened, false otherwise
 */
static bool
_check_missing_entries(struct cfg_schema_section *schema_section,
    struct cfg_db *db, struct cfg_section_type *section,
    struct cfg_named_section *named,
    const char *section_name, struct autobuf *out) {
  struct cfg_schema_entry *schema_entry, *schema_entry_it;
  bool warning, error;

  warning = false;
  error = false;

  /* check for missing values */
  CFG_FOR_ALL_SCHEMA_ENTRIES(schema_section, schema_entry, schema_entry_it) {
    if (!strarray_is_empty_c(&schema_entry->def)) {
      continue;
    }

    /* mandatory parameter */
    warning = !cfg_db_find_entry(db, section->type, named->name, schema_entry->name);
    error |= warning;
    if (warning) {
      cfg_append_printable_line(out, "Missing mandatory value for entry '%s' in section %s",
          schema_entry->name, section_name);
    }
  }
  return error;
}

/**
 * Extract the encoded address families and the prefix flag
 * from the parameters of a netaddr validator.
 * @param entry pointer to schema entry
 * @param address_family_1 pointer to address family 1 (return value)
 * @param address_family_2 pointer to address family 2 (return value)
 * @return true if the validator allows a prefix, false otherwise
 */
static bool
_extract_netaddr_filter(const struct cfg_schema_entry *entry,
    int *address_family_1, int *address_family_2) {
  bool prefix = false;
  int p1, p2;

  p1 = entry->validate_params.p_i1;
  p2 = entry->validate_params.p_i2;

  /* check if we may accept a prefix */
  if (p1 < 0) {
    prefix = true;
    p1 = -p1;
  }
  if (p2 < 0) {
    prefix = true;
    p2 = -p2;

    /* explicit 'all addresses, but no prefix' case */
    if (p1 == 0) {
      p2 = 0;
    }
  }

  *address_family_1 = p1;
  *address_family_2 = p2;
  return prefix;
}
