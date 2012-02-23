
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common/autobuf.h"
#include "common/list.h"
#include "common/string.h"
#include "builddata/data.h"
#include "os_system.h"
#include "olsr_logging.h"
#include "olsr.h"

#define FOR_ALL_LOGHANDLERS(handler, iterator) list_for_each_element_safe(&_handler_list, handler, node, iterator)

struct log_handler_mask_entry *log_global_mask;
const char **LOG_SOURCE_NAMES;
static size_t _total_source_count;

static struct list_entity _handler_list;
static struct autobuf _logbuffer;
static const struct olsr_builddata *_builddata;

const char *LOG_SEVERITY_NAMES[LOG_SEVERITY_COUNT] = {
  "DEBUG",
  "INFO",
  "WARN",
};

const char OUT_OF_MEMORY_ERROR[] = "Out of memory error!";

static const char *_LOG_SOURCE_NAMES[LOG_CORESOURCE_COUNT] = {
  "all",
  "logging",
  "config",
  "main",
  "socket",
  "timer",
  "memcookie",
  "socket-stream",
  "socket-packet",
  "interface",
  "os-net",
  "os-system",
  "os-routing",
  "plugin-loader",
  "telnet",
  "plugins",
  "http",
};

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_logging_state);

/**
 * Initialize logging system
 * @param data builddata defined by application
 * @param def_severity default severity level
 * @param lognames array of string pointers with logging labels
 * @param level_count number of custom logging levels
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_log_init(const struct olsr_builddata *data, enum log_severity def_severity,
    const char **lognames, size_t level_count)
{
  enum log_severity sev;
  enum log_source src;

  if (olsr_subsystem_is_initialized(&_logging_state))
    return 0;

  _builddata = data;

  _total_source_count = LOG_CORESOURCE_COUNT + level_count;
  log_global_mask = olsr_log_allocate_mask();

  /* concat the core name list and the custom one of the user */
  LOG_SOURCE_NAMES = calloc(_total_source_count, sizeof(char *));
  memcpy(LOG_SOURCE_NAMES, _LOG_SOURCE_NAMES,
      LOG_CORESOURCE_COUNT * sizeof (char *));
  if (lognames) {
    memcpy(LOG_SOURCE_NAMES + LOG_CORESOURCE_COUNT, lognames,
        level_count * sizeof(char *));
  }

  list_init_head(&_handler_list);

  if (abuf_init(&_logbuffer)) {
    fputs("Not enough memory for logging buffer\n", stderr);
    return -1;
  }

  /* clear global mask */
  for (sev = def_severity; sev < LOG_SEVERITY_COUNT; sev++) {
    for (src = 0; src < _total_source_count; src++) {
      log_global_mask[src].log_for_severity[sev] = true;
    }
  }

  olsr_subsystem_init(&_logging_state);
  return 0;
}

/**
 * Cleanup all resources allocated by logging system
 */
void
olsr_log_cleanup(void)
{
  struct log_handler_entry *h, *iterator;

  if (olsr_subsystem_cleanup(&_logging_state))
    return;

  /* remove all handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    olsr_log_removehandler(h);
  }

  olsr_log_free_mask (log_global_mask);
  free (LOG_SOURCE_NAMES);
  abuf_free(&_logbuffer);
}

/**
 * Registers a custom logevent handler. Handler and bitmask_ptr have to
 * be initialized.
 * @param h pointer to log event handler struct
 */
void
olsr_log_addhandler(struct log_handler_entry *h)
{
  list_add_tail(&_handler_list, &h->node);
  h->int_bitmask = olsr_log_allocate_mask();
  olsr_log_updatemask();
}

/**
 * Unregister a logevent handler
 * @param h pointer to handler entry
 */
void
olsr_log_removehandler(struct log_handler_entry *h)
{
  list_remove(&h->node);
  olsr_log_free_mask(h->int_bitmask);
  olsr_log_updatemask();
}

/**
 * @return pointer to application builddata
 */
const struct olsr_builddata *
olsr_log_get_builddata(void) {
  return _builddata;
}

/**
 * @return total number of logging sources
 */
enum log_source
olsr_log_get_sourcecount(void) {
  return _total_source_count;
}

/**
 * Print version string
 * @param abuf target output buffer
 */
void
olsr_log_printversion(struct autobuf *abuf) {
  abuf_appendf(abuf," %s version %s (%s)\n"
            " Built on %s\n"
            " Git: %s\n"
            "      %s\n"
            "%s",
            _builddata->app_name, _builddata->version,
            _builddata->builddate, _builddata->buildsystem,
            _builddata->git_commit, _builddata->git_change,
            _builddata->versionstring_trailer);
}

/**
 * Recalculate the combination of the olsr_cnf log event mask and all (if any)
 * custom masks of logfile handlers. Must be called every times a event mask
 * changes without a logevent handler being added or removed.
 */
void
olsr_log_updatemask(void)
{
  enum log_severity sev;
  enum log_source src;
  struct log_handler_entry *h, *iterator;

  /* first copy bitmasks to internal memory */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    olsr_log_copy_mask(h->int_bitmask, h->bitmask);
  }

  /* second propagate source ALL to all other sources for each logger */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    for (sev = 0; sev < LOG_SEVERITY_COUNT; sev++) {
      if (h->int_bitmask[LOG_ALL].log_for_severity[sev]) {
        for (src = 0; src < _total_source_count; src++) {
          h->int_bitmask[src].log_for_severity[sev] = true;
        }
      }
    }
  }

  /* third, propagate events from debug to info to warn to error */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    for (src = 0; src < _total_source_count; src++) {
      bool active = false;

      for (sev = 0; sev < LOG_SEVERITY_COUNT; sev++) {
        active |= h->int_bitmask[src].log_for_severity[sev];
        h->int_bitmask[src].log_for_severity[sev] = active;
      }
    }
  }

  /* finally calculate the global logging bitmask */
  for (sev = 0; sev < LOG_SEVERITY_COUNT; sev++) {
    for (src = 0; src < _total_source_count; src++) {
      log_global_mask[src].log_for_severity[sev] = false;

      FOR_ALL_LOGHANDLERS(h, iterator) {
        log_global_mask[src].log_for_severity[sev]
             |= h->int_bitmask[src].log_for_severity[sev];
      }
    }
  }
}

/**
 * This function should not be called directly, use the macros OLSR_{DEBUG,INFO,WARN} !
 *
 * Generates a logfile entry and calls all log handler to store/output it.
 *
 * @param severity severity of the log event (SEVERITY_DEBUG to SEVERITY_WARN)
 * @param source source of the log event (LOG_LOGGING, ... )
 * @param no_header true if time header should not be created
 * @param file filename where the logging macro have been called
 * @param line line number where the logging macro have been called
 * @param format printf format string for log output plus a variable number of arguments
 */
void
olsr_log(enum log_severity severity, enum log_source source, bool no_header,
    const char *file, int line, const char *format, ...)
{
  struct log_handler_entry *h, *iterator;
  struct log_parameters param;
  va_list ap;
  int p1 = 0, p2 = 0, p3 = 0;

  /* test if event is consumed by any log handler */
  if (!log_global_mask[source].log_for_severity[severity]) {
    /* no log handler is interested in this event, so drop it */
    return;
  }

  va_start(ap, format);

  /* generate log string */
  abuf_clear(&_logbuffer);
  if (!no_header) {
    struct tm *tm_ptr;
    struct timeval timeval;

    /* calculate local time */
    if (os_system_gettimeofday(&timeval)) {
      p1 = abuf_appendf(&_logbuffer, "gettimeofday-error ");
    }
    else {
      /* there is no localtime_r in win32 */
      tm_ptr = localtime((time_t *) & timeval.tv_sec);

      p1 = abuf_appendf(&_logbuffer, "%d:%02d:%02d.%03ld ",
          tm_ptr->tm_hour, tm_ptr->tm_min, tm_ptr->tm_sec,
          (long)(timeval.tv_usec / 1000));
    }

    p2 = abuf_appendf(&_logbuffer, "%s(%s) %s %d: ",
          LOG_SEVERITY_NAMES[severity], LOG_SOURCE_NAMES[source], file, line);
  }
  p3 = abuf_vappendf(&_logbuffer, format, ap);

  /* remove \n at the end of the line if necessary */
  if (_logbuffer.buf[p1 + p2 + p3 - 1] == '\n') {
    _logbuffer.buf[p1 + p2 + p3 - 1] = 0;
    p3--;
  }

  param.severity = severity;
  param.source = source;
  param.no_header = no_header;
  param.file = file;
  param.line = line;
  param.buffer = _logbuffer.buf;
  param.timeLength = p1;
  param.prefixLength = p2;

  /* use stderr logger if nothing has been configured */
  if (list_is_empty(&_handler_list)) {
    olsr_log_stderr(NULL, &param);
    return;
  }

  /* call all log handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    if (h->int_bitmask[source].log_for_severity[severity]) {
      h->handler(h, &param);
    }
  }
  va_end(ap);
}

/**
 * This function should not be called directly, use the macro OLSR_OOM_WARN
 *
 * Generates a logfile entry and calls all log handler to store/output it.
 *
 * @param severity severity of the logging event
 * @param source source of the log event (LOG_LOGGING, ... )
 * @param file filename where the logging macro have been called
 * @param line line number where the logging macro have been called
 */
void
olsr_log_oom(enum log_severity severity, enum log_source source,
    const char *file, int line)
{
  struct log_handler_entry *h, *iterator;
  struct log_parameters param;
  int i,j;

  /* test if event is consumed by any log handler */
  if (!log_global_mask[source].log_for_severity[severity]) {
    /* no log handler is interested in this event, so drop it */
    return;
  }

  /* generate OOM log string */
  _logbuffer.buf[0] = 0;
  strscat(_logbuffer.buf, LOG_SEVERITY_NAMES[severity], _logbuffer.size);
  strscat(_logbuffer.buf, " ", _logbuffer.size);
  strscat(_logbuffer.buf, LOG_SOURCE_NAMES[source], _logbuffer.size);
  strscat(_logbuffer.buf, " ", _logbuffer.size);
  strscat(_logbuffer.buf, file, _logbuffer.size);
  strscat(_logbuffer.buf, " ", _logbuffer.size);

  j = strlen(_logbuffer.buf) + 4;

  for (i=0; i < 5; i++) {
    _logbuffer.buf[j-i] = '0' + (line % 10);
    line /= 10;
  }
  _logbuffer.buf[++j] = ' ';
  _logbuffer.buf[++j] = 0;

  strscat(_logbuffer.buf, OUT_OF_MEMORY_ERROR, _logbuffer.size);

  param.severity = severity;
  param.source = source;
  param.no_header = true;
  param.file = file;
  param.line = line;
  param.buffer = _logbuffer.buf;
  param.timeLength = 0;
  param.prefixLength = 0;


  /* use stderr logger if nothing has been configured */
  if (list_is_empty(&_handler_list)) {
    olsr_log_stderr(NULL, &param);
    return;
  }

  /* call all log handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    if (h->int_bitmask[source].log_for_severity[severity]) {
      h->handler(h, &param);
    }
  }
}

/**
 * Logger for stderr output
 * @param entry logging handler, might be NULL because this is the
 *   default logger
 * @param param logging parameter set
 */
void
olsr_log_stderr(struct log_handler_entry *entry __attribute__ ((unused)),
    struct log_parameters *param)
{
  fputs(param->buffer, stderr);
  fputc('\n', stderr);
}

/**
 * Logger for file output
 * @param entry logging handler
 * @param param logging parameter set
 */
void
olsr_log_file(struct log_handler_entry *entry,
    struct log_parameters *param)
{
  FILE *f;

  f = entry->custom;
  fputs(param->buffer, f);
  fputc('\n', f);
}

/**
 * Logger for syslog output
 * @param entry logging handler, might be NULL
 * @param param logging parameter set
 */
void
olsr_log_syslog(struct log_handler_entry *entry __attribute__ ((unused)),
    struct log_parameters *param)
{
  os_system_log(param->severity, param->buffer + param->timeLength);
}
