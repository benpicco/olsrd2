
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

#include <stdio.h>
#include <stdlib.h>

#include "common/common_types.h"
#include "config/cfg_schema.h"
#include "config/cfg_delta.h"
#include "config/io/cfg_io_file.h"
#include "config/parser/cfg_parser_compact.h"

#include "olsr_logging.h"
#include "olsr_plugins.h"
#include "olsr_cfg.h"
#include "olsr.h"

static struct cfg_db *olsr_db = NULL;
static struct cfg_schema olsr_schema;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_cfg_refcount);

/* define global configuration template */
static struct cfg_schema_section global_section = {
  .t_type = CFG_SECTION_GLOBAL,
};

static struct cfg_schema_entry global_entries[] = {
  CFG_VALIDATE_BOOL(CFG_GLOBAL_FORK, "no"),
  CFG_VALIDATE_STRING(CFG_GLOBAL_PLUGIN, "", .t_list = true),
};

/**
 * Initializes the olsrd configuration subsystem
 */
int
olsr_cfg_init(void) {
  if (olsr_subsystem_init(&olsr_cfg_refcount))
    return 0;

  /* initialize schema */
  cfg_schema_add(&olsr_schema);
  cfg_schema_add_section(&olsr_schema, &global_section);
  cfg_schema_add_entries(&global_section, global_entries, ARRAYSIZE(global_entries));

  /* initialize database */
  if ((olsr_db = cfg_db_add()) == NULL) {
    OLSR_WARN(LOG_CONFIG, "Cannot create configuration database for OLSR.");
    olsr_cfg_refcount--;
    return -1;
  }

  cfg_db_link_schema(olsr_db, &olsr_schema);

  /* initialize config format handler */
  cfg_io_add(&cfg_io_file);
  // cfg_parser_add(&cfg_parser_compact);
  return 0;
}

/**
 * Cleans up all data allocated by the olsrd configuration subsystem
 */
void
olsr_cfg_cleanup(void) {
  if (olsr_subsystem_cleanup(&olsr_cfg_refcount))
    return;

  cfg_db_remove(olsr_db);
  cfg_schema_remove(&olsr_schema);
}

/**
 * Converts the content of the configuration database into the internal
 * storage structs
 * @return 0 if successful, false otherwise
 */
int
olsr_cfg_apply(void) {
  struct cfg_named_section *named;
  struct cfg_entry *entry;
  char *ptr;

  /* read global section */
  named = cfg_db_find_namedsection(olsr_db, CFG_SECTION_GLOBAL, NULL);

  /* load plugins */
  entry = cfg_db_get_entry(named, CFG_GLOBAL_PLUGIN);
  if (entry) {
    OLSR_FOR_ALL_CFG_LIST_ENTRIES(entry, ptr) {
      olsr_plugins_load(ptr);
    }
  }

  /*
  if (cfg_schema_tobin(&olsr_config, named, global_entries, ARRAYSIZE(global_entries))) {
    OLSR_ERROR(LOG_MAIN, "Error during conversion of global section into internal struct\n");
    return 1;
  }
  */

  return 0;
}

/**
 * @return pointer to olsr configuration database
 */
struct cfg_db *
olsr_cfg_get_db(void) {
  return olsr_db;
}

/**
 * @return pointer to olsr configuration schema
 */
struct cfg_schema *
olsr_cfg_get_schema(void) {
  return &olsr_schema;
}
