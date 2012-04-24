
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "common/common_types.h"
#include "common/list.h"
#include "common/avl.h"
#include "common/avl_comp.h"
#include "olsr_logging.h"
#include "olsr_callbacks.h"

static const char *_unknown_key(void *);

const char *OLSR_CALLBACK_EVENTS[3] = {
  [CALLBACK_EVENT_CHANGE] = "change",
  [CALLBACK_EVENT_ADD] = "add",
  [CALLBACK_EVENT_REMOVE] = "remove",
};

struct avl_tree callback_provider_tree;

enum log_source LOG_CALLBACK;

/**
 * Initialize the internal data of the callback provider system
 */
void
olsr_callback_init(void) {
  avl_init(&callback_provider_tree, avl_comp_strcasecmp, false, NULL);

  LOG_CALLBACK = olsr_log_register_source("callback");
}

/**
 * Cleanup the internal data of the callback provider system
 */
void
olsr_callback_cleanup(void) {
  struct olsr_callback_provider *prv, *iterator;

  OLSR_FOR_ALL_CALLBACK_PROVIDERS(prv, iterator) {
    olsr_callback_remove(prv);
  }
}

/**
 * Create a new callback provider
 * @param prv pointer to callback provider
 * @param name pointer to name of provider
 * @return 0 if provider was registered successfully, -1 otherwise
 */
int
olsr_callback_add(struct olsr_callback_provider *prv) {
  /* check if provider already exists */
  if (avl_find(&callback_provider_tree, prv->name) != NULL) {
    OLSR_WARN(LOG_CALLBACK,
        "Provider '%s' already exists. Not creating.\n", prv->name);
    return -1;
  }

  OLSR_DEBUG(LOG_CALLBACK, "Create callback provider '%s'\n", prv->name);

  prv->_node.key = prv->name;
  avl_insert(&callback_provider_tree, &prv->_node);

  if (prv->cb_getkey == NULL) {
    prv->cb_getkey = _unknown_key;
  }

  list_init_head(&prv->_callbacks);
  return 0;
}

/**
 * Cleans up an existing registered callback provider
 * @param pointer to initialized callback provider
 */
void
olsr_callback_remove(struct olsr_callback_provider *prv) {
  struct olsr_callback_consumer *cons, *iterator;

  OLSR_DEBUG(LOG_CALLBACK, "Destroying callback provider '%s' (object count %u)\n",
      prv->name, prv->_obj_count);

  OLSR_FOR_ALL_CALLBACK_CONSUMERS(prv, cons, iterator) {
    olsr_callback_unregister_consumer(cons);
  }

  avl_delete(&callback_provider_tree, &prv->_node);
  free(prv->name);
  prv->name = NULL;
}

/**
 * Registers a new callback consumer to an existing provider
 * @param prv_name name of callback provider
 * @param cons_name name of new callback consumer
 * @param cons pointer to uninitialized callback consumer
 * @return 0 if successfully registered, -1 otherwise
 */
int
olsr_callback_register_consumer(struct olsr_callback_consumer *cons) {
  struct olsr_callback_provider *prv;

  prv = avl_find_element(&callback_provider_tree, cons->provider, prv, _node);
  if (prv == NULL) {
    OLSR_WARN(LOG_CALLBACK, "Could not find callback provider '%s'\n", cons->provider);
    return -1;
  }

  OLSR_DEBUG(LOG_CALLBACK, "Register callback '%s' with provider '%s'\n",
      cons->name, cons->provider);

  list_add_tail(&prv->_callbacks, &cons->_node);
  return 0;
}

/**
 * Unregistered an initialized callback consumer
 * @param cons pointer to callback consumer
 */
void
olsr_callback_unregister_consumer(struct olsr_callback_consumer *cons) {
  if (list_is_node_added(&cons->_node)) {
    OLSR_DEBUG(LOG_CALLBACK, "Unregister callback '%s' with provider '%s'\n",
        cons->name, cons->provider);

    list_remove(&cons->_node);
  }
}

/**
 * Fire a callback for an object
 * @param prv callback provider
 * @param obj pointer to object
 * @param event type of event
 */
void
olsr_callback_event(struct olsr_callback_provider *prv, void *obj,
    enum olsr_callback_event event) {
  struct olsr_callback_consumer *cons, *iterator;

  if (prv->_in_use) {
    OLSR_WARN(LOG_CALLBACK, "Warning, recursive use of callback %s. Skipping.\n",
        prv->name);
    return;
  }

  prv->_in_use = true;
  prv->_obj_count++;
  OLSR_DEBUG(LOG_CALLBACK, "object %s (%u) to callback '%s': %s event\n",
      prv->cb_getkey(obj), prv->_obj_count, prv->name,
      OLSR_CALLBACK_EVENTS[event]);

  OLSR_FOR_ALL_CALLBACK_CONSUMERS(prv, cons, iterator) {
    bool fire = false;

    switch (event) {
      case CALLBACK_EVENT_CHANGE:
        if (cons->cb_change) {
          cons->cb_add(obj);
          fire = true;
        }
        break;
      case CALLBACK_EVENT_ADD:
        if (cons->cb_add) {
          cons->cb_add(obj);
          fire = true;
        }
        break;
      case CALLBACK_EVENT_REMOVE:
        if (cons->cb_remove) {
          cons->cb_remove(obj);
          fire = true;
        }
        break;
    }

    if (fire) {
      OLSR_DEBUG_NH(LOG_CALLBACK, "Fired %s callback for provider %s",
          OLSR_CALLBACK_EVENTS[event], cons->name);
    }
  }
  prv->_in_use = false;
}

/**
 * Helper function to display a name of an object.
 * @param pointer to object
 * @return string representation of objects hexadecimal address
 */
static const char *
_unknown_key(void *obj) {
  static char buffer[32];

  snprintf(buffer, sizeof(buffer),
      "0x%"PRINTF_SIZE_T_SPECIFIER"x", (size_t)obj);
  return buffer;
}
