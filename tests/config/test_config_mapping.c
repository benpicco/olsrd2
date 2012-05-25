
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2012, the olsr.org team - see HISTORY file
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

#include "config/cfg_db.h"
#include "config/cfg_schema.h"

#include "common/autobuf.h"
#include "common/netaddr.h"
#include "common/string.h"
#include "config/cfg_db.h"
#include "config/cfg_schema.h"

#include "../cunit.h"

#define CFG_SEC "sec"
#define CFG_SECNAME "secname"

const char *choices[] = {
  "choice1", "choice2", "choice3"
};

static struct cfg_db *db = NULL;
static struct autobuf out;

struct bin_data {
  char *string;
  char string_array[10];
  int choice;
  int integer;
  struct netaddr address;
  bool boolean;
};

struct bin_data2 {
  int choice;
  bool boolean;
};

static struct cfg_schema schema;

static struct cfg_schema_section section = {
  .type = CFG_SEC, .mode = CFG_SSMODE_NAMED
};

static struct cfg_schema_entry entries[] = {
  CFG_MAP_STRING(bin_data, string, "string", "a string", "help string"),
  CFG_MAP_STRING_ARRAY(bin_data, string_array, "string_array", "test", "help string array", 5),
  CFG_MAP_CHOICE(bin_data, choice, "choice", "choice1", "help choice", choices),
  CFG_MAP_INT(bin_data, integer, "integer", "3", "help int"),
  CFG_MAP_NETADDR(bin_data, address, "address", "10.0.0.1", "help ip", false, false),
  CFG_MAP_BOOL(bin_data, boolean, "boolean", "0", "help bool")
};

static struct cfg_schema_section section2 = {
  .type = CFG_SEC, .mode = CFG_SSMODE_NAMED
};

static struct cfg_schema_entry entries2[] = {
  CFG_MAP_CHOICE(bin_data2, choice, "choice", "choice1", "help choice", choices),
  CFG_MAP_BOOL(bin_data2, boolean, "boolean", "0", "help bool")
};

const char IP_10_coloncolon_1[16] = {
    0x00,0x10, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0x01
};

static void
clear_elements(void) {
  if (db) {
    cfg_db_remove(db);
  }

  db = cfg_db_add();
  cfg_db_add_entry(db, CFG_SEC, CFG_SECNAME, "string", "abc");
  cfg_db_add_entry(db, CFG_SEC, CFG_SECNAME, "string_array", "pm");
  cfg_db_add_entry(db, CFG_SEC, CFG_SECNAME, "choice", "choice2");
  cfg_db_add_entry(db, CFG_SEC, CFG_SECNAME, "integer", "42");
  cfg_db_add_entry(db, CFG_SEC, CFG_SECNAME, "address", "10::1");
  cfg_db_add_entry(db, CFG_SEC, CFG_SECNAME, "boolean", "true");

  abuf_clear(&out);
}

static void
test_binary_mapping(void) {
  int result;
  struct bin_data data;
  struct cfg_named_section *named;

  START_TEST();

  memset(&data, 0, sizeof(data));

  named = cfg_db_find_namedsection(db, CFG_SEC, CFG_SECNAME);
  CHECK_TRUE(named != NULL, "Could not find named section");
  if (named) {
    result = cfg_schema_tobin(&data, named, entries, ARRAYSIZE(entries));
    CHECK_TRUE(0 == result, "Conversion failed");

    if (result == 0) {
      CHECK_TRUE(data.string, "String pointer is NULL");
      if (data.string) {
        CHECK_TRUE(strcmp(data.string, "abc") == 0, "String is not 'abc' but '%s'",
            data.string);
      }

      CHECK_TRUE(strcmp(data.string_array, "pm") == 0,
          "String-Array is not 'pm' but '%s'", data.string_array);
      CHECK_TRUE(data.choice == 1, "Choice is not '1' but '%d'", data.choice);
      CHECK_TRUE(data.integer == 42, "Integer is not '42' but '%d'", data.integer);
      CHECK_TRUE(memcmp(data.address.addr, IP_10_coloncolon_1, 16) == 0,
          "Netaddr Address part is not consistent");
      CHECK_TRUE(data.address.prefix_len == 128,
          "Netaddr Prefixlen is not 128 but %d", data.address.prefix_len);
      CHECK_TRUE(data.address.type == AF_INET6,
          "Netaddr Addresstype is not IPv6");
      CHECK_TRUE(data.boolean, "Boolean was false");
    }
  }

  free(data.string);
  END_TEST();
}

static void
test_dual_binary_mapping(void) {
  int result;
  struct bin_data data;
  struct bin_data2 data2;

  struct cfg_named_section *named;

  START_TEST();

  memset(&data, 0, sizeof(data));
  memset(&data2, 0, sizeof(data2));

  named = cfg_db_find_namedsection(db, CFG_SEC, CFG_SECNAME);
  CHECK_TRUE(named != NULL, "Could not find named section");
  if (named) {
    result = cfg_schema_tobin(&data, named, entries, ARRAYSIZE(entries));
    CHECK_TRUE(0 == result, "Conversion failed");

    if (result == 0) {
      CHECK_TRUE(data.string, "String pointer is NULL");
      if (data.string) {
        CHECK_TRUE(strcmp(data.string, "abc") == 0, "String is not 'abc' but '%s'",
            data.string);
      }

      CHECK_TRUE(strcmp(data.string_array, "pm") == 0,
          "String-Array is not 'pm' but '%s'", data.string_array);
      CHECK_TRUE(data.choice == 1, "Choice is not '1' but '%d'", data.choice);
      CHECK_TRUE(data.integer == 42, "Integer is not '42' but '%d'", data.integer);
      CHECK_TRUE(memcmp(data.address.addr, IP_10_coloncolon_1, 16) == 0,
          "Netaddr Address part is not consistent");
      CHECK_TRUE(data.address.prefix_len == 128,
          "Netaddr Prefixlen is not 128 but %d", data.address.prefix_len);
      CHECK_TRUE(data.address.type == AF_INET6,
          "Netaddr Addresstype is not IPv6");
      CHECK_TRUE(data.boolean, "Boolean was false");
    }

    result = cfg_schema_tobin(&data2, named, entries2, ARRAYSIZE(entries2));
    CHECK_TRUE(0 == result, "Conversion 2 failed");

    if (result == 0) {
      CHECK_TRUE(data2.choice == 1, "Choice is not '1' but '%d'", data2.choice);
      CHECK_TRUE(data2.boolean, "Boolean was false");
    }
  }

  free(data.string);
  END_TEST();
}

int
main(int argc __attribute__ ((unused)), char **argv __attribute__ ((unused))) {
  cfg_schema_add(&schema);
  cfg_schema_add_section(&schema, &section, entries, ARRAYSIZE(entries));
  cfg_schema_add_section(&schema, &section2, entries2, ARRAYSIZE(entries2));

  abuf_init(&out);
  BEGIN_TESTING();

  test_binary_mapping();
  test_dual_binary_mapping();

  FINISH_TESTING();

  abuf_free(&out);
  if (db) {
    cfg_db_remove(db);
  }
  return total_fail;
}
