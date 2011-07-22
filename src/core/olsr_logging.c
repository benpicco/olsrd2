
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
OLSR_SUBSYSTEM_STATE(olsr_logging_state);

/**
 * Called by main method just after configuration options have been parsed
 */
int
olsr_log_init(enum log_severity def_severity)
{
  enum log_severity j;
  enum log_source i;

  if (olsr_subsystem_init(&olsr_logging_state))
    return 0;

  list_init_head(&log_handler_list);

  if (abuf_init(&logbuffer, 4096)) {
    fputs("Not enough memory for logging buffer\n", stderr);
    olsr_logging_state--;
    return -1;
  }

  /* clear global mask */
  for (j = 0; j < LOG_SEVERITY_COUNT; j++) {
    for (i = 0; i < LOG_SOURCE_COUNT; i++) {
      log_global_mask.mask[j][i] = j >= def_severity;
    }
  }
  return 0;
}

/**
 * Called just before olsr_shutdown finishes
 */
void
olsr_log_cleanup(void)
{
  struct log_handler_entry *h, *iterator;

  if (olsr_subsystem_cleanup(&olsr_logging_state))
    return;

  /* remove all handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    olsr_log_removehandler(h);
  }

  abuf_free(&logbuffer);
}

/**
 * Registers a custom logevent handler
 * @param handler pointer to handler function
 * @param mask pointer to custom event filter or NULL if handler use filter
 *   from olsr_cnf
 */
struct log_handler_entry *
olsr_log_addhandler(log_handler_cb *handler, struct log_handler_mask *mask)
{
  struct log_handler_entry *h;

  /*
   * The logging system is used in the memory cookie manager, so the logging
   * system has to allocate its memory directly. Do not try to use
   * olsr_memcookie_malloc() here.
   */
  h = calloc(sizeof(*h), 1);
  h->handler = handler;
  h->bitmask_ptr = mask;

  list_add_tail(&log_handler_list, &h->node);
  olsr_log_updatemask();

  return h;
}

/**
 * Call this function to remove a logevent handler
 * @param handler pointer to handler function
 */
void
olsr_log_removehandler(struct log_handler_entry *h)
{
  list_remove(&h->node);
  olsr_log_updatemask();

  free(h);
}

/**
 * Recalculate the combination of the olsr_cnf log event mask and all (if any)
 * custom masks of logfile handlers. Must be called every times a event mask
 * changes.
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
 * @param file filename where the logging macro have been called
 * @param line line number where the logging macro have been called
 * @param format printf format string for log output plus a variable number of arguments
 */
void
olsr_log(enum log_severity severity, enum log_source source, bool no_header, const char *file, int line, const char *format, ...)
{
  struct log_handler_entry *h, *iterator;
  va_list ap;
  int p1 = 0, p2 = 0, p3 = 0;
  struct tm now, *tm_ptr;
  struct timeval timeval;

  /* test if event is consumed by any log handler */
  if (!log_global_mask.mask[severity][source]) {
    /* no log handler is interested in this event, so drop it */
    return;
  }

  va_start(ap, format);

  /* calculate local time */
  os_gettimeofday(&timeval, NULL);

  /* there is no localtime_r in win32 */
  tm_ptr = localtime((time_t *) & timeval.tv_sec);
  now = *tm_ptr;

  /* generate log string (insert file/line in DEBUG mode) */
  abuf_clear(&logbuffer);
  if (!no_header) {
    p1 = abuf_appendf(&logbuffer, "%d:%02d:%02d.%03ld ",
                  now.tm_hour, now.tm_min, now.tm_sec, (long)(timeval.tv_usec / 1000));

    p2 = abuf_appendf(&logbuffer, "%s(%s) %s %d: ",
        LOG_SEVERITY_NAMES[severity], LOG_SOURCE_NAMES[source], file, line);
  }
  p3 = abuf_vappendf(&logbuffer, format, ap);

  /* remove \n at the end of the line if necessary */
  if (logbuffer.buf[p1 + p2 + p3 - 1] == '\n') {
    logbuffer.buf[p1 + p2 + p3 - 1] = 0;
    p3--;
  }

  /* use stderr logger if nothing has been configured */
  if (list_is_empty(&log_handler_list)) {
    olsr_log_stderr(NULL, severity, source, no_header, file, line, logbuffer.buf, p1, p2-p1);
    return;
  }

  /* call all log handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    if (h->int_bitmask.mask[severity][source]) {
      h->handler(h, severity, source, no_header, file, line, logbuffer.buf, p1, p2-p1);
    }
  }
  va_end(ap);
}

/**
 * This function should not be called directly, use the macro OLSR_OOM_WARN
 *
 * Generates a logfile entry and calls all log handler to store/output it.
 *
 * @param source source of the log event (LOG_LOGGING, ... )
 * @param file filename where the logging macro have been called
 * @param line line number where the logging macro have been called
 * @param text Error text
 */
void
olsr_log_oom(enum log_severity severity, enum log_source source,
    const char *file, int line)
{
  static const char digits[] = "0123456789";
  static struct log_handler_entry *h, *iterator;
  static int i,j;

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
    logbuffer.buf[j-i] = digits[line % 10];
    line /= 10;
  }
  logbuffer.buf[++j] = ' ';
  logbuffer.buf[++j] = 0;

  strscat(logbuffer.buf, OUT_OF_MEMORY_ERROR, logbuffer.size);

  /* use stderr logger if nothing has been configured */
  if (list_is_empty(&log_handler_list)) {
    olsr_log_stderr(NULL, severity, source, false, file, line, logbuffer.buf, 0, 0);
    return;
  }

  /* call all log handlers */
  FOR_ALL_LOGHANDLERS(h, iterator) {
    if (h->int_bitmask.mask[severity][source]) {
      h->handler(h, severity, source, false, file, line, logbuffer.buf, 0, 0);
    }
  }
}

void
olsr_log_stderr(struct log_handler_entry *entry __attribute__ ((unused)),
    enum log_severity severity __attribute__ ((unused)),
    enum log_source source __attribute__ ((unused)),
    bool no_header __attribute__ ((unused)),
    const char *file __attribute__ ((unused)), int line __attribute__ ((unused)),
    char *buffer,
    int timeLength __attribute__ ((unused)),
    int prefixLength __attribute__ ((unused)))
{
  fputs(buffer, stderr);
  fputc('\n', stderr);
}

void
olsr_log_file(struct log_handler_entry *entry,
    enum log_severity severity __attribute__ ((unused)),
    enum log_source source __attribute__ ((unused)),
    bool no_header __attribute__ ((unused)),
    const char *file __attribute__ ((unused)), int line __attribute__ ((unused)),
    char *buffer,
    int timeLength __attribute__ ((unused)),
    int prefixLength __attribute__ ((unused)))
{
  FILE *f;

  f = entry->custom;
  fputs(buffer, f);
  fputc('\n', f);
}

void
olsr_log_syslog(struct log_handler_entry *entry __attribute__ ((unused)),
    enum log_severity severity __attribute__ ((unused)),
    enum log_source source __attribute__ ((unused)),
    bool no_header __attribute__ ((unused)),
    const char *file __attribute__ ((unused)), int line __attribute__ ((unused)),
    char *buffer, int timeLength,
    int prefixLength __attribute__ ((unused)))
{
  os_printline(severity, &buffer[timeLength]);
}
