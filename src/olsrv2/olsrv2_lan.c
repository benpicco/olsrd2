
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
#include "core/olsr_logging.h"
#include "core/olsr_subsystem.h"
#include "subsystems/olsr_class.h"
#include "rfc5444/rfc5444.h"

#include "nhdp/nhdp.h"
#include "olsrv2/olsrv2.h"
#include "olsrv2/olsrv2_lan.h"

static void _remove(struct olsrv2_lan_entry *entry);

/* originator set class and timer */
static struct olsr_class _lan_class = {
  .name = "OLSRv2 LAN set",
  .size = sizeof(struct olsrv2_lan_entry),
};

/* global tree of originator set entries */
struct avl_tree olsrv2_lan_tree;

/**
 * Initialize olsrv2 lan set
 */
void
olsrv2_lan_init(void) {
  olsr_class_add(&_lan_class);

  avl_init(&olsrv2_lan_tree, avl_comp_netaddr, false);
}

/**
 * Cleanup all resources allocated by orignator set
 */
void
olsrv2_lan_cleanup(void) {
  struct olsrv2_lan_entry *entry, *e_it;

  /* remove all originator entries */
  avl_for_each_element_safe(&olsrv2_lan_tree, entry, _node, e_it) {
    _remove(entry);
  }

  /* remove class */
  olsr_class_remove(&_lan_class);
}

/**
 * Add a new entry to the olsrv2 local attached network
 * @param domain NHDP domain for data
 * @param prefix local attacked network prefix
 * @param metric outgoing metric
 * @param distance hopcount distance
 * @return pointer to lan entry, NULL if out of memory
 */
struct olsrv2_lan_entry *
olsrv2_lan_add(struct nhdp_domain *domain,
    struct netaddr *prefix, uint32_t metric, uint8_t distance) {
  struct olsrv2_lan_entry *entry;
  int i;

  entry = olsrv2_lan_get(prefix);
  if (entry == NULL) {
    entry = olsr_class_malloc(&_lan_class);
    if (entry == NULL) {
      return NULL;
    }

    /* copy key and append to tree */
    memcpy(&entry->prefix, prefix, sizeof(*prefix));
    entry->_node.key = &entry->prefix;
    avl_insert(&olsrv2_lan_tree, &entry->_node);

    /* initialize linkcost and distance */
    for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
      entry->data[i].outgoing_metric = RFC5444_METRIC_INFINITE;
      entry->data[i].distance = 0;
      entry->data[i].active = false;
    }
  }

  entry->data[domain->index].outgoing_metric = metric;
  entry->data[domain->index].distance = distance;
  entry->data[domain->index].active = true;

  return entry;
}

/**
 * Remove a local attached network entry
 * @param prefix local attacked network prefix
 * @param domain NHDP domain for data
 */
void
olsrv2_lan_remove(struct nhdp_domain *domain,
    struct netaddr *prefix) {
  struct olsrv2_lan_entry *entry;
  int i;

  entry = olsrv2_lan_get(prefix);
  if (!entry) {
    return;
  }

  entry->data[domain->index].active = false;

  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    if (entry->data[i].active) {
      /* entry is still in use */
      return;
    }
  }
  _remove(entry);
}

/**
 * Remove a local attached network entry
 * @param entry LAN entry
 */
static void
_remove(struct olsrv2_lan_entry *entry) {
  avl_remove(&olsrv2_lan_tree, &entry->_node);
  olsr_class_free(&_lan_class, entry);
}
