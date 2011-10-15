
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
#include "os_time.h"
#include "os_system.h"
#include "olsr_logging.h"
#include "olsr.h"

#define FOR_ALL_LOGHANDLERS(handler, iterator) list_for_each_element_safe(&log_handler_list, handler, node, iterator)

struct log_handler_mask log_global_mask;

static struct list_entity log_handler_list;
static struct autobuf logbuffer;

const char *LOG_SEVERITY_NAMES[LOG_SEVERITY_COUNT] = {
  "DEBUG",
  "INFO",
  "WARN",
};

const char OUT_OF_MEMORY_ERROR[] = "Out of memory error!";

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_logging_state);

/**
 * Initialize logging system
 * @param def_severity default severity level
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_log_init(enum log_severity def_severity)
{
  enum log_severity j;
  enum log_source i;

  if (olsr_subsystem_is_initialized(&_logging_state))
    return 0;

  list_init_head(&log_handler_list);

  if (abuf_init(&logbuffer, 4096)) {
    fputs("Not enough memory for logging buffer\n", stderr);
    olsr_subsystem_cleanup(&_logging_state);
    return -1;
  }

  /* clear global mask */
  for (j = 0; j < LOG_SEVERITY_COUNT; j++) {
    for (i = 0; i < LOG_SOURCE_COUNT; i++) {
      log_global_mask.mask[j][i] = j >= def_severity;
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

  abuf_free(&logbuffer);
}

/**
 * Registers a custom logevent handler. Handler and bitmask_ptr have to
 * be initialized.
 * @param h pointer to log event handler struct
 */
void
olsr_log_addhandler(struct log_handler_entry *h)
{
  list_add_tail(&log_handler_list, &h->node);
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
 * Recalculate the combination of the olsr_cnf log event mask and all (if any)
 * custom masks of logfile handlers. Must be called every times a event mask
 * changes without a logevent handler being added or removed.
 */
void
olsr_log_updatemask(void)
{
  int i, j;
  struct log_handler_entry *h, *iterator;

  /* first copy bitmasks to internal memory */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    memcpy (&h->int_bitmask, h->bitmask_ptr, sizeof(h->int_bitmask));
  }

  /* second propagate source ALL to all other sources for each logger */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    for (j = 0; j < LOG_SEVERITY_COUNT; j++) {
      if (h->int_bitmask.mask[j][LOG_ALL]) {
        for (i = 0; i < LOG_SOURCE_COUNT; i++) {
          h->int_bitmask.mask[j][i] = true;
        }
      }
    }
  }

  /* third, propagate events from debug to info to warn to error */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    for (j = 0; j < LOG_SOURCE_COUNT; j++) {
      bool active = false;

      for (i = 0; i < LOG_SEVERITY_COUNT; i++) {
        active |= h->int_bitmask.mask[i][j];
        h->int_bitmask.mask[i][j] = active;
      }
    }
  }

  /* finally calculate the global logging bitmask */
  for (j = 0; j < LOG_SEVERITY_COUNT; j++) {
    for (i = 0; i < LOG_SOURCE_COUNT; i++) {
      log_global_mask.mask[j][i] = false;

      FOR_ALL_LOGHANDLERS(h, iterator) {
        log_global_mask.mask[j][i] |= h->int_bitmask.mask[j][i];
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
  if (!log_global_mask.mask[severity][source]) {
    /* no log handler is interested in this event, so drop it */
    return;
  }

  va_start(ap, format);

  /* generate log string */
  abuf_clear(&logbuffer);
  if (!no_header) {
    struct tm *tm_ptr;
    struct timeval timeval;

    /* calculate local time */
    if (os_system_gettimeofday(&timeval)) {
      p1 = abuf_appendf(&logbuffer, "gettimeofday-error ");
    }
    else {
      /* there is no localtime_r in win32 */
      tm_ptr = localtime((time_t *) & timeval.tv_sec);

      p1 = abuf_appendf(&logbuffer, "%d:%02d:%02d.%03ld ",
          tm_ptr->tm_hour, tm_ptr->tm_min, tm_ptr->tm_sec,
          (long)(timeval.tv_usec / 1000));
    }

    p2 = abuf_appendf(&logbuffer, "%s(%s) %s %d: ",
          LOG_SEVERITY_NAMES[severity], LOG_SOURCE_NAMES[source], file, line);
  }
  p3 = abuf_vappendf(&logbuffer, format, ap);

  /* remove \n at the end of the line if necessary */
  if (logbuffer.buf[p1 + p2 + p3 - 1] == '\n') {
    logbuffer.buf[p1 + p2 + p3 - 1] = 0;
    p3--;
  }

  param.severity = severity;
  param.source = source;
  param.no_header = no_header;
  param.file = file;
  param.line = line;
  param.buffer = logbuffer.buf;
  param.timeLength = p1;
  param.prefixLength = p2;

  /* use stderr logger if nothing has been configured */
  if (list_is_empty(&log_handler_list)) {
    olsr_log_stderr(NULL, &param);
    return;
  }

  /* call all log handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    if (h->int_bitmask.mask[severity][source]) {
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
  if (!log_global_mask.mask[severity][source])
    return;

  /* generate OOM log string */
  logbuffer.buf[0] = 0;
  strscat(logbuffer.buf, LOG_SEVERITY_NAMES[severity], logbuffer.size);
  strscat(logbuffer.buf, " ", logbuffer.size);
  strscat(logbuffer.buf, LOG_SOURCE_NAMES[source], logbuffer.size);
  strscat(logbuffer.buf, " ", logbuffer.size);
  strscat(logbuffer.buf, file, logbuffer.size);
  strscat(logbuffer.buf, " ", logbuffer.size);

  j = strlen(logbuffer.buf) + 4;

  for (i=0; i < 5; i++) {
    logbuffer.buf[j-i] = '0' + (line % 10);
    line /= 10;
  }
  logbuffer.buf[++j] = ' ';
  logbuffer.buf[++j] = 0;

  strscat(logbuffer.buf, OUT_OF_MEMORY_ERROR, logbuffer.size);

  param.severity = severity;
  param.source = source;
  param.no_header = true;
  param.file = file;
  param.line = line;
  param.buffer = logbuffer.buf;
  param.timeLength = 0;
  param.prefixLength = 0;


  /* use stderr logger if nothing has been configured */
  if (list_is_empty(&log_handler_list)) {
    olsr_log_stderr(NULL, &param);
    return;
  }

  /* call all log handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    if (h->int_bitmask.mask[severity][source]) {
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
