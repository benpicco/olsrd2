/*
 * olsrv2_originator_set.c
 *
 *  Created on: Mar 15, 2013
 *      Author: rogge
 */

#include <errno.h>

#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/common_types.h"
#include "common/netaddr.h"
#include "config/cfg.h"
#include "config/cfg_schema.h"
#include "core/olsr_logging.h"
#include "core/olsr_subsystem.h"
#include "core/olsr_class.h"
#include "rfc5444/rfc5444.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp.h"
#include "olsrv2/olsrv2.h"
#include "olsrv2/olsrv2_lan.h"

#define LOCAL_ATTACHED_NETWORK_KEY "lan"

static void _remove(struct olsrv2_lan_entry *entry);
static void _cb_cfg_changed(void);

/* olsrv2 LAN configuration */
static struct cfg_schema_section _olsrv2_section = {
  .type = CFG_OLSRV2_SECTION,
  .cb_delta_handler = _cb_cfg_changed,
};

static struct cfg_schema_entry _olsrv2_entries[] = {
  CFG_VALIDATE_LAN(LOCAL_ATTACHED_NETWORK_KEY, "",
      "locally attached network, a combination of an"
      " ip address or prefix followed by an absolute link cost value"
      " (in decimal or hex).", .list = true),
};

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

  /* add LAN configuration to OLSRv2 section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_olsrv2_section, _olsrv2_entries,
      ARRAYSIZE(_olsrv2_entries));
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

  /* remove schema section */
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_olsrv2_section);

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

    /* initialize metric with default */
    for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
      entry->outgoing_metric[i] = NHDP_METRIC_DEFAULT;
    }
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
 * Schema entry validator for an attached network.
 * See CFG_VALIDATE_ACL_*() macros.
 * @param entry pointer to schema entry
 * @param section_name name of section type and name
 * @param value value of schema entry
 * @param out pointer to autobuffer for validator output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
olsrv2_lan_validate(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out) {
  struct netaddr_str buf;
  const char *cost_ptr;
  uint32_t cost;

  if (value == NULL) {
    cfg_schema_validate_netaddr(entry, section_name, value, out);
    cfg_append_printable_line(out, "    This value is followed by an absolute link cost"
        "between %u and %u in decimal or hexadecimal",
        RFC5444_METRIC_MIN, RFC5444_METRIC_MAX);
    return 0;
  }

  cost_ptr = str_cpynextword(buf.buf, value, sizeof(buf));

  if (cfg_schema_validate_netaddr(entry, section_name, value, out)) {
    /* check prefix first */
    return -1;
  }

  if (cost_ptr) {
    errno = 0;
    cost = strtol(cost_ptr, NULL, 0);

    if (errno) {
      cfg_append_printable_line(out, "Illegal linkcost argument for prefix %s: %s",
          buf.buf, cost_ptr);
      return -1;
    }

    if (cost < RFC5444_METRIC_MIN) {
      cfg_append_printable_line(out, "Linkcost for prefix %s must not be smaller than %u/%x",
          buf.buf, RFC5444_METRIC_MIN, RFC5444_METRIC_MIN);
      return -1;
    }
    if (cost > RFC5444_METRIC_MAX) {
      cfg_append_printable_line(out, "Linkcost for prefix %s must not be larger than %u/%x",
          buf.buf, RFC5444_METRIC_MAX, RFC5444_METRIC_MAX);
      return -1;
    }
  }
  return 0;
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

/**
 * Callback handling LAN configuration changes
 */
static void
_cb_cfg_changed(void) {
  struct olsrv2_lan_entry *lan;
  struct cfg_entry *entry;
  struct netaddr_str buf;
  struct netaddr prefix;
  char *value;
  const char *cost_ptr;
  uint32_t cost;
  int i;

  /* mark existing entries as 'old' */
  avl_for_each_element(&olsrv2_lan_tree, lan, _node) {
    lan->_new = false;
  }

  /* run through all new entries */
  if (_olsrv2_section.post) {
    entry = cfg_db_get_entry(_olsrv2_section.post, LOCAL_ATTACHED_NETWORK_KEY);

    if (entry) {
      FOR_ALL_STRINGS(&entry->val, value) {
        /* extract data */
        cost_ptr = str_cpynextword(buf.buf, value, sizeof(buf));
        if (netaddr_from_string(&prefix, buf.buf)) {
          continue;
        }

        if (cost_ptr) {
          cost = strtol(cost_ptr, NULL, 0);
        }
        else {
          /* default cost (similar to HNAs in OLSRv1) */
          cost = 0;
        }

        /* add new entries if necessary */
        lan = olsrv2_lan_add(&prefix);
        if (!lan) {
          return;
        }
      }
    }

    for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
      lan->outgoing_metric[i] = cost;
    }
    lan->_new = true;
  }

  /* run through all old entries */
  if (_olsrv2_section.pre) {
    entry = cfg_db_get_entry(_olsrv2_section.pre, LOCAL_ATTACHED_NETWORK_KEY);

    if (entry) {
      FOR_ALL_STRINGS(&entry->val, value) {
        /* extract data */
        cost_ptr = str_cpynextword(buf.buf, value, sizeof(buf));
        if (netaddr_from_string(&prefix, buf.buf)) {
          continue;
        }

        lan = olsrv2_lan_get(&prefix);
        if (lan != NULL && !lan->_new) {
          /* remove old entries that are not also in new entries */
          _remove(lan);
        }
      }
    }
  }
}
