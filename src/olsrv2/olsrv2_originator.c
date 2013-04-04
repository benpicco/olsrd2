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
#include "core/olsr_netaddr_acl.h"
#include "core/olsr_timer.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp.h"

#include "olsrv2/olsrv2_originator.h"
#include "olsrv2/olsrv2.h"

/* definitions */
struct _config {
  uint64_t o_hold_time;
  struct netaddr v4, v6;
};

struct _originator {
  struct netaddr addr;
  bool custom;
};

/* prototypes */
static struct olsrv2_originator_set_entry *_remember_removed_originator(
    struct netaddr *originator, uint64_t vtime);
static void _set_originator(
    struct _originator *, const struct netaddr *addr, bool custom);
static void _cb_originator_entry_vtime(void *);
static void _cb_cfg_changed(void);

/* olsrv2 configuration */
static struct cfg_schema_section _originator_section = {
  .type = CFG_OLSRV2_SECTION,
  .cb_delta_handler = _cb_cfg_changed,
};

static struct cfg_schema_entry _originator_entries[] = {
  CFG_MAP_NETADDR_V4(_config, v4, "originator_v4", "0.0.0.0/0",
      "Filter for router IPv4 originator address", true, false),
  CFG_MAP_NETADDR_V6(_config, v6, "originator_v6", "::/0",
      "Filter for router IPv6 originator address", true, false),
  CFG_MAP_CLOCK_MIN(_config, o_hold_time, "originator_hold_time", "30.0",
    "Validity time for former Originator addresses", 100),
};

/* originator set class and timer */
static struct olsr_class _originator_entry_class = {
  .name = "OLSRv2 originator set",
  .size = sizeof(struct olsrv2_originator_set_entry),
};

static struct olsr_timer_info _originator_entry_timer = {
  .name = "OLSRv2 originator set vtime",
  .callback = _cb_originator_entry_vtime,
};

/* global tree of originator set entries */
struct avl_tree olsrv2_originator_set_tree;

/* originator configuration */
static struct _config _olsrv2_config;
static struct _originator _originator_v4, _originator_v6;

/**
 * Initialize olsrv2 originator set
 */
void
olsrv2_originator_init(void) {
  /* add configuration for olsrv2 section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_originator_section,
      _originator_entries, ARRAYSIZE(_originator_entries));

  /* initialize class and timer */
  olsr_class_add(&_originator_entry_class);
  olsr_timer_add(&_originator_entry_timer);

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
  olsr_timer_remove(&_originator_entry_timer);
  olsr_class_remove(&_originator_entry_class);

  /* cleanup configuration */
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_originator_section);
}

/**
 * @return current originator address
 */
const struct netaddr *
olsrv2_originator_get(int af_type) {
  if (af_type == AF_INET) {
    return &_originator_v4.addr;
  }
  return &_originator_v6.addr;
}

/**
 * Sets a new custom originator address
 * @param originator originator address
 */
void
olsrv2_originator_set(const struct netaddr *addr) {
  if (netaddr_get_address_family(addr) == AF_INET) {
    _set_originator(&_originator_v4, addr, true);
  }
  else if (netaddr_get_address_family(addr) == AF_INET6) {
    _set_originator(&_originator_v6, addr, true);
  }
}

/**
 * Resets the originator to the configured value
 */
void
olsrv2_originator_reset(int af_type) {
  if (af_type == AF_INET) {
    _set_originator(&_originator_v4, &_olsrv2_config.v4, false);
  }
  else if (af_type == AF_INET6) {
    _set_originator(&_originator_v6, &_olsrv2_config.v6, false);
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
    entry = olsr_class_malloc(&_originator_entry_class);
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
  olsr_timer_set(&entry->_vtime, vtime);

  return entry;
}

/**
 * Sets the originator address to a new value
 * @param originator new originator
 */
static
void _set_originator(struct _originator *orig, const struct netaddr *addr, bool custom) {
  struct olsrv2_originator_set_entry *entry;

  if (netaddr_get_address_family(&orig->addr) != AF_UNSPEC) {
    /* add old originator to originator set */
    _remember_removed_originator(&orig->addr, _olsrv2_config.o_hold_time);
  }

  memcpy(&orig->addr, addr, sizeof(*addr));

  /* remove new originator from set */
  entry = olsrv2_originator_get_entry(addr);
  if (entry) {
    _cb_originator_entry_vtime(entry);
  }

  /* remember if this is a custom originator */
  orig->custom = custom;

  /* update NHDP originator */
  nhdp_set_originator(addr);
}

/**
 * Callback fired when originator set entry must be removed
 * @param ptr pointer to originator set
 */
static void
_cb_originator_entry_vtime(void *ptr) {
  struct olsrv2_originator_set_entry *entry = ptr;

  olsr_timer_stop(&entry->_vtime);
  avl_remove(&olsrv2_originator_set_tree, &entry->_node);

  olsr_class_free(&_originator_entry_class, entry);
}

/**
 * Callback fired when configuration changed
 */
static void
_cb_cfg_changed(void) {
  if (cfg_schema_tobin(&_olsrv2_config, _originator_section.post,
      _originator_entries, ARRAYSIZE(_originator_entries))) {
    OLSR_WARN(LOG_OLSRV2, "Cannot convert OLSRv2 configuration.");
    return;
  }

  if (!_originator_v4.custom) {
    /* apply new originator */
    olsrv2_originator_reset(AF_INET);
  }
  if (!_originator_v6.custom) {
    /* apply new originator */
    olsrv2_originator_reset(AF_INET6);
  }
}
