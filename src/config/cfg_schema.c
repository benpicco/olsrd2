
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
    const char *section_name, bool cleanup, bool failFast,
    struct autobuf *out);
static bool
_check_missing_entries(struct cfg_schema_section *schema_section,
    struct cfg_db *db, struct cfg_section_type *section,
    struct cfg_named_section *named,
    bool failFast, const char *section_name, struct autobuf *out);
static bool
_check_single_value(struct cfg_schema_entry *schema_entry,
    struct cfg_db *db, struct cfg_section_type *section,
    struct cfg_named_section *named, struct cfg_entry *entry,
    bool cleanup, const char *section_name, struct autobuf *out);

const char *CFGLIST_BOOL_TRUE[] = { "true", "1", "on", "yes" };
const char *CFGLIST_BOOL[] = { "true", "1", "on", "yes", "false", "0", "off", "no" };

/**
 * Initialize a schema
 * @param schema pointer to uninitialized schema
 */
void
cfg_schema_add(struct cfg_schema *schema) {
  avl_init(&schema->sections, cfg_avlcmp_keys, false, NULL);
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
  assert (cfg_is_allowed_key(section->t_type));
  assert (section->t_optional || !section->t_named);

  section->node.key = section->t_type;
  if (avl_insert(&schema->sections, &section->node)) {
    /* name collision */
    return -1;
  }

  avl_init(&section->entries, cfg_avlcmp_keys, false, NULL);
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

  /* kill entries of section_schema */
  CFG_FOR_ALL_SCHEMA_ENTRIES(section, entry, ent_it) {
    cfg_schema_remove_entry(section, entry);
  }

  avl_remove(&schema->sections, &section->node);
}

/**
 * Adds a series of entries to a schema section
 * @param section pointer to section
 * @param entries pointer to array of entries
 * @param e_cnt number of array entries
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
 * Adds a single entry to a schema section
 * @param section pointer to section
 * @param entry pointer to entry
 * @return -1 if an error happened, 0 otherwise
 */
int
cfg_schema_add_entry(struct cfg_schema_section *section, struct cfg_schema_entry *entry) {
  assert (cfg_is_allowed_key(entry->t_name));

  entry->node.key = &entry->t_name[0];
  if (avl_insert(&section->entries, &entry->node)) {
    /* name collision */
    return -1;
  }
  return 0;
}

/**
 * Remove an array of entries from a schema section
 * @param section pointer to section
 * @param entries pointer to array of entries
 * @param e_cnt number of array entries
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
 * @param failFast if true, validation stops at the first error
 * @param cleanup if true, bad values will be removed from the database
 * @param ignore_unknown_sections true if the validation should skip sections
 *   in the database that have no schema.
 * @param out autobuffer for validation output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
cfg_schema_validate(struct cfg_db *db,
    bool failFast, bool cleanup, bool ignore_unknown_sections,
    struct autobuf *out) {
  char section_name[256];
  struct cfg_section_type *section, *section_it;
  struct cfg_named_section *named, *named_it;
  struct cfg_entry *entry, *entry_it;

  struct cfg_schema_section *schema_section, *schema_section_it;

  bool error = false;
  bool warning = false;
  bool hasName = false;

  if (db->schema == NULL) {
    return 1;
  }

  CFG_FOR_ALL_SECTION_TYPES(db, section, section_it) {
    /* check for missing schema sections */
    schema_section = cfg_schema_find_section(db->schema, section->type);
    if (schema_section == NULL) {
      if (ignore_unknown_sections) {
        continue;
      }

      cfg_append_printable_line(out,
          "Cannot find schema for section type '%s'", section->type);

      if (cleanup) {
        cfg_db_remove_sectiontype(db, section->type);
      }
      if (failFast) {
        /* stop here */
        return -1;
      }

      error |= true;
      continue;
    }

    /* check data of named sections in db */
    CFG_FOR_ALL_SECTION_NAMES(section, named, named_it) {
      warning = false;
      hasName = cfg_db_is_named_section(named);

      if (schema_section->t_named && !hasName) {
        cfg_append_printable_line(out, "The section type '%s' demands a name", section->type);

        warning = true;
      }
      else if (!schema_section->t_named && hasName) {
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

      if (warning && failFast) {
        /* stop here */
        break;
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
            cleanup, failFast, out);
        if (warning && failFast) {
          /* stop here */
          return -1;
        }
        error |= warning;
      }

      if (schema_section->t_validate) {
        if (schema_section->t_validate(schema_section, section_name, named, out)) {
          if (failFast) {
            /* stop here */
            return -1;
          }
          error = true;
        }
      }
      /* check for missing values */
      warning = _check_missing_entries(schema_section, db, section, named, failFast, section_name, out);
      if (warning && failFast) {
        /* stop here */
        return -1;
      }
      error |= warning;
    }

    if (cleanup && avl_is_empty(&section->names)) {
      /* if section type is empty, remove it too */
      cfg_db_remove_sectiontype(db, section->type);
    }
    if (warning && failFast) {
      return -1;
    }
  }

  /* search for missing mandatory sections */
  CFG_FOR_ALL_SCHEMA_SECTIONS(db->schema, schema_section, schema_section_it) {
    if (!schema_section->t_mandatory) {
      continue;
    }

    section = cfg_db_find_sectiontype(db, schema_section->t_type);
    warning = section == NULL || avl_is_empty(&section->names);
    if (warning) {
      cfg_append_printable_line(out, "Missing mandatory section of type '%s'",
          schema_section->t_type);
    }
    if (warning && failFast) {
      /* stop here */
      return -1;
    }
    error |= warning;
  }
  return error ? -1 : 0;
}

int
cfg_schema_tobin(void *target, struct cfg_named_section *named,
    const struct cfg_schema_entry *entries, size_t count) {
  char *ptr;
  size_t i;
  struct cfg_stringarray default_array, *value;

  ptr = (char *)target;

  for (i=0; i<count; i++) {
    if (entries[i].t_to_binary == NULL) {
      continue;
    }

    /* cleanup pointer */
    value = NULL;

    if (named) {
      struct cfg_entry *db_entry;
      db_entry = avl_find_element(&named->entries, entries[i].t_name, db_entry, node);
      if (db_entry) {
        value = &db_entry->val;
      }
    }

    if (value == NULL) {
      if (entries[i].t_default == NULL) {
        /* missing mandatory entry */
        return -1;
      }
      memcpy(&default_array.value, &entries[i].t_default, sizeof(default_array.value));
      default_array.last_value = default_array.value;
      default_array.length = strlen(default_array.value);

      value = &default_array;
    }

    if (entries[i].t_to_binary(&entries[i], value, ptr + entries[i].t_offset)) {
      /* error in conversion */
      return -1;
    }
  }
  return 0;
}

/**
 * Schema entry validator for string maximum length.
 * @param entry pointer to schema entry
 * @param section_name name of section type and name
 * @param value value of schema entry, NULL for help text generation
 * @param out pointer to autobuffer for validator output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
cfg_schema_validate_strlen(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out) {
  if (value == NULL) {
    if (entry->t_validate_params.p_i1 < INT32_MAX) {
      cfg_append_printable_line(out, "    Parameter must have a maximum length of %d characters",
          entry->t_validate_params.p_i1);
    }
    return 0;
  }
  if ((int)strlen(value) > entry->t_validate_params.p_i1) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s is longer than %d characters",
        value, entry->t_name, section_name, entry->t_validate_params.p_i1);
    return 1;
  }
  return 0;
}

/**
 * Schema entry validator for strings printable characters
 * and a maximum length.
 * See CFG_VALIDATE_PRINTABLE() und CFG_VALIDATE_PRINTABLE_LEN()
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
  if (value == NULL) {
    cfg_append_printable_line(out, "    Parameter must only contain printable characters.");
    return 0;
  }
  if (!cfg_is_printable(value)) {
    /* not a printable ascii character */
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s has has non-printable characters",
        value, entry->t_name, section_name);
    return 1;

  }
  return 0;
}

/**
 * Schema entry validator for choice (list of possible strings)
 * See CFG_VALIDATE_CHOICE() macro in cfg_schema.h
 * List selection will be case insensitive.
 * @param entry pointer to schema entry
 * @param section_name name of section type and name
 * @param value value of schema entry
 * @param out pointer to autobuffer for validator output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
cfg_schema_validate_choice(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out) {
  const char **list = entry->t_validate_params.p_ptr;
  int i;

  if (value == NULL) {
    cfg_append_printable_line(out, "    Parameter must be on of the following list:");

    abuf_puts(out, "    ");
    for (i=0; i < entry->t_validate_params.p_i1; i++) {
      abuf_appendf(out, "%s'%s'",
          i==0 ? "" : ", ", list[i]);
    }
    abuf_puts(out, "\n");
    return 0;
  }
  i = cfg_get_choice_index(value, list, (size_t)entry->t_validate_params.p_i1);
  if (i >= 0) {
    return 0;
  }

  cfg_append_printable_line(out, "Unknown value '%s'"
      " for entry '%s' in section %s",
      value, entry->t_name, section_name);
  return -1;
}

/**
 * Schema entry validator for integers
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

  if (value == NULL) {
    cfg_append_printable_line(out, "    Parameter must be an integer between %d and %d",
        entry->t_validate_params.p_i1, entry->t_validate_params.p_i2);
    return 0;
  }

  i = strtol(value, &endptr, 10);
  if (endptr == NULL || *endptr != 0) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s is not an integer",
        value, entry->t_name, section_name);
    return 1;
  }
  if (i < entry->t_validate_params.p_i1 || i > entry->t_validate_params.p_i2) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s' in section %s is "
        "not between %d and %d",
        value, entry->t_name, section_name,
        entry->t_validate_params.p_i1, entry->t_validate_params.p_i2);
    return 1;
  }
  return 0;
}

/**
 * Schema entry validator for network addresses and prefixes
 * See CFG_VALIDATE_NETADDR_() macros in cfg_schema.h
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
  int p1,p2;
  uint8_t max_prefix;

  p1 = entry->t_validate_params.p_i1;
  p2 = entry->t_validate_params.p_i2;

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

  if (value == NULL) {
    const char *p_string = prefix ? " with optional prefix string" : "";

    switch (p1) {
      case AF_INET:
        cfg_append_printable_line(out, "    Parameter must be an IPv4%s address%s",
            p2 == AF_INET6 ? " or IPv6" : "", p_string);
        break;
      case AF_INET6:
        cfg_append_printable_line(out, "    Parameter must be an IPv6 address%s",
            p_string);
        break;
      case AF_MAC48:
        cfg_append_printable_line(out, "    Parameter must be an MAC-48%s address%s",
            p2 == AF_EUI64 ? " or EUI64" : "", p_string);
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
    return 0;
  }

  if (netaddr_from_string(&addr, value)) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s is no valid network address",
        value, entry->t_name, section_name);
    return -1;
  }

  max_prefix = netaddr_get_maxprefix(&addr);

  /* check prefix length */
  if (addr.prefix_len > max_prefix) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s has an illegal prefix length",
        value, entry->t_name, section_name);
    return -1;
  }
  if (!prefix && addr.prefix_len != max_prefix) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s must be a single address, not a prefix",
        value, entry->t_name, section_name);
    return -1;
  }

  if (p1 == 0) {
    /* no further type check */
    return 0;
  }

  /* check address type */
  if ((p2 != 0 && p2 == addr.type) || (p1 == addr.type)) {
    return 0;
  }

  /* at least one condition was set, but no one matched */
  cfg_append_printable_line(out, "Value '%s' for entry '%s'"
      " in section '%s' is wrong address type",
      value, entry->t_name, section_name);
  return -1;
}

int
cfg_schema_tobin_strptr(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    struct cfg_stringarray *value, void *reference) {
  char **ptr;

  ptr = (char **)reference;
  if (*ptr) {
    free(ptr);
  }

  *ptr = strdup(value->last_value);
  return *ptr == NULL ? 1 : 0;
}

int
cfg_schema_tobin_strarray(const struct cfg_schema_entry *s_entry,
    struct cfg_stringarray *value, void *reference) {
  char *ptr;

  ptr = (char *)reference;

  strscpy(ptr, value->last_value, (size_t)s_entry->t_validate_params.p_i1);
  return 0;
}

int
cfg_schema_tobin_choice(const struct cfg_schema_entry *s_entry,
    struct cfg_stringarray *value, void *reference) {
  int *ptr;

  ptr = (int *)reference;

  *ptr = cfg_get_choice_index(value->last_value, s_entry->t_validate_params.p_ptr,
      (size_t)s_entry->t_validate_params.p_i1);
  return 0;
}

int
cfg_schema_tobin_int(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    struct cfg_stringarray *value, void *reference) {
  int *ptr;

  ptr = (int *)reference;

  *ptr = strtol(value->last_value, NULL, 10);
  return 0;
}

int
cfg_schema_tobin_netaddr(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    struct cfg_stringarray *value, void *reference) {
  struct netaddr *ptr;

  ptr = (struct netaddr *)reference;

  return netaddr_from_string(ptr, value->last_value);
}

int
cfg_schema_tobin_bool(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    struct cfg_stringarray *value, void *reference) {
  bool *ptr;

  ptr = (bool *)reference;

  *ptr = cfg_get_bool(value->last_value);
  return 0;
}

int
cfg_schema_tobin_stringlist(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    struct cfg_stringarray *value, void *reference) {
  struct cfg_stringarray *array;

  array = (struct cfg_stringarray *)reference;

  /* move new value into binary target */
  free (array->value);

  if (value->value == NULL || value->length == 0) {
    memset(array, 0, sizeof(*array));
    return 0;
  }
  array->value = malloc(value->length);
  if (array->value == NULL) {
    return -1;
  }

  memcpy(array->value, value->value, value->length);
  array->last_value = value->last_value - value->value + array->value;
  array->length = value->length;
  return 0;
}

static bool
_validate_cfg_entry(struct cfg_schema_section *schema_section,
    struct cfg_db *db, struct cfg_section_type *section,
    struct cfg_named_section *named, struct cfg_entry *entry,
    const char *section_name, bool cleanup, bool failFast,
    struct autobuf *out) {
  struct cfg_schema_entry *schema_entry;
  bool warning = false;
  char *ptr1 = NULL, *ptr2 = NULL;
  size_t size;

  schema_entry = cfg_schema_find_entry(schema_section, entry->name);

  if (schema_entry == NULL) {
    cfg_append_printable_line(out, "Unknown entry '%s'"
        " for section type '%s'", entry->name, section->type);
    return true;
  }

  if (!schema_entry->t_list) {
    return _check_single_value(schema_entry,
        db, section, named, entry,
        cleanup, section_name, out);
  }

  if (cleanup) {
    char *last_valid = entry->val.value;

    /* first remove duplicate entries */
    CFG_FOR_ALL_STRINGS(&entry->val, ptr1) {
      /* get pointer to next element */
      ptr2 = ptr1 + strlen(ptr1)+1;

      /* compare list value to any later value */
      while (ptr2 <= entry->val.last_value) {
        size = strlen(ptr2) + 1;

        if (strcmp(ptr2, ptr1) == 0) {
          /* duplicate found, remove it */
          size_t offset = (size_t)(ptr2 - entry->val.value);
          memmove (ptr2, ptr2 + size, entry->val.length - size - offset);
          entry->val.length -= size;
        }
        else {
          ptr2 += size;
        }
      }
      last_valid = ptr1;
    }
    entry->val.last_value = last_valid;
  }

  /* now validate syntax */
  ptr1 = entry->val.value;
  if (schema_entry->t_validate) {
    while (ptr1 < entry->val.value + entry->val.length) {
      if (schema_entry->t_validate(schema_entry, section_name, ptr1, out)) {
        /* warning is generated by the validate callback itself */
        warning = true;

        if (failFast) {
          return true;
        }
      }

      size = strlen(ptr1) + 1;

      if (warning && cleanup) {
        /* illegal entry found, remove it */
        size_t offset = (size_t)(ptr2 - entry->val.value);
        memmove(ptr1, ptr1 + size, entry->val.length - size - offset);
      }
      else {
        ptr1 += size;
      }
    }
  }

  if (entry->val.length == 0) {
    /* remove empty entry */
    cfg_db_remove_entry(db, section->type, named->name, entry->name);
  }

  return warning;
}

static bool
_check_single_value(struct cfg_schema_entry *schema_entry,
    struct cfg_db *db, struct cfg_section_type *section,
    struct cfg_named_section *named, struct cfg_entry *entry,
    bool cleanup, const char *section_name, struct autobuf *out) {
  bool warning = false;

  /* warning is generated by the validate callback itself */
  if (schema_entry->t_validate) {
    warning = schema_entry->t_validate(schema_entry, section_name, entry->val.last_value, out) != 0;
  }

  if (warning && cleanup) {
    /* remove bad entry */
    cfg_db_remove_entry(db, section->type, named->name, entry->name);
  }
  else if (cleanup && entry->val.last_value != entry->val.value) {
    /* shorten value */
    entry->val.length = strlen(entry->val.last_value) + 1;
    memmove(entry->val.value, entry->val.last_value, entry->val.length);
  }
  return warning;
}

static bool
_check_missing_entries(struct cfg_schema_section *schema_section,
    struct cfg_db *db, struct cfg_section_type *section,
    struct cfg_named_section *named,
    bool failFast, const char *section_name, struct autobuf *out) {
  struct cfg_schema_entry *schema_entry, *schema_entry_it;
  bool warning, error;

  warning = false;
  error = false;

  /* check for missing values */
  CFG_FOR_ALL_SCHEMA_ENTRIES(schema_section, schema_entry, schema_entry_it) {
    if (schema_entry->t_default != NULL) {
      continue;
    }

    /* mandatory parameter */
    warning = !cfg_db_find_entry(db, section->type, named->name, schema_entry->t_name);
    error |= warning;
    if (warning) {
      cfg_append_printable_line(out, "Missing mandatory value for entry '%s' in section %s",
          schema_entry->t_name, section_name);

      if (failFast) {
        /* stop here */
        break;
      }
    }
  }
  return error;
}
