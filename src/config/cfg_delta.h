
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

#ifndef CFG_DELTA_H_
#define CFG_DELTA_H_

struct cfg_delta;
struct cfg_delta_handler;
struct cfg_delta_filter;

#include "common/list.h"
#include "config/cfg_db.h"
#include "config/cfg_schema.h"

/**
 * Root of a series of delta handlers
 */
struct cfg_delta {
  /* tree of handlers */
  struct avl_tree handler;
};

/* callback for delta management */
typedef void cfg_delta_callback(void);

/**
 * A single handler definition for configuration delta
 * calculation
 */
struct cfg_delta_handler {
  /*
   * Node that holds the handler tree together,
   * will be initialized by cfg_delta_add_handler
   */
  struct avl_node node;

  /* priority for callbacks */
  uint32_t priority;

  /* section type for this handler, NULL for all types */
  const char *s_type;

  /*
   * pointer to a list of filters for this handler,
   * NULL for no filters (interested in anything)
   */
  struct cfg_delta_filter *filter;

  /*
   * callback handling detected changes between
   * different named configuration sections
   */
  cfg_delta_callback *callback;

  /* custom pointer for callback usage */
  void *custom;

  /*
   * internal variable for triggering callbacks,
   * will always be true when callback is triggered.
   */
  bool _trigger_callback;

  /*
   * internal variables to store parameters for
   * triggered callback
   */
  struct cfg_named_section *pre_delta, *post_delta;
};

/* type of change that happened for a filter */
enum cfg_delta_event {
  CFG_DELTA_NO_CHANGE = 0,
  CFG_DELTA_ADDED     = 1,
  CFG_DELTA_CHANGED   = 2,
  CFG_DELTA_REMOVED   = 3,
};

/**
 * One filter entry for a delta handler filter.
 * Only "k" must be filled by the user.
 */
struct cfg_delta_filter {
  /* key of the entry this filter matches */
  const char *k;

  /*
   * defines the change happened for the filters entry,
   * set by cfg_delta_calculate()
   */
  enum cfg_delta_event delta;

  /*
   * Pointer to entry before and after the change.
   * pre will be NULL for new entries,
   * post will be NULL for removed entries
   */
  struct cfg_entry *pre, *post;
};

EXPORT void cfg_delta_add(struct cfg_delta *);
EXPORT void cfg_delta_remove(struct cfg_delta *);

EXPORT void cfg_delta_add_handler(struct cfg_delta *, struct cfg_delta_handler *);
EXPORT void cfg_delta_remove_handler(struct cfg_delta *, struct cfg_delta_handler *);

EXPORT void cfg_delta_add_handler_by_schema(
    struct cfg_delta *delta, cfg_delta_callback *callback, uint32_t priority,
    struct cfg_delta_handler *d_handler, struct cfg_delta_filter *d_filter,
    struct cfg_schema_section *s_section, struct cfg_schema_entry *s_entries,
    size_t count);

EXPORT void cfg_delta_calculate(struct cfg_delta *, struct cfg_db *, struct cfg_db *);

#endif /* CFG_DELTA_H_ */
