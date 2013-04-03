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
#include "config/cfg_schema.h"
#include "core/olsr_class.h"
#include "core/olsr_logging.h"
#include "core/olsr_timer.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp.h"

#include "olsrv2/olsrv2_originator_set.h"
#include "olsrv2/olsrv2.h"

/* definitions */
struct _config {
  struct netaddr originator;
  uint64_t o_hold_time;
};

/* prototypes */
static void _set_originator(const struct netaddr *originator);
static void _cb_vtime(void *);
static void _cb_cfg_changed(void);

/* olsrv2 configuration */
static struct cfg_schema_section _olsrv2_section = {
  .type = CFG_OLSRV2_SECTION,
  .cb_delta_handler = _cb_cfg_changed,
};

static struct cfg_schema_entry _olsrv2_entries[] = {
  CFG_MAP_NETADDR(_config, originator, "originator", NULL,
      "Originator address for Routing", false, false),
  CFG_MAP_CLOCK_MIN(_config, o_hold_time, "originator_hold_time", "30.0",
    "Validity time for former Originator addresses", 100),
};

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

/* originator configuration */
static struct _config _olsrv2_config;
static struct netaddr *_originator;
static bool _custom_originator;

/**
 * Initialize olsrv2 originator set
 */
void
olsrv2_originatorset_init(void) {
  /* add configuration for olsrv2 section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_olsrv2_section,
      _olsrv2_entries, ARRAYSIZE(_olsrv2_entries));

  /* initialize class and timer */
  olsr_class_add(&_originator_class);
  olsr_timer_add(&_originator_timer);

  /* initialize global originator tree */
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

  /* cleanup configuration */
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_olsrv2_section);
}

/**
 * @return current originator address
 */
const struct netaddr *
olsrv2_get_originator(void) {
  return _originator;
}

/**
 * Sets a new custom originator address
 * @param originator originator address
 */
void
olsrv2_set_originator(const struct netaddr *originator) {
  _custom_originator = true;
  _set_originator(originator);
}

/**
 * Resets the originator to the configured value
 */
void
olsrv2_reset_originator(void) {
  _custom_originator = false;
  _set_originator(&_olsrv2_config.originator);
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
 * Sets the originator address to a new value
 * @param originator new originator
 */
static
void _set_originator(const struct netaddr *originator) {
  if (netaddr_get_address_family(_originator) != AF_UNSPEC) {
    /* add old originator to originator set */
    olsrv2_originatorset_add(_originator, _olsrv2_config.o_hold_time);
  }

  memcpy(&_originator, originator, sizeof(_originator));

  /* remove new originator from set */
  olsrv2_originatorset_remove(_originator);

  /* update NHDP originator */
  nhdp_set_originator(_originator);
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

/**
 * Callback fired when configuration changed
 */
static void
_cb_cfg_changed(void) {
  if (cfg_schema_tobin(&_olsrv2_config, _olsrv2_section.post,
      _olsrv2_entries, ARRAYSIZE(_olsrv2_entries))) {
    OLSR_WARN(LOG_OLSRV2, "Cannot convert OLSRv2 configuration.");
    return;
  }

  if (!_custom_originator) {
    /* apply new originator */
    olsrv2_reset_originator();
  }
}
