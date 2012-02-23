
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

#include "stdlib.h"
#include "string.h"

#include "common/common_types.h"
#include "common/autobuf.h"
#include "common/list.h"
#include "builddata/data.h"

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

/*
 * Defines the source of a logging event.
 * Keep this in the same order as the log_source and
 * log_severity enums (see olsr_logging_sources.h).
 */
enum log_source {
  LOG_ALL,
  LOG_LOGGING,
  LOG_CONFIG,
  LOG_MAIN,
  LOG_SOCKET,
  LOG_TIMER,
  LOG_MEMCOOKIE,
  LOG_SOCKET_STREAM,
  LOG_SOCKET_PACKET,
  LOG_INTERFACE,
  LOG_OS_NET,
  LOG_OS_SYSTEM,
  LOG_OS_ROUTING,
  LOG_PLUGINLOADER,
  LOG_TELNET,
  LOG_PLUGINS,
  LOG_HTTP,

  /* this one must be the last of the enums ! */
  LOG_CORESOURCE_COUNT
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

#define _OLSR_LOG(severity, source, no_header, format, args...) do { if (log_global_mask[source].log_for_severity[severity]) olsr_log(severity, source, no_header, __FILE__, __LINE__, format, ##args); } while(0)

#ifdef REMOVE_LOG_DEBUG
#define OLSR_DEBUG(source, format, args...) do { } while(0)
#define OLSR_DEBUG_NH(source, format, args...) do { } while(0)
#else
#define OLSR_DEBUG(source, format, args...) _OLSR_LOG(SEVERITY_DEBUG, source, false, format, ##args)
#define OLSR_DEBUG_NH(source, format, args...) _OLSR_LOG(SEVERITY_DEBUG, source, true, format, ##args)
#endif

#ifdef REMOVE_LOG_INFO
#define OLSR_INFO(source, format, args...) do { } while(0)
#define OLSR_INFO_NH(source, format, args...) do { } while(0)
#else
#define OLSR_INFO(source, format, args...) _OLSR_LOG(SEVERITY_INFO, source, false, format, ##args)
#define OLSR_INFO_NH(source, format, args...) _OLSR_LOG(SEVERITY_WARN, source, true, format, ##args)
#endif

#ifdef REMOVE_LOG_WARN
#define OLSR_WARN(source, format, args...) do { } while(0)
#define OLSR_WARN_NH(source, format, args...) do { } while(0)

#define OLSR_WARN_OOM(source) do { } while(0)
#else
#define OLSR_WARN(source, format, args...) _OLSR_LOG(SEVERITY_WARN, source, false, format, ##args)
#define OLSR_WARN_NH(source, format, args...) _OLSR_LOG(SEVERITY_WARN, source, true, format, ##args)

#define OLSR_WARN_OOM(source) do { if (log_global_mask[source].log_for_severity[SEVERITY_WARN]) olsr_log_oom(SEVERITY_WARN, source, __FILE__, __LINE__); } while(0)
#endif

typedef void log_handler_cb(struct log_handler_entry *, struct log_parameters *);

struct log_handler_mask_entry {
  bool log_for_severity[LOG_SEVERITY_COUNT];
};

struct log_handler_entry {
  struct list_entity node;
  log_handler_cb *handler;

  /* pointer to handlers own bitmask */
  struct log_handler_mask_entry *bitmask;

  /* internal bitmask copy */
  struct log_handler_mask_entry *int_bitmask;

  /* custom pointer for log handler */
  void *custom;
};

EXPORT extern struct log_handler_mask_entry *log_global_mask;
EXPORT extern const char **LOG_SOURCE_NAMES;
EXPORT extern const char *LOG_SEVERITY_NAMES[];

EXPORT int olsr_log_init(const struct olsr_builddata *, enum log_severity,
    const char **lognames, size_t level_count)
  __attribute__((warn_unused_result));
EXPORT void olsr_log_cleanup(void);

EXPORT void olsr_log_addhandler(struct log_handler_entry *);
EXPORT void olsr_log_removehandler(struct log_handler_entry *);
EXPORT void olsr_log_updatemask(void);

EXPORT enum log_source olsr_log_get_sourcecount(void);
EXPORT const struct olsr_builddata *olsr_log_get_builddata(void);
EXPORT void olsr_log_printversion(struct autobuf *abuf);

EXPORT void olsr_log(enum log_severity, enum log_source, bool, const char *, int, const char *, ...)
  __attribute__ ((format(printf, 6, 7)));

EXPORT void olsr_log_oom(enum log_severity, enum log_source, const char *, int);

EXPORT void olsr_log_stderr(struct log_handler_entry *,
    struct log_parameters *);
EXPORT void olsr_log_syslog(struct log_handler_entry *,
    struct log_parameters *);
EXPORT void olsr_log_file(struct log_handler_entry *,
    struct log_parameters *);

/**
 * Allocates an empty logging mask.
 * @return pointer to logging mask, NULL if not enough memory
 */
static INLINE struct log_handler_mask_entry *
olsr_log_allocate_mask(void) {
  return calloc(olsr_log_get_sourcecount(), sizeof(struct log_handler_mask_entry));
}

/**
 * Free the memory of an allocated logging mask.
 * @param mask pointer to mask
 */
static INLINE void
olsr_log_free_mask(struct log_handler_mask_entry *mask) {
  free(mask);
}

/**
 * Copies a logging mask
 * @param dst destination logging mask
 * @param src source logging mask
 */
static INLINE void
olsr_log_copy_mask(struct log_handler_mask_entry *dst, struct log_handler_mask_entry *src) {
  memcpy(dst, src, sizeof(struct log_handler_mask_entry) * olsr_log_get_sourcecount());
}

/**
 * Clears a logging mask
 * @param mask logging mask to be cleared
 */
static INLINE void
olsr_log_clear_mask(struct log_handler_mask_entry *mask) {
  memset(mask, 0, sizeof(struct log_handler_mask_entry) * olsr_log_get_sourcecount());
}

#endif /* OLSR_LOGGING_H_ */
