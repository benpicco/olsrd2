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
#include "core/olsr_interface.h"
#include "core/olsr_logging.h"
#include "common/netaddr_acl.h"
#include "core/olsr_timer.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp.h"

#include "olsrv2/olsrv2_originator.h"
#include "olsrv2/olsrv2.h"

/* definitions */
struct _config {
  uint64_t o_hold_time;
  struct netaddr_acl v4_acl, v6_acl;
};

/* prototypes */
static struct olsrv2_originator_set_entry *_remember_removed_originator(
    struct netaddr *originator, uint64_t vtime);
static void _set_originator(int af_type, struct netaddr *setting, const struct netaddr *new);
static void _update_originators(void);
static void _cb_if_event(struct olsr_interface_listener *);
static void _cb_originator_entry_vtime(void *);
static void _cb_cfg_changed(void);

/* olsrv2 configuration */
static struct cfg_schema_entry _originator_entries[] = {
  CFG_MAP_ACL_V4(_config, v4_acl, "originator_v4",
      OLSRV2_ROUTABLE_IPV4 ACL_DEFAULT_ACCEPT,
      "Filter for router IPv4 originator address"),
  CFG_MAP_ACL_V6(_config, v6_acl, "originator_v6",
      OLSRV2_ROUTABLE_IPV6 ACL_DEFAULT_ACCEPT,
      "Filter for router IPv6 originator address"),
  CFG_MAP_CLOCK_MIN(_config, o_hold_time, "originator_hold_time", "30.0",
    "Validity time for former Originator addresses", 100),
};

static struct cfg_schema_section _originator_section = {
  .type = CFG_OLSRV2_SECTION,
  .cb_delta_handler = _cb_cfg_changed,
  .entries = _originator_entries,
  .entry_count = ARRAYSIZE(_originator_entries),
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

/* global interface listener */
struct olsr_interface_listener _if_listener = {
  .process = _cb_if_event,
};

/* global tree of originator set entries */
struct avl_tree olsrv2_originator_set_tree;

/* originator configuration */
static struct _config _originator_config;

static struct netaddr _originator_v4, _originator_v6;

/**
 * Initialize olsrv2 originator set
 */
void
olsrv2_originator_init(void) {
  /* add configuration for olsrv2 section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_originator_section);

  /* initialize class and timer */
  olsr_class_add(&_originator_entry_class);
  olsr_timer_add(&_originator_entry_timer);

  /* initialize global originator tree */
  avl_init(&olsrv2_originator_set_tree, avl_comp_netaddr, false);

  /* activate interface listener */
  olsr_interface_add_listener(&_if_listener);
}

/**
 * Cleanup all resources allocated by orignator set
 */
void
olsrv2_originator_cleanup(void) {
  struct olsrv2_originator_set_entry *entry, *e_it;

  /* remove interface listener */
  olsr_interface_remove_listener(&_if_listener);

  /* remove ACL in configuration */
  netaddr_acl_remove(&_originator_config.v4_acl);
  netaddr_acl_remove(&_originator_config.v6_acl);

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
    if (netaddr_get_address_family(&_originator_v4) == AF_UNSPEC) {
      _update_originators();
    }
    return &_originator_v4;
  }

  if (netaddr_get_address_family(&_originator_v6) == AF_UNSPEC) {
    _update_originators();
  }
  return &_originator_v6;
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
 * Sets the originator address to a new value.
 * Parameter af_type is necessary for the case when both
 * current and new setting are AF_UNSPEC.
 *
 * @param af_type address family type of the originator
 *   (AF_INET or AF_INET6)
 * @param setting pointer to the storage of the originator
 * @param originator new originator
 */
static
void _set_originator(int af_type, struct netaddr *setting, const struct netaddr *new) {
  struct olsrv2_originator_set_entry *entry;

  if (netaddr_get_address_family(setting) != AF_UNSPEC) {
    /* add old originator to originator set */
    _remember_removed_originator(setting, _originator_config.o_hold_time);
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
 * Check if current originators are still valid and
 * lookup new one if necessary.
 */
static void
_update_originators(void) {
  struct olsr_interface *interf;
  struct netaddr new_v4, new_v6;
  bool keep_v4, keep_v6;
  size_t i;
  struct netaddr_str buf;

  OLSR_DEBUG(LOG_OLSRV2, "Updating OLSRv2 originators");

  keep_v4 = false;
  keep_v6 = false;

  netaddr_invalidate(&new_v4);
  netaddr_invalidate(&new_v6);

  avl_for_each_element(&olsr_interface_tree, interf, _node) {
    /* check if originator is still valid */
    for (i=0; i<interf->data.addrcount; i++) {
      struct netaddr *addr = &interf->data.addresses[i];

      keep_v4 |= netaddr_cmp(&_originator_v4, addr) == 0;
      keep_v6 |= netaddr_cmp(&_originator_v6, addr) == 0;

      if (!keep_v4 && netaddr_get_address_family(&new_v4) == AF_UNSPEC
          && netaddr_get_address_family(addr) == AF_INET
          && netaddr_acl_check_accept(&_originator_config.v4_acl, addr)) {
        memcpy(&new_v4, addr, sizeof(new_v4));
      }
      if (!keep_v6 && netaddr_get_address_family(&new_v6) == AF_UNSPEC
          && netaddr_get_address_family(addr) == AF_INET6
          && netaddr_acl_check_accept(&_originator_config.v6_acl, addr)) {
        memcpy(&new_v6, addr, sizeof(new_v6));
      }
    }
  }

  if (!keep_v4) {
    OLSR_DEBUG(LOG_OLSRV2, "Set IPv4 originator to %s",
        netaddr_to_string(&buf, &new_v4));
    _set_originator(AF_INET, &_originator_v4, &new_v4);
  }

  if (!keep_v6) {
    OLSR_DEBUG(LOG_OLSRV2, "Set IPv6 originator to %s",
        netaddr_to_string(&buf, &new_v6));
    _set_originator(AF_INET6, &_originator_v6, &new_v6);
  }
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
 * Callback for interface events
 * @param listener pointer to interface listener
 */
static void
_cb_if_event(struct olsr_interface_listener *listener __attribute__((unused))) {
  _update_originators();
}

/**
 * Callback fired when configuration changed
 */
static void
_cb_cfg_changed(void) {
  if (cfg_schema_tobin(&_originator_config, _originator_section.post,
      _originator_entries, ARRAYSIZE(_originator_entries))) {
    OLSR_WARN(LOG_OLSRV2, "Cannot convert OLSRv2 originator configuration.");
    return;
  }

  /*
   * during first initialization this might not work because interfaces
   * may not be completely initialized. But the interface event timer will
   * fire a moment later, doing the initialization again.
   */
  _update_originators();
}
