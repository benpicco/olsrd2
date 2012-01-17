
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/common_types.h"
#include "config/cfg_schema.h"
#include "config/cfg_db.h"
#include "config/cfg_delta.h"
#include "config/cfg.h"

#include "olsr_logging.h"
#include "olsr_logging_cfg.h"
#include "olsr_cfg.h"
#include "olsr.h"
#include "os_system.h"

#define LOG_SECTION     "log"
#define LOG_LEVEL_ENTRY "level"
#define LOG_DEBUG_ENTRY "debug"
#define LOG_INFO_ENTRY  "info"
#define LOG_WARN_ENTRY  "warn"
#define LOG_STDERR_ENTRY "stderr"
#define LOG_SYSLOG_ENTRY "syslog"
#define LOG_FILE_ENTRY   "file"

/* prototype for configuration change handler */
static void _cb_logcfg_apply(void);
static void _apply_log_setting(struct cfg_named_section *named,
    const char *entry_name, enum log_severity severity);

/* define logging configuration template */
static struct cfg_schema_section logging_section = {
  .type = LOG_SECTION
};

static const char *_dummy[0];
static struct cfg_schema_entry logging_entries[] = {
  /* the next three parameters are configured to a different list during runtime */
  CFG_VALIDATE_CHOICE(LOG_DEBUG_ENTRY, "",
      "Set logging sources that display debug, info and warnings",
      _dummy, .list = true),
      CFG_VALIDATE_CHOICE(LOG_INFO_ENTRY, "",
      "Set logging sources that display info and warnings",
      _dummy, .list = true),
      CFG_VALIDATE_CHOICE(LOG_WARN_ENTRY, "",
      "Set logging sources that display warnings",
      _dummy, .list = true),

  CFG_VALIDATE_INT_MINMAX(LOG_LEVEL_ENTRY, "0", "Set debug level template", -2, 3),
  CFG_VALIDATE_BOOL(LOG_STDERR_ENTRY, "false", "Set to true to activate logging to stderr"),
  CFG_VALIDATE_BOOL(LOG_SYSLOG_ENTRY, "false", "Set to true to activate logging to syslog"),
  CFG_VALIDATE_STRING(LOG_FILE_ENTRY, "", "Set a filename to log to a file"),
};

static struct cfg_delta_handler logcfg_delta_handler = {
  .s_type = LOG_SECTION,
  .callback = _cb_logcfg_apply,
};

static enum log_source *debug_lvl_1 = NULL;
static size_t debug_lvl_1_count = 0;

/* global logger configuration */
static struct log_handler_mask_entry *logging_cfg;
static struct log_handler_entry stderr_handler = {
  .handler = olsr_log_stderr
};
static struct log_handler_entry syslog_handler = {
  .handler = olsr_log_syslog
};
static struct log_handler_entry file_handler = {
  .handler = olsr_log_file
};

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_logcfg_state);

/**
 * Initialize logging configuration
 * @param debug_lvl_1_ptr array of logging sources for debug level 1
 * @param length number of level 1 logging sources
 */
void
olsr_logcfg_init(enum log_source *debug_lvl_1_ptr, size_t length) {
  if (olsr_subsystem_init(&_logcfg_state))
    return;

  debug_lvl_1 = debug_lvl_1_ptr;
  debug_lvl_1_count = length;

  logging_cfg = olsr_log_allocate_mask();
  stderr_handler.bitmask = logging_cfg;
  syslog_handler.bitmask = logging_cfg;
  file_handler.bitmask = logging_cfg;

  /* setup delta handler */
  cfg_delta_add_handler(olsr_cfg_get_delta(), &logcfg_delta_handler);
}

/**
 * Cleanup all allocated resources of logging configuration
 */
void
olsr_logcfg_cleanup(void) {
  if (olsr_subsystem_cleanup(&_logcfg_state))
    return;

  /* cleanup delta handler */
  cfg_delta_remove_handler(olsr_cfg_get_delta(), &logcfg_delta_handler);

  /* clean up former handlers */
  if (list_is_node_added(&stderr_handler.node)) {
    olsr_log_removehandler(&stderr_handler);
  }
  if (list_is_node_added(&syslog_handler.node)) {
    olsr_log_removehandler(&syslog_handler);
  }
  if (list_is_node_added(&file_handler.node)) {
    FILE *f;

    f = file_handler.custom;
    fflush(f);
    fclose(f);

    olsr_log_removehandler(&file_handler);
  }

  olsr_log_free_mask(logging_cfg);
}

/**
 * Add logging section to global configuration schema
 * @param schema pointer to schema
 */
void
olsr_logcfg_addschema(struct cfg_schema *schema) {
  int i;

  for (i=0; i<3; i++) {
    logging_entries[0].validate_params.p_i1 = olsr_log_get_sourcecount();
    logging_entries[0].validate_params.p_ptr = LOG_SOURCE_NAMES;
  }

  cfg_schema_add_section(schema, &logging_section);
  cfg_schema_add_entries(&logging_section, logging_entries, ARRAYSIZE(logging_entries));
}

/**
 * Apply the configuration settings of a db to the logging system
 * @param db pointer to configuration database
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_logcfg_apply(struct cfg_db *db) {
  struct cfg_named_section *named;
  const char *ptr, *file_name;
  size_t i;
  int file_errno = 0;
  bool activate_syslog, activate_file, activate_stderr;

  /* clean up logging mask */
  olsr_log_clear_mask(logging_cfg);

  /* first apply debug level */
  ptr = cfg_db_get_entry_value(db, LOG_SECTION, NULL, LOG_LEVEL_ENTRY)->value;
  switch (atoi(ptr)) {
    case -1:
      /* no logging */
      break;
    case 0:
      /* only warnings */
      logging_cfg[LOG_ALL].log_for_severity[SEVERITY_WARN] = true;
      break;
    case 1:
      /* warnings and some info */
      logging_cfg[LOG_ALL].log_for_severity[SEVERITY_WARN] = true;
      for (i=0; i<debug_lvl_1_count; i++) {
        logging_cfg[debug_lvl_1[i]].log_for_severity[SEVERITY_INFO] = true;
      }
      break;
    case 2:
      /* warning and info */
      logging_cfg[LOG_ALL].log_for_severity[SEVERITY_INFO] = true;
      break;
    case 3:
      /* all logging messages */
      logging_cfg[LOG_ALL].log_for_severity[SEVERITY_DEBUG] = true;
      break;
    default:
      break;
  }

  /* now apply specific settings */
  named = cfg_db_find_namedsection(db, LOG_SECTION, NULL);
  if (named != NULL) {
    _apply_log_setting(named, LOG_WARN_ENTRY, SEVERITY_WARN);
    _apply_log_setting(named, LOG_INFO_ENTRY, SEVERITY_INFO);
    _apply_log_setting(named, LOG_DEBUG_ENTRY, SEVERITY_DEBUG);
  }

  /* load settings which loggershould be activated */
  ptr = cfg_db_get_entry_value(db, LOG_SECTION, NULL, LOG_SYSLOG_ENTRY)->value;
  activate_syslog = cfg_get_bool(ptr);

  file_name = cfg_db_get_entry_value(db, LOG_SECTION, NULL, LOG_FILE_ENTRY)->value;
  activate_file = file_name != NULL && *file_name != 0;

  ptr = cfg_db_get_entry_value(db, LOG_SECTION, NULL, LOG_STDERR_ENTRY)->value;
  activate_stderr = cfg_get_bool(ptr);

  /* and finally modify the logging handlers */
  /* log.file */
  if (activate_file && !list_is_node_added(&file_handler.node)) {
    FILE *f;

    f = fopen(file_name, "w");
    if (f != NULL) {
      olsr_log_addhandler(&file_handler);
      file_handler.custom = f;
    }
    else {
      file_errno = errno;
      activate_file = false;
    }
  }
  else if (!activate_file && list_is_node_added(&file_handler.node)) {
    FILE *f = file_handler.custom;
    olsr_log_removehandler(&file_handler);

    fflush(f);
    fclose(f);
  }

  /* log.stderr (activate if syslog and file ar offline) */
  if (!config_global.fork) {
    activate_stderr |= !(activate_syslog || activate_file);
  }

  if (activate_stderr && !list_is_node_added(&stderr_handler.node)) {
    olsr_log_addhandler(&stderr_handler);
  }
  else if (!activate_stderr && list_is_node_added(&stderr_handler.node)) {
    olsr_log_removehandler(&stderr_handler);
  }

  /* log.syslog */
  if (config_global.fork) {
    activate_syslog |= !(activate_stderr || activate_file);
  }

  if (activate_syslog && !list_is_node_added(&syslog_handler.node)) {
    olsr_log_addhandler(&syslog_handler);
  }
  else if (!activate_syslog && list_is_node_added(&syslog_handler.node)) {
    olsr_log_removehandler(&syslog_handler);
  }

  /* reload logging mask */
  olsr_log_updatemask();

  if (file_errno) {
    OLSR_WARN(LOG_MAIN, "Cannot open file '%s' for logging: %s (%d)",
        file_name, strerror(file_errno), file_errno);
    return 1;
  }

  return 0;
}

/**
 * Apply the logging options of one severity setting to the logging mask
 * @param named pointer to configuration section
 * @param entry_name name of setting (debug, info, warn)
 * @param severity severity level corresponding severity level
 */
static void
_apply_log_setting(struct cfg_named_section *named,
    const char *entry_name, enum log_severity severity) {
  struct cfg_entry *entry;
  char *ptr;
  size_t i;

  entry = cfg_db_get_entry(named, entry_name);
  if (entry) {
    FOR_ALL_STRINGS(&entry->val, ptr) {
      i = cfg_get_choice_index(ptr, LOG_SOURCE_NAMES, ARRAYSIZE(LOG_SOURCE_NAMES));
      logging_cfg[i].log_for_severity[severity] = true;
    }
  }
}

/**
 * Wrapper for configuration delta handling
 */
static void
_cb_logcfg_apply(void) {
  if (olsr_logcfg_apply(olsr_cfg_get_db())) {
    os_system_log(SEVERITY_WARN, "Could not open logging file");
  }
}
