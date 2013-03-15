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
#include "rfc5444/rfc5444.h"
#include "core/olsr_class.h"

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
 * @param prefix local attacked network prefix
 * @return pointer to lan entry, NULL if out of memory
 */
struct olsrv2_lan_entry *
olsrv2_lan_add(struct netaddr *prefix) {
  struct olsrv2_lan_entry *entry;

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

    /* initialize metric with default */
    entry->outgoing_metric = RFC5444_METRIC_DEFAULT;
  }

  return entry;
}

/**
 * Remove a local attached network entry
 * @param prefix local attacked network prefix
 */
void
olsrv2_lan_remove(struct netaddr *prefix) {
  struct olsrv2_lan_entry *entry;

  entry = olsrv2_lan_get(prefix);
  if (entry) {
    _remove(entry);
  }
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
