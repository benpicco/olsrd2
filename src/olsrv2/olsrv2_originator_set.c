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
#include "core/olsr_class.h"
#include "core/olsr_timer.h"

#include "olsrv2/olsrv2_originator_set.h"

static void _cb_vtime(void *);

/* originator set class and timer */
static struct olsr_class _originator_class = {
  .name = "OLSRv2 originator set",
  .size = sizeof(struct olsrv2_originator_entry),
};

static struct olsr_timer_info _originator_timer = {
  .name = "OLSRv2 originator set vtime",
  .callback = _cb_vtime,
};

/* global tree of originator set entries */
struct avl_tree olsrv2_originator_tree;

/**
 * Initialize olsrv2 originator set
 */
void
olsrv2_originatorset_init(void) {
  olsr_class_add(&_originator_class);
  olsr_timer_add(&_originator_timer);

  avl_init(&olsrv2_originator_tree, avl_comp_netaddr, false);
}

/**
 * Cleanup all resources allocated by orignator set
 */
void
olsrv2_originatorset_cleanup(void) {
  struct olsrv2_originator_entry *entry, *e_it;

  /* remove all originator entries */
  avl_for_each_element_safe(&olsrv2_originator_tree, entry, _node, e_it) {
    _cb_vtime(entry);
  }

  /* remove timer and class */
  olsr_timer_remove(&_originator_timer);
  olsr_class_remove(&_originator_class);
}

/**
 * Add a new entry to the olsrv2 originator set
 * @param originator originator address
 * @param vtime validity time of entry
 * @return pointer to originator set entry, NULL if out of memory
 */
struct olsrv2_originator_entry *
olsrv2_originatorset_add(struct netaddr *originator, uint64_t vtime) {
  struct olsrv2_originator_entry *entry;

  entry = olsrv2_originatorset_get(originator);
  if (entry == NULL) {
    entry = olsr_class_malloc(&_originator_class);
    if (entry == NULL) {
      /* out of memory */
      return NULL;
    }

    /* copy key and append to tree */
    memcpy(&entry->originator, originator, sizeof(*originator));
    entry->_node.key = &entry->originator;
    avl_insert(&olsrv2_originator_tree, &entry->_node);

    /* initialize timer */
    entry->_vtime.info = &_originator_timer;
    entry->_vtime.cb_context = entry;
  }

  /* reset validity time */
  olsr_timer_set(&entry->_vtime, vtime);

  return entry;
}

/**
 * Remove an originator set entry
 * @param originator originator address
 */
void
olsrv2_originatorset_remove(struct netaddr *originator) {
  struct olsrv2_originator_entry *entry;

  entry = olsrv2_originatorset_get(originator);
  if (entry) {
    /* trigger validity timer callback to remove entry */
    _cb_vtime(entry);
  }
}

/**
 * Callback fired when originator set entry must be removed
 * @param ptr
 */
static void
_cb_vtime(void *ptr) {
  struct olsrv2_originator_entry *entry = ptr;

  olsr_timer_stop(&entry->_vtime);
  avl_remove(&olsrv2_originator_tree, &entry->_node);

  olsr_class_free(&_originator_class, entry);
}
