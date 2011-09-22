
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

#ifndef CFG_SCHEMA_H_
#define CFG_SCHEMA_H_

struct cfg_schema;
struct cfg_schema_section;
struct cfg_schema_entry;

#ifndef OS_WIN32
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "common/autobuf.h"
#include "common/avl.h"
#include "common/netaddr.h"

#include "config/cfg_db.h"
#include "config/cfg_stringarray.h"

#if !defined(REMOVE_HELPTEXT)
#define _CFG_VALIDATE(name, def, help, args...)                  { .t_name = (name), .t_default = (def), .t_help = (help), ##args }
#else
#define _CFG_VALIDATE(name, def, help, args...)                  { .t_name = (name), .t_default = (def), ##args }
#endif

#define CFG_VALIDATE_STRING(name, def, help, args...)                  _CFG_VALIDATE(name, def, help, ##args)
#define CFG_VALIDATE_STRING_LEN(name, def, help, maxlen, args...)      _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_strlen, .t_validate_params = {.p_i1 = (maxlen) }, ##args )
#define CFG_VALIDATE_PRINTABLE(name, def, help, args...)               _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_printable, .t_validate_params = {.p_i1 = INT32_MAX }, ##args )
#define CFG_VALIDATE_PRINTABLE_LEN(name, def, help, maxlen, args...)   _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_printable, .t_validate_params = {.p_i1 = (maxlen) }, ##args )
#define CFG_VALIDATE_CHOICE(name, def, help, list, args...)            _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_choice, .t_validate_params = {.p_ptr = (list), .p_i1 = ARRAYSIZE(list)}, ##args )
#define CFG_VALIDATE_INT(name, def, help, args...)                     _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_int, .t_validate_params = {.p_i1 = INT32_MIN, .p_i2 = INT32_MAX}, ##args )
#define CFG_VALIDATE_INT_MINMAX(name, def, help, min, max, args...)    _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_int, .t_validate_params = {.p_i1 = (min), .p_i2 = (max)}, ##args )
#define CFG_VALIDATE_NETADDR(name, def, help, prefix, args...)         _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_netaddr, .t_validate_params = {.p_i1 = 0, .p_i2 = !!(prefix) ? -1 : 0 }, ##args )
#define CFG_VALIDATE_NETADDR_HWADDR(name, def, help, prefix, args...)  _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_netaddr, .t_validate_params = {.p_i1 = !!(prefix) ? -AF_MAC48 : AF_MAC48, .p_i2 = AF_EUI64 }, ##args )
#define CFG_VALIDATE_NETADDR_MAC48(name, def, help, prefix, args...)   _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_netaddr, .t_validate_params = {.p_i1 = !!(prefix) ? -AF_MAC48 : AF_MAC48}, ##args )
#define CFG_VALIDATE_NETADDR_EUI64(name, def, help, prefix, args...)   _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_netaddr, .t_validate_params = {.p_i1 = !!(prefix) ? -AF_EUI64 : AF_EUI64}, ##args )
#define CFG_VALIDATE_NETADDR_V4(name, def, help, prefix, args...)      _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_netaddr, .t_validate_params = {.p_i1 = !!(prefix) ? -AF_INET : AF_INET }, ##args )
#define CFG_VALIDATE_NETADDR_V6(name, def, help, prefix, args...)      _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_netaddr, .t_validate_params = {.p_i1 = !!(prefix) ? -AF_INET6 : AF_INET6 }, ##args )
#define CFG_VALIDATE_NETADDR_V46(name, def, help, prefix, args...)     _CFG_VALIDATE(name, def, help, .t_validate = cfg_schema_validate_netaddr, .t_validate_params = {.p_i1 = !!(prefix) ? -AF_INET : AF_INET, .p_i2 = AF_INET6}, ##args )

#define CFG_VALIDATE_BOOL(name, def, help, args...)                    CFG_VALIDATE_CHOICE(name, def, help, CFGLIST_BOOL, ##args)

#define CFG_MAP_STRING(reference, name, def, help, args...)                  _CFG_VALIDATE(#name, def, help, .t_to_binary = cfg_schema_tobin_strptr, .t_offset = offsetof(struct reference, name), ##args )
#define CFG_MAP_STRING_LEN(reference, name, def, help, maxlen, args...)      CFG_VALIDATE_STRING_LEN(#name, def, help, maxlen, .t_to_binary = cfg_schema_tobin_strptr, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_STRING_ARRAY(reference, name, def, help, maxlen, args...)    _CFG_VALIDATE(#name, def, help, .t_validate_params = {.p_i1 = (maxlen) }, .t_to_binary = cfg_schema_tobin_strarray, .t_offset = offsetof(struct reference, name), ##args )
#define CFG_MAP_PRINTABLE(reference, name, def, help, args...)               CFG_VALIDATE_PRINTABLE(#name, def, help, .t_to_binary = cfg_schema_tobin_strptr, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_PRINTABLE_LEN(reference, name, def, help, args...)           CFG_VALIDATE_PRINTABLE_LEN(#name, def, help, maxlen, .t_to_binary = cfg_schema_tobin_strptr, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_PRINTABLE_ARRAY(reference, name, def, help, maxlen, args...) CFG_VALIDATE_PRINTABLE_LEN(#name, def, help, maxlen, .t_to_binary = cfg_schema_tobin_strarray, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_CHOICE(reference, name, def, help, list, args...)            CFG_VALIDATE_CHOICE(#name, def, help, list, .t_to_binary = cfg_schema_tobin_choice, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_INT(reference, name, def, help, args...)                     CFG_VALIDATE_INT(#name, def, help, .t_to_binary = cfg_schema_tobin_int, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_INT_MINMAX(reference, name, def, help, min, max, args...)    CFG_VALIDATE_INT_MINMAX(#name, def, help, min, max, .t_to_binary = cfg_schema_tobin_int, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_NETADDR(reference, name, def, help, prefix, args...)         CFG_VALIDATE_NETADDR(#name, def, help, prefix, .t_to_binary = cfg_schema_tobin_netaddr, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_NETADDR_HWADDR(reference, name, def, help, prefix, args...)  CFG_VALIDATE_NETADDR_HWADDR(#name, def, help, prefix, .t_to_binary = cfg_schema_tobin_netaddr, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_NETADDR_MAC48(reference, name, def, help, prefix, args...)   CFG_VALIDATE_NETADDR_MAC48(#name, def, help, prefix, .t_to_binary = cfg_schema_tobin_netaddr, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_NETADDR_EUI64(reference, name, def, help, prefix, args...)   CFG_VALIDATE_NETADDR_EUI64(#name, def, help, prefix, .t_to_binary = cfg_schema_tobin_netaddr, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_NETADDR_V4(reference, name, def, help, prefix, args...)      CFG_VALIDATE_NETADDR_V4(#name, def, help, prefix, .t_to_binary = cfg_schema_tobin_netaddr, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_NETADDR_V6(reference, name, def, help, prefix, args...)      CFG_VALIDATE_NETADDR_V6(#name, def, help, prefix, .t_to_binary = cfg_schema_tobin_netaddr, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_NETADDR_V46(reference, name, def, help, prefix, args...)     CFG_VALIDATE_NETADDR_V46(#name, def, help, prefix, .t_to_binary = cfg_schema_tobin_netaddr, .t_offset = offsetof(struct reference, name), ##args)

#define CFG_MAP_BOOL(reference, name, def, help, args...)                    CFG_VALIDATE_BOOL(#name, def, help, .t_to_binary = cfg_schema_tobin_bool, .t_offset = offsetof(struct reference, name), ##args)
#define CFG_MAP_STRINGLIST(reference, name, def, help, args...)              _CFG_VALIDATE(#name, def, help, .t_to_binary = cfg_schema_tobin_stringlist, .t_offset = offsetof(struct reference, name), .t_list = true, ##args )

/*
 * Example of a section schema definition
 *
 * struct cfg_schema_section test_section = {
 *   .t_type = "plugin", .t_named = true
 * };
 *
 * struct cfg_schema_entry test_entries[] = {
 *   CFG_VALIDATE_BOOL("enable", "true"),
 *   CFG_VALIDATE_PRINTABLE("logfile", "/tmp/test", .t_list = true),
 *   CFG_VALIDATE_INT_MINMAX("value", "0", 0, 20),
 * };
 */

/*
 * Example of a section schema definition with binary mapping
 *
 * struct bin_data {
 *   bool enable;
 *   char *logfile;
 *   int value;
 * };
 *
 * struct cfg_schema_section test_section = {
 *   .t_type = "plugin", .t_named = true
 * };
 *
 * struct cfg_schema_entry test_entries[] = {
 *   CFG_MAP_BOOL(bin_data, enable, "true"),
 *   CFG_MAP_PRINTABLE(bin_data, logfile, "/tmp/test", .t_list = true),
 *   CFG_MAP_INT_MINMAX(bin_data, value, "0", 0, 20),
 * };
 */

struct cfg_schema {
  /* tree of sections of this schema */
  struct avl_tree sections;
};

/*
 * Represents the schema of all named sections within
 * a certain type
 */
struct cfg_schema_section {
  /* node for tree in schema, initialized by schema_add() */
  struct avl_node node;

  /* name of section type */
  const char *t_type;

  /* true if sections of this  schema have a name */
  bool t_named;

  /* true if at least one section of this type must exist */
  bool t_mandatory;

  /*
   * true if delta listeners should NOT be triggered for
   * this section on a cfg_delta_trigger_non_optional() call.
   */
  bool t_optional;

  /* help text for section */
  const char *t_help;

  /* callback for checking configuration of section */
  int (*t_validate)(struct cfg_schema_section *, const char *section_name,
      struct cfg_named_section *, struct autobuf *);

  /* list of entries in section */
  struct avl_tree entries;
};

/* Represents the schema of a configuration entry */
struct cfg_schema_entry {
  /* node for tree in section schema */
  struct avl_node node;

  /* name of entry */
  const char *t_name;

  /* default value */
  const char *t_default;

  /* help text for entry */
  const char *t_help;

  /* value is a list of parameters instead of a single one */
  bool t_list;

  /*
   * set to true to mark entry as stateful, so it must be
   * acknowledged before being written over by configuration
   * changes
   */
  bool t_confirm;

  /* callback for checking value of entry */
  int (*t_validate)(const struct cfg_schema_entry *entry,
      const char *section_name, const char *value, struct autobuf *out);

  /* parameters for check functions */
  struct validate_params {
      int p_i1, p_i2;
      void *p_ptr;
  } t_validate_params;

  /* callback for converting string into binary */
  int (*t_to_binary)(const struct cfg_schema_entry *s_entry,
      struct cfg_stringarray *value, void *ptr);

  /* offset of current binary data compared to reference pointer */
  size_t t_offset;
};

EXPORT const char *CFGLIST_BOOL_TRUE[4];
EXPORT const char *CFGLIST_BOOL[8];

#define CFG_FOR_ALL_SCHEMA_SECTIONS(tmpl, section, iterator) avl_for_each_element_safe(&(tmpl->sections), section, node, iterator)
#define CFG_FOR_ALL_SCHEMA_ENTRIES(section, entry, iterator) avl_for_each_element_safe(&section->entries, entry, node, iterator)

EXPORT void cfg_schema_add(struct cfg_schema *schema);
EXPORT void cfg_schema_remove(struct cfg_schema *schema);

EXPORT int cfg_schema_add_section(struct cfg_schema *schema, struct cfg_schema_section *section);
EXPORT void cfg_schema_remove_section(struct cfg_schema *schema, struct cfg_schema_section *section);

EXPORT int cfg_schema_add_entries(struct cfg_schema_section *section, struct cfg_schema_entry *entries, size_t e_cnt);
EXPORT int cfg_schema_add_entry(struct cfg_schema_section *, struct cfg_schema_entry *entry);
EXPORT void cfg_schema_remove_entries(struct cfg_schema_section *, struct cfg_schema_entry *entries, size_t e_cnt);

EXPORT int cfg_schema_validate(struct cfg_db *db,
    bool failFast, bool cleanup, bool ignore_unknown_sections, struct autobuf *out);

EXPORT int cfg_schema_tobin(void *target, struct cfg_named_section *named,
    const struct cfg_schema_entry *entries, size_t count);

EXPORT int cfg_schema_validate_printable(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out);
EXPORT int cfg_schema_validate_strlen(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out);
EXPORT int cfg_schema_validate_choice(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out);
EXPORT int cfg_schema_validate_int(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out);
EXPORT int cfg_schema_validate_netaddr(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out);

EXPORT int cfg_schema_tobin_strptr(const struct cfg_schema_entry *s_entry,
    struct cfg_stringarray *value, void *reference);
EXPORT int cfg_schema_tobin_strarray(const struct cfg_schema_entry *s_entry,
    struct cfg_stringarray *value, void *reference);
EXPORT int cfg_schema_tobin_choice(const struct cfg_schema_entry *s_entry,
    struct cfg_stringarray *value, void *reference);
EXPORT int cfg_schema_tobin_int(const struct cfg_schema_entry *s_entry,
    struct cfg_stringarray *value, void *reference);
EXPORT int cfg_schema_tobin_netaddr(const struct cfg_schema_entry *s_entry,
    struct cfg_stringarray *value, void *reference);
EXPORT int cfg_schema_tobin_bool(const struct cfg_schema_entry *s_entry,
    struct cfg_stringarray *value, void *reference);
EXPORT int cfg_schema_tobin_stringlist(const struct cfg_schema_entry *s_entry,
    struct cfg_stringarray *value, void *reference);

/**
 * Finds a section in a schema
 * @param schema pointer to schema
 * @param type type of section
 * @return pointer to section, NULL if not found
 */
static INLINE struct cfg_schema_section *
cfg_schema_find_section(struct cfg_schema *schema, const char *type) {
  struct cfg_schema_section *section;

  return avl_find_element(&schema->sections, type, section, node);
}

/**
 * Remove a single entry from a schema section
 * @param section pointer to section
 * @param entry pointer to entry
 */
static INLINE void
cfg_schema_remove_entry(struct cfg_schema_section *section, struct cfg_schema_entry *entry) {
  avl_remove(&section->entries, &entry->node);
  entry->node.key = NULL;
}

/**
 * Finds an entry in a schema section
 * @param section pointer to section
 * @param name name of entry
 * @return pointer of entry, NULL if not found
 */
static INLINE struct cfg_schema_entry *
cfg_schema_find_entry(struct cfg_schema_section *section, const char *name) {
  struct cfg_schema_entry *entry;

  return avl_find_element(&section->entries, name, entry, node);
}

#endif /* CFG_SCHEMA_H_ */
