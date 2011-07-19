
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

#include "olsr_logging.h"
#include "olsr_logging_cfg.h"
#include "olsr.h"

#define LOG_SECTION     "log"
#define LOG_LEVEL_ENTRY "level"
#define LOG_DEBUG_ENTRY "debug"
#define LOG_INFO_ENTRY  "info"
#define LOG_WARN_ENTRY  "warn"
#define LOG_STDERR_ENTRY "stderr"
#define LOG_SYSLOG_ENTRY "syslog"
#define LOG_FILE_ENTRY   "file"

/* define logging configuration template */
static struct cfg_schema_section logging_section = {
  .t_type = LOG_SECTION
};

static struct cfg_schema_entry logging_entries[] = {
  CFG_VALIDATE_INT_MINMAX(LOG_LEVEL_ENTRY, "0", -2, 3),
  CFG_VALIDATE_CHOICE(LOG_DEBUG_ENTRY, "", LOG_SOURCE_NAMES, .t_list = true),
  CFG_VALIDATE_CHOICE(LOG_INFO_ENTRY, "", LOG_SOURCE_NAMES, .t_list = true),
  CFG_VALIDATE_CHOICE(LOG_WARN_ENTRY, "", LOG_SOURCE_NAMES, .t_list = true),
  CFG_VALIDATE_BOOL(LOG_STDERR_ENTRY, "false"),
  CFG_VALIDATE_BOOL(LOG_SYSLOG_ENTRY, "false"),
  CFG_VALIDATE_STRING(LOG_FILE_ENTRY, ""),
};

static enum log_source *debug_lvl_1 = NULL;
static size_t debug_lvl_1_count = 0;

/* global logger configuration */
static struct log_handler_mask logging_cfg;
static struct log_handler_entry *stderr_handler = NULL;
static struct log_handler_entry *syslog_handler = NULL;
static struct log_handler_entry *file_handler = NULL;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_logcfg_refcount);

void
olsr_logcfg_init(enum log_source *debug_lvl_1_ptr, size_t count) {
  if (olsr_subsystem_init(&olsr_logcfg_refcount))
    return;

  debug_lvl_1 = debug_lvl_1_ptr;
  debug_lvl_1_count = count;

  stderr_handler = NULL;
  syslog_handler = NULL;
  file_handler = NULL;

  memset(&logging_cfg, 0, sizeof(logging_cfg));
}

void
olsr_logcfg_cleanup(void) {
  if (olsr_subsystem_cleanup(&olsr_logcfg_refcount))
    return;

  /* clean up former handlers */
  if (stderr_handler) {
    olsr_log_removehandler(stderr_handler);
  }
  if (syslog_handler) {
    olsr_log_removehandler(syslog_handler);
  }
  if (file_handler) {
    FILE *f;

    f = file_handler->custom;
    fflush(f);
    fclose(f);

    olsr_log_removehandler(file_handler);
  }
}

void
olsr_logcfg_addschema(struct cfg_schema *schema) {
  cfg_schema_add_section(schema, &logging_section);
  cfg_schema_add_entries(&logging_section, logging_entries, ARRAYSIZE(logging_entries));
}

static void
apply_log_setting(struct cfg_named_section *named,
    const char *entry_name, enum log_severity severity) {
  struct cfg_entry *entry;
  char *ptr;
  size_t i;

  entry = cfg_db_get_entry(named, entry_name);
  if (entry) {
    OLSR_FOR_ALL_CFG_LIST_ENTRIES(entry, ptr) {
      i = cfg_get_choice_index(ptr, LOG_SOURCE_NAMES, ARRAYSIZE(LOG_SOURCE_NAMES));
      logging_cfg.mask[severity][i] = true;
    }
  }
}

int
olsr_logcfg_apply(struct cfg_db *db) {
  struct cfg_named_section *named;
  const char *ptr, *file_name;
  size_t i;
  int file_errno = 0;

  /* remove active handlers */
  olsr_logcfg_cleanup();

  /* clean up logging mask */
  memset(&logging_cfg, 0, sizeof(logging_cfg));

  /* first apply debug level */
  ptr = cfg_db_get_entry_value(db, LOG_SECTION, NULL, LOG_LEVEL_ENTRY);
  switch (atoi(ptr)) {
    case -1:
      /* no logging */
      break;
    case 0:
      /* only warnings */
      logging_cfg.mask[SEVERITY_WARN][LOG_ALL] = true;
      break;
    case 1:
      /* warnings and some info */
      logging_cfg.mask[SEVERITY_WARN][LOG_ALL] = true;
      for (i=0; i<debug_lvl_1_count; i++) {
        logging_cfg.mask[SEVERITY_INFO][debug_lvl_1[i]] = true;
      }
      break;
    case 2:
      /* warning and info */
      logging_cfg.mask[SEVERITY_INFO][LOG_ALL] = true;
      break;
    case 3:
      /* all logging messages */
      logging_cfg.mask[SEVERITY_DEBUG][LOG_ALL] = true;
      break;
    default:
      break;
  }

  /* now apply specific settings */
  named = cfg_db_find_namedsection(db, LOG_SECTION, NULL);
  if (named != NULL) {
    apply_log_setting(named, LOG_WARN_ENTRY, SEVERITY_WARN);
    apply_log_setting(named, LOG_INFO_ENTRY, SEVERITY_INFO);
    apply_log_setting(named, LOG_DEBUG_ENTRY, SEVERITY_DEBUG);
  }

  /* and finally re-add logging handlers */

  /* log.syslog */
  ptr = cfg_db_get_entry_value(db, LOG_SECTION, NULL, LOG_SYSLOG_ENTRY);
  if (cfg_get_bool(ptr)) {
    syslog_handler = olsr_log_addhandler(olsr_log_syslog, &logging_cfg);
  }

  /* log.file */
  file_name = cfg_db_get_entry_value(db, LOG_SECTION, NULL, LOG_FILE_ENTRY);
  if (file_name != NULL && *file_name != 0) {
    FILE *f;

    f = fopen(file_name, "w");
    if (f != NULL) {
      file_handler = olsr_log_addhandler(olsr_log_file, &logging_cfg);
      file_handler->custom = f;
    }
    else {
      file_errno = errno;
    }
  }

  /* log.stderr (activate if syslog and file ar offline) */
  ptr = cfg_db_get_entry_value(db, LOG_SECTION, NULL, LOG_STDERR_ENTRY);
  if (cfg_get_bool(ptr) || (syslog_handler == NULL && file_handler == NULL)) {
    stderr_handler = olsr_log_addhandler(olsr_log_stderr, &logging_cfg);
  }

  if (file_errno) {
    OLSR_WARN(LOG_MAIN, "Cannot open file '%s' for logging: %s (%d)",
        file_name, strerror(file_errno), file_errno);
    return 1;
  }
  return 0;
}
