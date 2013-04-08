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

struct _lan_data {
  uint32_t metric;
  uint32_t dist;
  uint32_t domain;
};

static void _remove(struct olsrv2_lan_entry *entry);
static const char *_parse_lan_parameters(struct _lan_data *dst, const char *src);
static void _cb_cfg_changed(void);

/* olsrv2 LAN configuration */
static struct cfg_schema_section _olsrv2_section = {
  .type = CFG_OLSRV2_SECTION,
  .cb_delta_handler = _cb_cfg_changed,
};

static struct cfg_schema_entry _olsrv2_entries[] = {
  CFG_VALIDATE_LAN(LOCAL_ATTACHED_NETWORK_KEY, "",
      "locally attached network, a combination of an"
      " ip address or prefix followed by an up to three optional parameters"
      " which define link metric cost, hopcount distance and domain of the prefix"
      " ( <metric=...> <dist=...> <domain=...> ).",
      .list = true),
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

    /* initialize linkcost and distance */
    for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
      entry->outgoing_metric[i] = 0;
      entry->distance[i] = 2;
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
  struct _lan_data data;
  const char *ptr, *result;

  if (value == NULL) {
    cfg_schema_validate_netaddr(entry, section_name, value, out);
    cfg_append_printable_line(out,
        "    This value is followed by a list of three optional parameters.");
    cfg_append_printable_line(out,
        "    - 'metric=<m>' the link metric of the LAN (between %u and %u)."
        " The default is 0.", RFC5444_METRIC_MIN, RFC5444_METRIC_MAX);
    cfg_append_printable_line(out,
        "    - 'domain=<d>' the domain of the LAN (between 0 and 255)."
        " The default is 0.");
    cfg_append_printable_line(out,
        "    - 'dist=<d>' the hopcount distance of the LAN (between 0 and 255)."
        " The default is 2.");
    return 0;
  }

  ptr = str_cpynextword(buf.buf, value, sizeof(buf));
  if (cfg_schema_validate_netaddr(entry, section_name, value, out)) {
    /* check prefix first */
    return -1;
  }

  result = _parse_lan_parameters(&data, ptr);
  if (result) {
    cfg_append_printable_line(out, "Value '%s' for entry '%s'"
        " in section %s has %s",
        value, entry->key.entry, section_name, result);
    return -1;
  }

  if (data.metric < RFC5444_METRIC_MIN || data.metric > RFC5444_METRIC_MAX) {
    cfg_append_printable_line(out, "Metric %u for prefix %s must be between %u and %u",
        data.metric, buf.buf, RFC5444_METRIC_MIN, RFC5444_METRIC_MAX);
    return -1;
  }
  if (data.domain > 255) {
    cfg_append_printable_line(out,
        "Domain %u for prefix %s must be between 0 and 255", data.domain, buf.buf);
    return -1;
  }
  if (data.dist > 255) {
    cfg_append_printable_line(out,
        "Distance %u for prefix %s must be between 0 and 255", data.dist, buf.buf);
    return -1;
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
 * Parse parameters of lan prefix string
 * @param dst pointer to data structure to store results.
 * @param src source string
 * @return NULL if parser worked without an error, a pointer
 *   to the suffix of the error message otherwise.
 */
static const char *
_parse_lan_parameters(struct _lan_data *dst, const char *src) {
  char buffer[64];
  const char *ptr, *next;

  ptr = src;
  while (ptr != NULL) {
    next = str_cpynextword(buffer, ptr, sizeof(buffer));

    if (strncasecmp(buffer, "metric=", 7) == 0) {
      dst->metric = strtoul(&buffer[7], NULL, 0);
      if (dst->metric == 0 && errno != 0) {
        return "an illegal metric parameter";
      }
    }
    else if (strncasecmp(buffer, "domain=", 7) == 0) {
      dst->domain = strtoul(&buffer[7], NULL, 10);
      if (dst->domain == 0 && errno != 0) {
        return "an illegal domain parameter";
      }
    }
    else if (strncasecmp(buffer, "dist=", 5) == 0) {
      dst->dist = strtoul(&buffer[5], NULL, 10);
      if (dst->dist == 0 && errno != 0) {
        return "an illegal distance parameter";
      }
    }
    else {
      return "an unknown parameter";
    }
    ptr = next;
  }
  return NULL;
}

/**
 * Callback handling LAN configuration changes
 */
static void
_cb_cfg_changed(void) {
  struct netaddr_str addr_buf;
  struct olsrv2_lan_entry *lan, *lan_it;
  struct cfg_entry *entry;
  struct netaddr prefix;
  const char *ptr;
  char *value;

  struct _lan_data data;

  /* remove all old entries */
  avl_for_each_element_safe(&olsrv2_lan_tree, lan, _node, lan_it) {
    _remove(lan);
  }

  /* run through all post-update entries */
  if (_olsrv2_section.post) {
    entry = cfg_db_get_entry(_olsrv2_section.post, LOCAL_ATTACHED_NETWORK_KEY);

    if (entry) {
      FOR_ALL_STRINGS(&entry->val, value) {
        /* extract data */
        ptr = str_cpynextword(addr_buf.buf, value, sizeof(addr_buf));
        if (netaddr_from_string(&prefix, addr_buf.buf)) {
          continue;
        }
        if (_parse_lan_parameters(&data, ptr)) {
          continue;
        }

        /* add new entries if necessary */
        lan = olsrv2_lan_add(&prefix);
        if (!lan) {
          return;
        }

        lan->distance[data.domain] = data.dist;
        lan->outgoing_metric[data.domain] = data.metric;
      }
    }
  }
}
