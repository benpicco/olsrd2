
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2013, the olsr.org team - see HISTORY file
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

#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/common_types.h"
#include "common/netaddr.h"
#include "common/netaddr_acl.h"
#include "core/oonf_logging.h"
#include "subsystems/oonf_class.h"
#include "subsystems/oonf_interface.h"
#include "subsystems/oonf_timer.h"

#include "nhdp/nhdp.h"

#include "olsrv2/olsrv2_originator.h"
#include "olsrv2/olsrv2.h"

/* prototypes */
static struct olsrv2_originator_set_entry *_remember_removed_originator(
    struct netaddr *originator, uint64_t vtime);
static void _set_originator(int af_type, struct netaddr *setting, const struct netaddr *new);
static void _cb_originator_entry_vtime(void *);

/* originator set class and timer */
static struct oonf_class _originator_entry_class = {
  .name = "OLSRV2 originator set",
  .size = sizeof(struct olsrv2_originator_set_entry),
};

static struct oonf_timer_info _originator_entry_timer = {
  .name = "OLSRV2 originator set vtime",
  .callback = _cb_originator_entry_vtime,
};

/* global tree of originator set entries */
struct avl_tree olsrv2_originator_set_tree;

/* current originators */
static struct netaddr _originator_v4, _originator_v6;

/**
 * Initialize olsrv2 originator set
 */
void
olsrv2_originator_init(void) {
  /* initialize class and timer */
  oonf_class_add(&_originator_entry_class);
  oonf_timer_add(&_originator_entry_timer);

  /* initialize global originator tree */
  avl_init(&olsrv2_originator_set_tree, avl_comp_netaddr, false);
}

/**
 * Cleanup all resources allocated by orignator set
 */
void
olsrv2_originator_cleanup(void) {
  struct olsrv2_originator_set_entry *entry, *e_it;

  /* remove all originator entries */
  avl_for_each_element_safe(&olsrv2_originator_set_tree, entry, _node, e_it) {
    _cb_originator_entry_vtime(entry);
  }

  /* remove timer and class */
  oonf_timer_remove(&_originator_entry_timer);
  oonf_class_remove(&_originator_entry_class);
}

/**
 * @return current originator address
 */
const struct netaddr *
olsrv2_originator_get(int af_type) {
  if (af_type == AF_INET) {
    return &_originator_v4;
  }
  else if (af_type == AF_INET6) {
    return &_originator_v6;
  }
  return NULL;
}

bool
olsrv2_originator_is_local(const struct netaddr *addr) {
  if (netaddr_cmp(&_originator_v4, addr) == 0) {
    return true;
  }
  if (netaddr_cmp(&_originator_v6, addr) == 0) {
    return true;
  }
  return olsrv2_originator_get_entry(addr) != NULL;
}

/**
 * Sets the originator address to a new value.
 * Parameter af_type is necessary for the case when both
 * current and new setting are AF_UNSPEC.
 *
 * @param originator new originator
 */
void
olsrv2_originator_set(const struct netaddr *originator) {
  if (netaddr_get_address_family(originator) == AF_INET) {
    _set_originator(AF_INET, &_originator_v4, originator);
  }
  else if (netaddr_get_address_family(originator) == AF_INET6) {
    _set_originator(AF_INET6, &_originator_v6, originator);
  }
}

/**
 * Add a new entry to the olsrv2 originator set
 * @param originator originator address
 * @param vtime validity time of entry
 * @return pointer to originator set entry, NULL if out of memory
 */
static struct olsrv2_originator_set_entry *
_remember_removed_originator(struct netaddr *originator, uint64_t vtime) {
  struct olsrv2_originator_set_entry *entry;

  entry = olsrv2_originator_get_entry(originator);
  if (entry == NULL) {
    entry = oonf_class_malloc(&_originator_entry_class);
    if (entry == NULL) {
      /* out of memory */
      return NULL;
    }

    /* copy key and append to tree */
    memcpy(&entry->originator, originator, sizeof(*originator));
    entry->_node.key = &entry->originator;
    avl_insert(&olsrv2_originator_set_tree, &entry->_node);

    /* initialize timer */
    entry->_vtime.info = &_originator_entry_timer;
    entry->_vtime.cb_context = entry;
  }

  /* reset validity time */
  oonf_timer_set(&entry->_vtime, vtime);

  return entry;
}

/**
 * Sets the originator address to a new value.
 * Parameter af_type is necessary for the case when both
 * current and new setting are AF_UNSPEC.
 *
 * @param af_type address family type of the originator
 *   (AF_INET or AF_INET6)
 * @param setting pointer to the storage of the originator
 * @param originator new originator
 */
static void
_set_originator(int af_type, struct netaddr *setting, const struct netaddr *new) {
  struct olsrv2_originator_set_entry *entry;

  if (netaddr_get_address_family(setting) != AF_UNSPEC) {
    /* add old originator to originator set */
    _remember_removed_originator(setting, olsrv2_get_old_originator_validity());
  }

  memcpy(setting, new, sizeof(*setting));

  /* remove new originator from set */
  entry = olsrv2_originator_get_entry(new);
  if (entry) {
    _cb_originator_entry_vtime(entry);
  }

  /* update NHDP originator */
  if (netaddr_get_address_family(new) != AF_UNSPEC) {
    nhdp_set_originator(new);
  }
  else {
    nhdp_reset_originator(af_type);
  }
}

/**
 * Callback fired when originator set entry must be removed
 * @param ptr pointer to originator set
 */
static void
_cb_originator_entry_vtime(void *ptr) {
  struct olsrv2_originator_set_entry *entry = ptr;

  oonf_timer_stop(&entry->_vtime);
  avl_remove(&olsrv2_originator_set_tree, &entry->_node);

  oonf_class_free(&_originator_entry_class, entry);
}
