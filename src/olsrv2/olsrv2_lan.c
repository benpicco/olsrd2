/*
 * olsrv2_originator_set.c
 *
 *  Created on: Mar 15, 2013
 *      Author: rogge
 */

#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/common_types.h"
#include "common/netaddr.h"
#include "core/olsr_logging.h"
#include "core/olsr_subsystem.h"
#include "core/olsr_class.h"
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
