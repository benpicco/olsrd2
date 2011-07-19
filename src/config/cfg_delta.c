
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

#include "common/avl.h"
#include "common/avl_comp.h"

#include "config/cfg.h"
#include "config/cfg_db.h"
#include "config/cfg_delta.h"

static void _delta_section(struct cfg_delta *delta,
    struct cfg_section_type *pre_change, struct cfg_section_type *post_change);
static void _handle_namedsection(struct cfg_delta *delta, const char *type,
    struct cfg_named_section *pre_change, struct cfg_named_section *post_change);
static bool _setup_filterresults(struct cfg_delta_handler *handler,
    struct cfg_named_section *pre_change, struct cfg_named_section *post_change);

/**
 * Initialize the root of a delta handler list
 * @param delta pointer to cfg_delta object
 */
void
cfg_delta_add(struct cfg_delta *delta) {
  list_init_head(&delta->handler);
  list_init_head(&delta->all_types_handler);
}

/**
 * Cleans up a cfg_delta object, will remove
 * all hooked up handlers from the internal lists
 * @param delta pointer to cfg_delta object
 */
void
cfg_delta_remove(struct cfg_delta *delta) {
  struct cfg_delta_handler *handler, *iterator;

  list_for_each_element_safe(&delta->handler, handler, node, iterator) {
    cfg_delta_remove_handler(handler);
  }
  list_for_each_element_safe(&delta->all_types_handler, handler, node, iterator) {
    cfg_delta_remove_handler(handler);
  }
}

/**
 * Adds a handler to a cfg_delta list
 * Some fields of the handler have to be initialized before this
 * call, see header file for description
 *
 * @param delta pointer to cfg_delta object
 * @param handler pointer to handler
 */
void
cfg_delta_add_handler(struct cfg_delta *delta, struct cfg_delta_handler *handler) {
  assert(handler->callback);

  if (handler->s_type) {
    list_add_tail(&delta->handler, &handler->node);
  }
  else {
    list_add_tail(&delta->all_types_handler, &handler->node);
  }
}

/**
 * Removes a handler from a cfg_delta list
 * @param handler pointer to delta handler
 */
void
cfg_delta_remove_handler(struct cfg_delta_handler *handler) {
  list_remove(&handler->node);
}

/**
 * Adds a delta handler by copying a schema section and entries
 * @param delta base pointer for delta calculation
 * @param callback callback for delta handling
 * @param d_handler pointer to uninitialized cfg_delta_handler
 * @param d_filter pointer to uninitialized array of cfg_delta_filter
 * @param s_section pointer to initialized schema_section
 * @param s_entries pointer to initialized array of schema_entry
 * @param count number of schema_entry and cfg_delta_filter in array
 */
void
cfg_delta_add_handler_by_schema(
    struct cfg_delta *delta, cfg_delta_callback *callback,
    struct cfg_delta_handler *d_handler, struct cfg_delta_filter *d_filter,
    struct cfg_schema_section *s_section, struct cfg_schema_entry *s_entries,
    size_t count) {
  size_t i;

  d_handler->s_type = s_section->t_type;
  d_handler->callback = callback;
  d_handler->filter = d_filter;

  for (i=0; i < count; i++) {
    memset(&d_filter[i], 0, sizeof(struct cfg_delta_filter));
    d_filter[i].k = s_entries[i].t_name;
  }
  memset(&d_filter[i], 0, sizeof(struct cfg_delta_filter));

  cfg_delta_add_handler(delta, d_handler);
}

/**
 * Calculates the difference between two configuration
 * databases and call all corresponding handlers.
 * @param delta pointer to cfg_delta object
 * @param pre_change database containing the pre-change settings
 * @param post_change database containing the post-change settings
 */
void
cfg_delta_calculate(struct cfg_delta *delta,
    struct cfg_db *pre_change, struct cfg_db *post_change) {
  struct cfg_section_type *section_pre = NULL, *section_post = NULL;

  struct cfg_named_section *named, *named_it;

  section_pre = avl_first_element_safe(&pre_change->sectiontypes, section_pre, node);
  section_post = avl_first_element_safe(&post_change->sectiontypes, section_post, node);

  while (section_pre != NULL || section_post != NULL) {
    int cmp_sections;

    /* compare pre and post */
    cmp_sections = cfg_cmp_keys(section_pre->type, section_post->type);

    if (cmp_sections < 0) {
      /* handle pre-section */
      OLSR_FOR_ALL_CFG_SECTION_NAMES(section_pre, named, named_it) {
        _handle_namedsection(delta, section_pre->type, named, NULL);
      }
    }
    else if (cmp_sections > 0) {
      /* handle post-section */
      OLSR_FOR_ALL_CFG_SECTION_NAMES(section_post, named, named_it) {
        _handle_namedsection(delta, section_post->type, NULL, named);
      }
    }
    else {
      /* type is available in both db's, this might be an change event */
      _delta_section(delta, section_pre, section_post);
    }

    if (cmp_sections <= 0) {
      section_pre = avl_next_element_safe(&pre_change->sectiontypes, section_pre, node);
    }
    if (cmp_sections >= 0) {
      section_post = avl_next_element_safe(&post_change->sectiontypes, section_post, node);
    }
  }
}

/**
 * Calculates the delta between two section types
 * @param delta pointer to cfg_delta object
 * @param pre_change
 * @param post_change
 */
static void
_delta_section(struct cfg_delta *delta,
    struct cfg_section_type *pre_change, struct cfg_section_type *post_change) {
  struct cfg_named_section *named_pre = NULL, *named_post = NULL;

  named_pre = avl_first_element_safe(&pre_change->names, named_pre, node);
  named_post = avl_first_element_safe(&post_change->names, named_post, node);

  while (named_pre != NULL || named_post != NULL) {
    int cmp_sections;

    /* compare pre and post */
    cmp_sections = cfg_cmp_keys(named_pre->name, named_post->name);

    if (cmp_sections < 0) {
      /* handle pre-named */
      _handle_namedsection(delta, pre_change->type, named_pre, NULL);
    }
    else if (cmp_sections > 0) {
      /* handle post-section */
      _handle_namedsection(delta, post_change->type, NULL, named_post);
    }
    else {
      /* named section is available in both db's, we have section change */
      _handle_namedsection(delta, pre_change->type, named_pre, named_post);
    }

    if (cmp_sections <= 0) {
      named_pre = avl_next_element_safe(&pre_change->names, named_pre, node);
    }
    if (cmp_sections >= 0) {
      named_post = avl_next_element_safe(&post_change->names, named_post, node);
    }
  }
}
/**
 * Handles the difference between two named sections
 * @param delta pointer to cfg_delta object
 * @param type pointer to type string of section
 * @param pre_change pointer to a named section before the change,
 *   NULL if section was added.
 * @param post_change pointer to a named section after the change,
 *   NULL if section was removed.
 */
static void
_handle_namedsection(struct cfg_delta *delta, const char *type,
    struct cfg_named_section *pre_change, struct cfg_named_section *post_change) {
  struct cfg_delta_handler *handler;

  list_for_each_element(&delta->handler, handler, node) {
    if (cfg_cmp_keys(handler->s_type, type) != 0) {
      continue;
    }

    if (_setup_filterresults(handler, pre_change, post_change)) {
      handler->callback(handler, pre_change, post_change);
    }
  }
  list_for_each_element(&delta->all_types_handler, handler, node) {
    if (_setup_filterresults(handler, pre_change, post_change)) {
      handler->callback(handler, pre_change, post_change);
    }
  }
}

/**
 * Initializes the filter results for a change between two named
 * sections for a handler
 *
 * @param handler pointer to delta handler
 * @param pre_change pointer to a named section before the change,
 *   NULL if section was added.
 * @param post_change pointer to a named section after the change,
 *   NULL if section was removed.
 * @return true if callback has to be called, either because at least
 *   one filter matches or because the handler has no filters.
 *   false otherwise.
 */
static bool
_setup_filterresults(struct cfg_delta_handler *handler,
    struct cfg_named_section *pre_change, struct cfg_named_section *post_change) {
  struct cfg_entry *pre, *post;
  bool change;
  size_t i;

  if (handler->filter == NULL) {
    return true;
  }

  pre = NULL;
  post = NULL;
  change = false;

  for (i=0; handler->filter[i].k != NULL; i++) {
    if (pre_change) {
      pre = avl_find_element(&pre_change->entries, handler->filter[i].k, pre, node);
    }
    if (post_change) {
      post = avl_find_element(&post_change->entries, handler->filter[i].k, post, node);
    }

    handler->filter[i].pre = pre;
    handler->filter[i].post = post;

    if (pre == NULL && post == NULL) {
      handler->filter[i].delta = CFG_DELTA_NO_CHANGE;
    }
    else if (pre == NULL) {
      handler->filter[i].delta = CFG_DELTA_ADDED;
      change = true;
    }
    else if (post == NULL) {
      handler->filter[i].delta = CFG_DELTA_REMOVED;
      change = true;
    }
    else if (pre->length == post->length
        && memcmp(pre->value, post->value, pre->length) == 0) {
      handler->filter[i].delta = CFG_DELTA_NO_CHANGE;
    }
    else {
      handler->filter[i].delta = CFG_DELTA_CHANGED;
      change = true;
    }
  }
  return change;
}
