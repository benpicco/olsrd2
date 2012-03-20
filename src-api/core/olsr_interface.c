
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

#include "common/common_types.h"
#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/list.h"
#include "common/netaddr.h"

#include "olsr_interface.h"
#include "olsr_logging.h"
#include "olsr_timer.h"
#include "olsr.h"
#include "os_net.h"
#include "os_system.h"
#include "os_routing.h"

/* timeinterval to delay change in interface to trigger actions */
#define OLSR_INTERFACE_CHANGE_INTERVAL 100

static struct olsr_interface *_interface_add(uint32_t, bool mesh);
static void _interface_remove(uint32_t, bool mesh);
static void _cb_change_handler(void *);
static void _trigger_change_timer(struct olsr_interface *);

/* global tree of known interfaces */
struct avl_tree olsr_interface_tree;

/* remember state of subsystem */
OLSR_SUBSYSTEM_STATE(_interface_state);

static struct list_entity _interface_listener;
static struct olsr_timer_info _change_timer_info = {
  .name = "Interface change",
  .callback = _cb_change_handler,
};

/**
 * Initialize interface subsystem
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_interface_init(void) {
  if (olsr_subsystem_is_initialized(&_interface_state))
    return 0;

  olsr_timer_add(&_change_timer_info);

  avl_init(&olsr_interface_tree, avl_comp_uint32, false, NULL);
  list_init_head(&_interface_listener);

  olsr_subsystem_init(&_interface_state);
  return 0;
}

/**
 * Cleanup interface subsystem
 */
void
olsr_interface_cleanup(void) {
  struct olsr_interface_listener *listener, *l_it;

  if (olsr_subsystem_cleanup(&_interface_state))
    return;

  list_for_each_element_safe(&_interface_listener, listener, node, l_it) {
    olsr_interface_remove_listener(listener);
  }

  olsr_timer_remove(&_change_timer_info);
}

/**
 * Add a listener to a specific interface
 * @param listener initialized listener object
 * @return pointer to olsr_interface struct, NULL if an error happened
 */
struct olsr_interface *
olsr_interface_add_listener(
    struct olsr_interface_listener *listener) {
  struct olsr_interface *interf;

  if (listener->if_index == 0 && listener->name) {
    listener->if_index = if_nametoindex(listener->name);
  }
  interf = _interface_add(listener->if_index, listener->mesh);
  if (interf != NULL && listener->process != NULL) {
    list_add_tail(&_interface_listener, &listener->node);
  }

  return interf;
}

/**
 * Removes a listener to an interface object
 * @param listener pointer to listener object
 */
void
olsr_interface_remove_listener(
    struct olsr_interface_listener *listener) {
  if (listener->process) {
    list_remove(&listener->node);
  }
  _interface_remove(listener->if_index, listener->mesh);
}

/**
 * Trigger a potential change in the interface settings. Normally
 * called by os_system code
 * @param if_index index of interface
 */
void
olsr_interface_trigger_change(uint32_t if_index) {
  struct olsr_interface *interf;

  interf = avl_find_element(&olsr_interface_tree, &if_index, interf, node);
  if (interf == NULL) {
    return;
  }

  /* trigger interface reload in 100 ms */
  _trigger_change_timer(interf);
}

struct olsr_interface_data *
olsr_interface_get_data(uint32_t if_index) {
  struct olsr_interface *interf;

  interf = avl_find_element(&olsr_interface_tree, &if_index, interf, node);
  if (interf == NULL) {
    return NULL;
  }

  return &interf->data;
}

/**
 * Add an interface to the listener system
 * @param if_index index of interface
 * @param mesh true if interface is used for mesh traffic
 * @return pointer to interface struct, NULL if an error happened
 */
static struct olsr_interface *
_interface_add(uint32_t if_index, bool mesh) {
  struct olsr_interface *interf;

  interf = avl_find_element(&olsr_interface_tree, &if_index, interf, node);
  if (!interf) {
    /* allocate new interface */
    interf = calloc(1, sizeof(*interf));
    if (interf == NULL) {
      OLSR_WARN_OOM(LOG_INTERFACE);
      return NULL;
    }

    /* hookup */
    interf->data.index = if_index;
    interf->node.key = &interf->data.index;
    avl_insert(&olsr_interface_tree, &interf->node);

    interf->change_timer.info = &_change_timer_info;
    interf->change_timer.cb_context = interf;

    /* initialize data of interface */
    os_net_update_interface(&interf->data, if_index);
  }

  /* update reference counters */
  interf->usage_counter++;
  if(mesh) {
    if (interf->mesh_counter == 0) {
      os_routing_init_mesh_if(interf);
    }
    interf->mesh_counter++;
  }

  /* trigger update */
  _trigger_change_timer(interf);

  return interf;
}

/**
 * Remove an interface from the listener system. If multiple listeners
 * share an interface, this will only decrease the reference counter.
 * @param if_index index of interface
 * @param mesh true if interface is used for mesh traffic
 */
static void
_interface_remove(uint32_t if_index, bool mesh) {
  struct olsr_interface *interf;

  interf = avl_find_element(&olsr_interface_tree, &if_index, interf, node);
  if (!interf) {
    return;
  }

  interf->usage_counter--;
  if (mesh) {
    interf->mesh_counter--;

    if (interf->mesh_counter < 1) {
      os_routing_cleanup_mesh_if(interf);
    }
  }

  if (interf->usage_counter > 0) {
    return;
  }

  avl_remove(&olsr_interface_tree, &interf->node);
  free(interf);
}


/**
 * Timer callback to handle potential change of data of an interface
 * @param ptr pointer to interface object
 */
static void
_cb_change_handler(void *ptr) {
  struct olsr_interface_data old_data, new_data;
  struct olsr_interface_listener *listener, *l_it;
  struct olsr_interface *interf;

  interf = ptr;

  /* read interface data */
  if (os_net_update_interface(&new_data, interf->data.index)) {
    /* an error happened, try again */
    _trigger_change_timer(interf);
    return;
  }

  /* something changed ? */
  if (memcmp(&interf->data, &new_data, sizeof(new_data)) == 0) {
    return;
  }

  /* copy data to interface object, but remember the old data */
  memcpy(&old_data, &interf->data, sizeof(old_data));
  memcpy(&interf->data, &new_data, sizeof(interf->data));

  /* call listeners */
  list_for_each_element_safe(&_interface_listener, listener, node, l_it) {
    if (listener->process != NULL && listener->if_index == interf->data.index) {
      listener->process(interf, &old_data);
    }
  }
}

/**
 * Activate the change timer of an interface object
 * @param interf pointer to interface object
 */
static void
_trigger_change_timer(struct olsr_interface *interf) {
  olsr_timer_set(&interf->change_timer, OLSR_INTERFACE_CHANGE_INTERVAL);
}
