
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

#ifndef OLSR_LOGGING_H_
#define OLSR_LOGGING_H_

struct log_handler_entry;

#include "common/common_types.h"
#include "common/list.h"

#include "olsr_logging_sources.h"

/**
 * defines the severity of a logging event
 */
enum log_severity {
  SEVERITY_DEBUG,                      //!< SEVERITY_DEBUG
  SEVERITY_INFO,                       //!< SEVERITY_INFO
  SEVERITY_WARN,                       //!< SEVERITY_WARN

  /* this one must be the last of the enums ! */
  LOG_SEVERITY_COUNT                   //!< LOG_SEVERITY_COUNT
};

struct log_parameters {
  enum log_severity severity;
  enum log_source source;
  bool no_header;
  const char *file;
  int line;
  char *buffer;
  int timeLength;
  int prefixLength;
};

extern const char *LOG_SEVERITY_NAMES[LOG_SEVERITY_COUNT];

/**
 * these four macros should be used to generate OLSR logging output
 *
 * OLSR_DEBUG should be used for all output that is only usefull for debugging a specific
 * part of the code. This could be information about the internal progress of a function,
 * state of variables, ...
 *
 * OLSR_INFO should be used for all output that does not inform the user about a
 * problem/error in OLSR. Examples would be "SPF run triggered" or "Hello package received
 * from XXX.XXX.XXX.XXX".
 *
 * OLSR_WARN should be used for all error messages.
 *
 * OLSR_WARN_OOM should be called in an out-of-memory event to display some warning
 * without allocating more memory.
 */
#ifdef REMOVE_LOG_DEBUG
#define OLSR_DEBUG(source, format, args...) do { } while(0)
#define OLSR_DEBUG_NH(source, format, args...) do { } while(0)
#else
#define OLSR_DEBUG(source, format, args...) do { if (log_global_mask.mask[SEVERITY_DEBUG][source]) olsr_log(SEVERITY_DEBUG, source, false, __FILE__, __LINE__, format, ##args); } while(0)
#define OLSR_DEBUG_NH(source, format, args...) do { if (log_global_mask.mask[SEVERITY_DEBUG][source]) olsr_log(SEVERITY_DEBUG, source, true, __FILE__, __LINE__, format, ##args); } while(0)
#endif

#ifdef REMOVE_LOG_INFO
#define OLSR_INFO(source, format, args...) do { } while(0)
#define OLSR_INFO_NH(source, format, args...) do { } while(0)
#else
#define OLSR_INFO(source, format, args...) do { if (log_global_mask.mask[SEVERITY_INFO][source]) olsr_log(SEVERITY_INFO, source, false, __FILE__, __LINE__, format, ##args); } while(0)
#define OLSR_INFO_NH(source, format, args...) do { if (log_global_mask.mask[SEVERITY_INFO][source]) olsr_log(SEVERITY_INFO, source, true, __FILE__, __LINE__, format, ##args); } while(0)
#endif

#ifdef REMOVE_LOG_WARN
#define OLSR_WARN(source, format, args...) do { } while(0)
#define OLSR_WARN_NH(source, format, args...) do { } while(0)

#define OLSR_WARN_OOM(source) do { } while(0)
#else
#define OLSR_WARN(source, format, args...) do { if (log_global_mask.mask[SEVERITY_WARN][source]) olsr_log(SEVERITY_WARN, source, false, __FILE__, __LINE__, format, ##args); } while(0)
#define OLSR_WARN_NH(source, format, args...) do { if (log_global_mask.mask[SEVERITY_WARN][source]) olsr_log(SEVERITY_WARN, source, true, __FILE__, __LINE__, format, ##args); } while(0)

#define OLSR_WARN_OOM(source) do { if (log_global_mask.mask[SEVERITY_WARN][source]) olsr_log_oom(SEVERITY_WARN, source, __FILE__, __LINE__); } while(0)
#endif

typedef void log_handler_cb(struct log_handler_entry *, struct log_parameters *);

struct log_handler_mask {
  bool mask[LOG_SEVERITY_COUNT][LOG_SOURCE_COUNT];
};

struct log_handler_entry {
  struct list_entity node;
  log_handler_cb *handler;

  /* pointer to handlers own bitmask */
  struct log_handler_mask *bitmask_ptr;

  /* internal bitmask copy */
  struct log_handler_mask int_bitmask;

  /* custom pointer for log handler */
  void *custom;
};

EXPORT extern struct log_handler_mask log_global_mask;

EXPORT int olsr_log_init(const char *, enum log_severity)
  __attribute__((warn_unused_result));
EXPORT void olsr_log_cleanup(void);

EXPORT const char *olsr_log_get_programm_name(void);

EXPORT void olsr_log_addhandler(struct log_handler_entry *);
EXPORT void olsr_log_removehandler(struct log_handler_entry *);
EXPORT void olsr_log_updatemask(void);

EXPORT void olsr_log(enum log_severity, enum log_source, bool, const char *, int, const char *, ...)
  __attribute__ ((format(printf, 6, 7)));

EXPORT void olsr_log_oom(enum log_severity, enum log_source, const char *, int);

EXPORT void olsr_log_stderr(struct log_handler_entry *,
    struct log_parameters *);
EXPORT void olsr_log_syslog(struct log_handler_entry *,
    struct log_parameters *);
EXPORT void olsr_log_file(struct log_handler_entry *,
    struct log_parameters *);

#endif /* OLSR_LOGGING_H_ */
