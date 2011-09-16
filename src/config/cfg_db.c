
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
#include <stdlib.h>
#include <string.h>

#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/string.h"

#include "config/cfg.h"
#include "config/cfg_schema.h"
#include "config/cfg_memory.h"
#include "config/cfg_db.h"

static struct cfg_section_type *_alloc_section(struct cfg_db *, const char *);
static void _free_sectiontype(struct cfg_section_type *);

static struct cfg_named_section *_alloc_namedsection(
    struct cfg_section_type *, const char *);
static void _free_namedsection(struct cfg_named_section *);

static struct cfg_entry *_alloc_entry(
    struct cfg_named_section *, const char *);
static void _free_entry(struct cfg_entry *);

/**
 * @return new configuration database without entries,
 *   NULL if no memory left
 */
struct cfg_db *
cfg_db_add(void) {
  struct cfg_db *db;

  db = calloc(1, sizeof(*db));
  if (db) {
    avl_init(&db->sectiontypes, cfg_avlcmp_keys, false, NULL);
    cfg_memory_add(&db->memory);
  }
  return db;
}

/**
 * Removes a configuration database including all data from memory
 * @param db pointer to configuration database
 */
void
cfg_db_remove(struct cfg_db *db) {
  struct cfg_section_type *section, *section_it;

  CFG_FOR_ALL_SECTION_TYPES(db, section, section_it) {
    _free_sectiontype(section);
  }

  cfg_memory_remove(&db->memory);
  free(db);
}

/**
 * Copy parts of a db into a new db
 * @param dst pointer to target db
 * @param src
 * @param section_type
 * @param section_name
 * @param entry_name
 */
void
_cfg_db_append(struct cfg_db *dst, struct cfg_db *src,
    const char *section_type, const char *section_name, const char *entry_name) {
  struct cfg_section_type *section, *section_it;
  struct cfg_named_section *named, *named_it;
  struct cfg_entry *entry, *entry_it;
  char *ptr;

  CFG_FOR_ALL_SECTION_TYPES(src, section, section_it) {
    if (section_type != NULL && cfg_cmp_keys(section->type, section_type) != 0) {
      continue;
    }

    CFG_FOR_ALL_SECTION_NAMES(section, named, named_it) {
      if (section_name != NULL && cfg_cmp_keys(named->name, section_name) != 0) {
        continue;
      }

      _cfg_db_add_section(dst, section->type, named->name);

      CFG_FOR_ALL_ENTRIES(named, entry, entry_it) {
        if (entry_name != NULL && cfg_cmp_keys(entry->name, entry_name) != 0) {
          continue;
        }

        CFG_FOR_ALL_STRINGS(&entry->val, ptr) {
          cfg_db_set_entry(dst, section->type, named->name, entry->name, ptr, true);
        }
      }
    }
  }
}

/**
 * Adds a named section to a configuration database
 * @param db pointer to configuration database
 * @param section_type type of section
 * @param section_name name of section, NULL if an unnamed one
 * @return pointer to named section, NULL if an error happened
 */
struct cfg_named_section *
_cfg_db_add_section(struct cfg_db *db, const char *section_type,
    const char *section_name) {
  struct cfg_section_type *section;
  struct cfg_named_section *named;

  /* consistency check */
  assert (section_type);

  /* get section */
  section = avl_find_element(&db->sectiontypes, section_type, section, node);
  if (section == NULL) {
    section = _alloc_section(db, section_type);
  }

  /* get named section */
  named = avl_find_element(&section->names, section_name, named, node);
  if (named == NULL) {
    named = _alloc_namedsection(section, section_name);
  }

  return named;
}

/**
 * Removes a section type (including all namedsections and entries of it)
 * from a configuration database
 * @param db pointer to configuration database
 * @param section_type type of section
 * @return -1 if section type did not exist, 0 otherwise
 */
int
cfg_db_remove_sectiontype(struct cfg_db *db, const char *section_type) {
  struct cfg_section_type *section;

  /* find section */
  section = cfg_db_find_sectiontype(db, section_type);
  if (section == NULL) {
    return -1;
  }

  _free_sectiontype(section);
  return 0;
}

/**
 * Finds a named section object inside a configuration database
 * @param db pointer to configuration database
 * @param section_type type of section
 * @param section_name name of section, NULL if an unnamed section
 * @return pointer to named section, NULL if not found
 */
struct cfg_named_section *
cfg_db_find_namedsection(
    struct cfg_db *db, const char *section_type, const char *section_name) {
  struct cfg_section_type *section;
  struct cfg_named_section *named = NULL;

  section = cfg_db_find_sectiontype(db, section_type);
  if (section != NULL) {
    named = avl_find_element(&section->names, section_name, named, node);
  }
  return named;
}

/**
 * Removes a section type (including all entries of it)
 * from a configuration database.
 * If the section_type below is empty afterwards, you might
 * want to delete it too.
 * @param db pointer to configuration database
 * @param section_type type of section
 * @param section_name name of section, NULL if an unnamed section
 * @return -1 if section type did not exist, 0 otherwise
 */
int
cfg_db_remove_namedsection(struct cfg_db *db, const char *section_type,
    const char *section_name) {
  struct cfg_named_section *named;

  named = cfg_db_find_namedsection(db, section_type, section_name);
  if (named == NULL) {
    return -1;
  }

  /* only free named section */
  _free_namedsection(named);
  return 0;
}

/**
 * Changes an entry to a configuration database
 * @param db pointer to configuration database
 * @param section_type type of section
 * @param section_name name of section, NULL if an unnamed one
 * @param entry_name entry name
 * @param value entry value
 * @param append true if the value should be appended to a list,
 *   false if it should overwrite all old values
 * @return pointer to cfg_entry, NULL if an error happened
 */
struct cfg_entry *
cfg_db_set_entry(struct cfg_db *db, const char *section_type,
    const char *section_name, const char *entry_name, const char *value,
    bool append) {
  struct cfg_entry *entry;
  struct cfg_named_section *named;
  char *old = NULL;
  size_t old_size = 0, new_size = 0;

  new_size = strlen(value) + 1;

  /* create section */
  named = _cfg_db_add_section(db, section_type, section_name);

  /* get entry */
  entry = avl_find_element(&named->entries, entry_name, entry, node);
  if (entry == NULL) {
    entry = _alloc_entry(named, entry_name);
  }

  /* copy old values */
  old = entry->val.value;
  if (entry->val.value != NULL && append) {
    old_size = entry->val.length;
  }

  entry->val.length = old_size + new_size;
  entry->val.value = cfg_memory_alloc_string(&db->memory, entry->val.length);

  if (old_size) {
    memcpy(entry->val.value, old, old_size);
  }
  memcpy(entry->val.value + old_size, value, new_size);
  entry->val.last_value = entry->val.value + old_size;

  cfg_memory_free_string(&db->memory, old);
  return entry;
}

/**
 * Finds a specific entry inside a configuration database
 * @param db pointer to configuration database
 * @param section_type type of section
 * @param section_name name of section, NULL if an unnamed section
 * @param entry_name name of the entry
 * @return pointer to configuration entry, NULL if not found
 */
struct cfg_entry *
cfg_db_find_entry(struct cfg_db *db,
    const char *section_type, const char *section_name, const char *entry_name) {
  struct cfg_named_section *named;
  struct cfg_entry *entry = NULL;

  /* get named section */
  named = cfg_db_find_namedsection(db, section_type, section_name);
  if (named != NULL) {
    entry = avl_find_element(&named->entries, entry_name, entry, node);
  }
  return entry;
}

/**
 * Removes an entry from a configuration database
 * @param db pointer to configuration database
 * @param section_type type of section
 * @param section_name name of section, NULL if an unnamed one
 * @param entry_name entry name
 * @return -1 if the entry did not exist, 0 if it was removed
 */
int
cfg_db_remove_entry(struct cfg_db *db, const char *section_type,
    const char *section_name, const char *entry_name) {
  struct cfg_entry *entry;

  /* get entry */
  entry = cfg_db_find_entry(db, section_type, section_name, entry_name);
  if (entry == NULL) {
    /* entry not there */
    return -1;
  }

  _free_entry(entry);
  return 0;
}

/**
 * Accessor function to read the string value of a single entry
 * from a configuration database.
 * @param db pointer to configuration database
 * @param section_type type of section
 * @param section_name name of section, NULL if an unnamed section
 * @param entry_name name of the entry
 * @return string value, NULL if not found or list of values
 */
const char *
cfg_db_get_entry_value(struct cfg_db *db, const char *section_type,
    const char *section_name, const char *entry_name) {
  struct cfg_entry *entry;
  struct cfg_schema_section *s_section;
  struct cfg_schema_entry *s_entry;

  entry = cfg_db_find_entry(db, section_type, section_name, entry_name);
  if (entry != NULL) {
    return entry->val.last_value;
  }

  if (db->schema == NULL) {
    return NULL;
  }

  /* look for default value */
  s_section = cfg_schema_find_section(db->schema, section_type);
  if (s_section == NULL) {
    return NULL;
  }

  s_entry = cfg_schema_find_entry(s_section, entry_name);
  if (s_entry) {
    return s_entry->t_default;
  }
  return NULL;
}

/**
 * Removes an element from a configuration entry list
 * @param db pointer to configuration database
 * @param section_type type of section
 * @param section_name name of section, NULL if an unnamed one
 * @param entry_name entry name
 * @param value value to be removed from the list
 * @return 0 if the element was removes, -1 if it wasn't there
 */
int
cfg_db_remove_element(struct cfg_db *db, const char *section_type,
    const char *section_name, const char *entry_name, const char *value) {
  struct cfg_entry *entry;
  char *ptr, *last_ptr;

  /* find entry */
  entry = cfg_db_find_entry(db, section_type, section_name, entry_name);
  if (entry == NULL) {
    return -1;
  }

  if (!cfg_db_is_multipart_entry(entry)) {
    /* only a single element in list */
    if (strcmp(value, entry->val.value) == 0) {
      _free_entry(entry);
      return 0;
    }
    return -1;
  }

  last_ptr = NULL;
  CFG_FOR_ALL_STRINGS(&entry->val, ptr) {
    if (strcmp(ptr, value) == 0) {
      size_t value_len = strlen(value) + 1;

      entry->val.length -= value_len;

      if (entry->val.last_value != ptr) {
        /* not the last element */
        size_t offset = (size_t)(ptr - entry->val.value);
        memmove(ptr, ptr + value_len, entry->val.length - offset);
        entry->val.last_value -= value_len;
      }
      else {
        /* last element */
        entry->val.last_value = last_ptr;
      }
      return 0;
    }
    last_ptr = ptr;
  }

  /* element not in list */
  return -1;
}


/**
 * Counts the number of list items of an entry
 * @param entry pointer to entry
 * @return number of items in the entries value
 */
size_t
cfg_db_entry_get_listsize(struct cfg_entry *entry) {
  char *ptr;
  size_t cnt = 1;

  for (ptr = entry->val.value; ptr < entry->val.last_value; ptr++) {
    if (*ptr == 0) {
      cnt++;
    }
  }
  return cnt;
}

/**
 * Creates a section type in a configuration database
 * @param db pointer to configuration database
 * @param type type of section
 * @return pointer to section type
 */
static struct cfg_section_type *
_alloc_section(struct cfg_db *db, const char *type) {
  struct cfg_section_type *section;

  assert(type);

  section = cfg_memory_alloc(&db->memory, sizeof(*section));
  section->type = cfg_memory_strdup(&db->memory, type);

  section->node.key = section->type;
  avl_insert(&db->sectiontypes, &section->node);

  section->db = db;

  avl_init(&section->names, cfg_avlcmp_keys, false, NULL);
  return section;
}

/**
 * Removes a section type from a configuration database
 * including its named section and entries.
 * @param section pointer to section
 */
static void
_free_sectiontype(struct cfg_section_type *section) {
  struct cfg_named_section *named, *named_it;

  /* remove all named sections */
  CFG_FOR_ALL_SECTION_NAMES(section, named, named_it) {
    _free_namedsection(named);
  }

  avl_remove(&section->db->sectiontypes, &section->node);
  cfg_memory_free_string(&section->db->memory, section->type);
  cfg_memory_free(&section->db->memory, section, sizeof(*section));
}

/**
 * Creates a named section in a configuration database.
 * @param section pointer to section type
 * @param name name of section (may not be NULL)
 * @return pointer to named section
 */
static struct cfg_named_section *
_alloc_namedsection(struct cfg_section_type *section,
    const char *name) {
  struct cfg_named_section *named;

  named = cfg_memory_alloc(&section->db->memory, sizeof(*section));
  named->name = cfg_memory_strdup(&section->db->memory, name);

  named->node.key = named->name;
  avl_insert(&section->names, &named->node);

  named->section_type = section;
  avl_init(&named->entries, cfg_avlcmp_keys, false, NULL);
  return named;
}

/**
 * Removes a named section from a database including entries.
 * @param named pointer to named section.
 */
static void
_free_namedsection(struct cfg_named_section *named) {
  struct cfg_entry *entry, *entry_it;

  /* remove all entries first */
  CFG_FOR_ALL_ENTRIES(named, entry, entry_it) {
    _free_entry(entry);
  }

  avl_remove(&named->section_type->names, &named->node);
  cfg_memory_free_string(&named->section_type->db->memory, named->name);
  cfg_memory_free(&named->section_type->db->memory, named, sizeof(*named));
}

/**
 * Creates an entry in a configuration database.
 * It will not initialize the value.
 * @param named pointer to named section
 * @param name name of entry
 * @return pointer to configuration entry
 */
static struct cfg_entry *
_alloc_entry(struct cfg_named_section *named,
    const char *name) {
  struct cfg_entry *entry;

  entry = cfg_memory_alloc(&named->section_type->db->memory, sizeof(*entry));

  entry->name = cfg_memory_strdup(&named->section_type->db->memory, name);
  entry->node.key = entry->name;
  avl_insert(&named->entries, &entry->node);

  entry->named_section = named;
  return entry;
}

/**
 * Removes a configuration entry from a database
 * @param entry pointer to configuration entry
 */
static void
_free_entry(struct cfg_entry *entry) {
  struct cfg_db *db;
  avl_remove(&entry->named_section->entries, &entry->node);

  db = entry->named_section->section_type->db;
  cfg_memory_free_string(&db->memory, entry->name);
  cfg_memory_free_string(&db->memory, entry->val.value);
  cfg_memory_free(&db->memory, entry, sizeof(*entry));
}
