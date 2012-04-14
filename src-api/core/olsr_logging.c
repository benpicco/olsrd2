
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
#include "os_syslog.h"
#include "olsr_logging.h"
#include "olsr.h"

#define FOR_ALL_LOGHANDLERS(handler, iterator) list_for_each_element_safe(&_handler_list, handler, node, iterator)

uint8_t log_global_mask[LOG_MAXIMUM_SOURCES];

static struct list_entity _handler_list;
static struct autobuf _logbuffer;
static const struct olsr_builddata *_builddata;
static uint8_t _default_mask;
static size_t _max_sourcetext_len, _max_severitytext_len, _source_count;

static const char OUT_OF_MEMORY_ERROR[] = "Out of memory error!";

const char *LOG_SOURCE_NAMES[LOG_MAXIMUM_SOURCES] = {
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
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_log_init(const struct olsr_builddata *data, enum log_severity def_severity)
{
  enum log_severity sev;
  enum log_source src;
  size_t len;

  if (olsr_subsystem_is_initialized(&_logging_state))
    return 0;

  _builddata = data;
  _source_count = LOG_CORESOURCE_COUNT;

  list_init_head(&_handler_list);

  if (abuf_init(&_logbuffer)) {
    fputs("Not enough memory for logging buffer\n", stderr);
    return -1;
  }

  /* initialize maximum severity length */
  _max_severitytext_len = 0;
  OLSR_FOR_ALL_LOGSEVERITIES(sev) {
    len = strlen(olsr_log_getseverityname(sev));
    if (len > _max_severitytext_len) {
      _max_severitytext_len = len;
    }
  }

  /* initialize maximum source length */
  _max_sourcetext_len = 0;
  for (src = 0; src < LOG_CORESOURCE_COUNT; src++) {
    len = strlen(LOG_SOURCE_NAMES[src]);
    if (len > _max_sourcetext_len) {
      _max_sourcetext_len = len;
    }
  }

  /* set default mask */
  _default_mask = 0;
  OLSR_FOR_ALL_LOGSEVERITIES(sev) {
    if (sev >= def_severity) {
      _default_mask |= sev;
    }
  }

  /* clear global mask */
  memset(&log_global_mask, _default_mask, sizeof(log_global_mask));

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
  enum log_source src;

  if (olsr_subsystem_cleanup(&_logging_state))
    return;

  /* remove all handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    olsr_log_removehandler(h);
  }

  for (src = LOG_CORESOURCE_COUNT; src < LOG_MAXIMUM_SOURCES; src++) {
    free ((void *)LOG_SOURCE_NAMES[src]);
    LOG_SOURCE_NAMES[src] = NULL;
  }
  abuf_free(&_logbuffer);
}

const char *
olsr_log_getseverityname(enum log_severity sev) {
  switch (sev) {
    case SEVERITY_DEBUG:
      return "DEBUG";
    case SEVERITY_INFO:
      return "INFO";
    case SEVERITY_WARN:
      return "WARN";
    default:
      return "UNKNOWN";
  }
}

/**
 * Registers a custom logevent handler. Handler and bitmask_ptr have to
 * be initialized.
 * @param h pointer to log event handler struct
 * @return -1 if an out of memory error happened, 0 otherwise
 */
void
olsr_log_addhandler(struct log_handler_entry *h)
{
  list_add_tail(&_handler_list, &h->node);
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
  olsr_log_updatemask();
}

/**
 * register a new logging source in the logger
 * @param name pointer to the name of the logging source
 * @return index of the new logging source, LOG_MAIN if out of memory
 */
int
olsr_log_register_source(const char *name) {
  size_t i, len;

  /* maybe the source is already there ? */
  for (i=0; i<_source_count; i++) {
    if (strcmp(name, LOG_SOURCE_NAMES[i]) == 0) {
      return i;
    }
  }

  if (i == LOG_MAXIMUM_SOURCES) {
    OLSR_WARN(LOG_LOGGING, "Maximum number of logging sources reached,"
        " cannot allocate %s", name);
    return LOG_MAIN;
  }

  if ((LOG_SOURCE_NAMES[i] = strdup(name)) == NULL) {
    OLSR_WARN_OOM(LOG_LOGGING);
    return LOG_MAIN;
  }

  _source_count++;
  len = strlen(name);
  if (len > _max_sourcetext_len) {
    _max_sourcetext_len = len;
  }
  return i;
}

size_t
olsr_log_get_max_severitytextlen(void) {
  return _max_severitytext_len;
}

size_t
olsr_log_get_max_sourcetextlen(void) {
  return _max_sourcetext_len;
}

size_t
olsr_log_get_sourcecount(void) {
  return _source_count;
}

/**
 * @return pointer to application builddata
 */
const struct olsr_builddata *
olsr_log_get_builddata(void) {
  return _builddata;
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
  enum log_source src;
  struct log_handler_entry *h, *iterator;

  /* first copy bitmasks to internal memory */
  /* and reset global mask */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    memcpy(h->_bitmask, h->bitmask, LOG_MAXIMUM_SOURCES);
  }
  memset(log_global_mask, 0, LOG_MAXIMUM_SOURCES);

  /* second propagate source ALL to all other sources for each logger */
  /* third, propagate events from debug to info and from info to warn */
  /* finally, calculate the global logging bitmask */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    for (src = 1; src < LOG_MAXIMUM_SOURCES; src++) {
      h->_bitmask[src] |= h->_bitmask[0];
      h->_bitmask[src] |= h->bitmask[src] << 1;
      h->_bitmask[src] |= h->bitmask[src] << 1;

      log_global_mask[src] |= h->_bitmask[src];
    }
  }
}

const char *
olsr_log_get_walltime(void) {
  static char buf[sizeof("00:00:00.000")];
  struct timeval now;
  struct tm *tm;

  if (os_system_gettimeofday(&now)) {
    return NULL;
  }

  tm = localtime(&now.tv_sec);
  if (tm == NULL) {
    return NULL;
  }
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03ld",
      tm->tm_hour, tm->tm_min, tm->tm_sec, now.tv_usec / 1000);

  return buf;
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

  va_start(ap, format);

  /* generate log string */
  abuf_clear(&_logbuffer);
  if (!no_header) {
    p1 = abuf_appendf(&_logbuffer, "%s ", olsr_log_get_walltime());
    p2 = abuf_appendf(&_logbuffer, "%s(%s) %s %d: ",
        olsr_log_getseverityname(severity), LOG_SOURCE_NAMES[source], file, line);
  }
  p3 = abuf_vappendf(&_logbuffer, format, ap);

  /* remove \n at the end of the line if necessary */
  if (abuf_getptr(&_logbuffer)[p1 + p2 + p3 - 1] == '\n') {
    abuf_getptr(&_logbuffer)[p1 + p2 + p3 - 1] = 0;
  }

  param.severity = severity;
  param.source = source;
  param.no_header = no_header;
  param.file = file;
  param.line = line;
  param.buffer = abuf_getptr(&_logbuffer);
  param.timeLength = p1;
  param.prefixLength = p2;

  /* use stderr logger if nothing has been configured */
  if (list_is_empty(&_handler_list)) {
    olsr_log_stderr(NULL, &param);
    return;
  }

  /* call all log handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    if (olsr_log_mask_test(h->_bitmask, source, severity)) {
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
  char *ptr;

  /* generate OOM log string */
  ptr = abuf_getptr(&_logbuffer);
  ptr[0] = 0;
  strscat(ptr, olsr_log_getseverityname(severity), abuf_getmax(&_logbuffer));
  strscat(ptr, " ", abuf_getmax(&_logbuffer));
  strscat(ptr, LOG_SOURCE_NAMES[source], abuf_getmax(&_logbuffer));
  strscat(ptr, " ", abuf_getmax(&_logbuffer));
  strscat(ptr, file, abuf_getmax(&_logbuffer));
  strscat(ptr, " ", abuf_getmax(&_logbuffer));

  j = strlen(ptr) + 4;

  for (i=0; i < 5; i++) {
    ptr[j-i] = '0' + (line % 10);
    line /= 10;
  }
  ptr[++j] = ' ';
  ptr[++j] = 0;

  strscat(ptr, OUT_OF_MEMORY_ERROR, abuf_getmax(&_logbuffer));

  param.severity = severity;
  param.source = source;
  param.no_header = true;
  param.file = file;
  param.line = line;
  param.buffer = ptr;
  param.timeLength = 0;
  param.prefixLength = 0;


  /* use stderr logger if nothing has been configured */
  if (list_is_empty(&_handler_list)) {
    olsr_log_stderr(NULL, &param);
    return;
  }

  /* call all log handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    if (olsr_log_mask_test(h->_bitmask, source, severity)) {
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
  os_syslog_log(param->severity, param->buffer + param->timeLength);
}
