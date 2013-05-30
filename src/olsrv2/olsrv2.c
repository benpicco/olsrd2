
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

#include <errno.h>

#include "common/avl.h"
#include "common/common_types.h"
#include "common/list.h"
#include "common/netaddr.h"
#include "common/netaddr_acl.h"
#include "config/cfg_schema.h"
#include "rfc5444/rfc5444.h"
#include "core/oonf_logging.h"
#include "core/oonf_subsystem.h"
#include "core/os_core.h"
#include "subsystems/oonf_rfc5444.h"
#ifdef USE_TELNET
#include "subsystems/oonf_telnet.h"
#endif
#include "subsystems/oonf_timer.h"

#include "nhdp/nhdp_interfaces.h"

#include "olsrv2/olsrv2.h"
#include "olsrv2/olsrv2_lan.h"
#include "olsrv2/olsrv2_originator.h"
#include "olsrv2/olsrv2_reader.h"
#include "olsrv2/olsrv2_tc.h"
#include "olsrv2/olsrv2_writer.h"

/* definitions */
#define OLSRV2_NAME "olsrv2"
#define _LOCAL_ATTACHED_NETWORK_KEY "lan"

struct _config {
  uint64_t tc_interval;
  uint64_t tc_validity;

  uint64_t f_hold_time;
  uint64_t p_hold_time;
  struct netaddr_acl routable;

  /* configuration for originator set */
  struct netaddr_acl originator_v4_acl;
  struct netaddr_acl originator_v6_acl;
};

struct _lan_data {
  struct nhdp_domain *domain;
  uint32_t metric;
  uint32_t dist;
};

/* prototypes */
static void _early_cfg_init(void);
static int _init(void);
static void _initiate_shutdown(void);
static void _cleanup(void);

static const char *_parse_lan_parameters(struct _lan_data *dst, const char *src);
static void _parse_lan_array(struct cfg_named_section *section, bool add);
static void _cb_generate_tc(void *);

static void _update_originators(void);
static void _cb_if_event(struct oonf_interface_listener *);

static void _cb_cfg_olsrv2_changed(void);
static void _cb_cfg_domain_changed(void);

#ifdef USE_TELNET
/* prototypes */
static enum oonf_telnet_result _cb_topology(struct oonf_telnet_data *con);

/* nhdp telnet commands */
static struct oonf_telnet_command _cmds[] = {
    TELNET_CMD("olsrv2", _cb_topology,
        "OLSRV2 database information command\n"),
};
#endif

/* subsystem definition */
static struct cfg_schema_entry _rt_domain_entries[] = {
  CFG_MAP_BOOL(olsrv2_routing_domain, use_srcip_in_routes, "srcip_routes", "no",
      "Set the source IP of IPv4-routes to a fixed value."),
  CFG_MAP_INT_MINMAX(olsrv2_routing_domain, protocol, "protocol", "100",
      "Protocol number to be used in routing table", 1, 254),
  CFG_MAP_INT_MINMAX(olsrv2_routing_domain, table, "table", "254",
      "Routing table number for routes", 1, 254),
  CFG_MAP_INT_MINMAX(olsrv2_routing_domain, distance, "distance", "2",
      "Metric Distance to be used in routing table", 1, 255),
};

static struct cfg_schema_section _rt_domain_section = {
  .type = CFG_NHDP_DOMAIN_SECTION,
  .mode = CFG_SSMODE_NAMED_WITH_DEFAULT,
  .def_name = CFG_NHDP_DEFAULT_DOMAIN,
  .cb_delta_handler = _cb_cfg_domain_changed,
  .entries = _rt_domain_entries,
  .entry_count = ARRAYSIZE(_rt_domain_entries),
};

static struct cfg_schema_entry _olsrv2_entries[] = {
  CFG_MAP_CLOCK_MIN(_config, tc_interval, "tc_interval", "5.0",
    "Time between two TC messages", 100),
  CFG_MAP_CLOCK_MIN(_config, tc_validity, "tc_validity", "300.0",
    "Validity time of a TC messages", 100),
  CFG_MAP_CLOCK_MIN(_config, f_hold_time, "forward_hold_time", "300.0",
    "Holdtime for forwarding set information", 100),
    CFG_MAP_CLOCK_MIN(_config, p_hold_time, "processing_hold_time", "300.0",
      "Holdtime for processing set information", 100),
  CFG_MAP_ACL_V46(_config, routable, "routable",
      OLSRV2_ROUTABLE_IPV4 OLSRV2_ROUTABLE_IPV6 ACL_DEFAULT_ACCEPT,
    "Filter to decide which addresses are considered routable"),

  CFG_VALIDATE_LAN(_LOCAL_ATTACHED_NETWORK_KEY, "",
    "locally attached network, a combination of an"
    " ip address or prefix followed by an up to three optional parameters"
    " which define link metric cost, hopcount distance and domain of the prefix"
    " ( <metric=...> <dist=...> <domain=...> ).",
    .list = true),

  CFG_MAP_ACL_V4(_config, originator_v4_acl, "originator_v4",
    OLSRV2_ROUTABLE_IPV4 ACL_DEFAULT_ACCEPT,
    "Filter for router IPv4 originator address"),
  CFG_MAP_ACL_V6(_config, originator_v6_acl, "originator_v6",
    OLSRV2_ROUTABLE_IPV6 ACL_DEFAULT_ACCEPT,
    "Filter for router IPv6 originator address"),
};

static struct cfg_schema_section _olsrv2_section = {
  .type = CFG_OLSRV2_SECTION,
  .cb_delta_handler = _cb_cfg_olsrv2_changed,
  .entries = _olsrv2_entries,
  .entry_count = ARRAYSIZE(_olsrv2_entries),
  .next_section = &_rt_domain_section,
};

struct oonf_subsystem olsrv2_subsystem = {
  .name = OLSRV2_NAME,
  .early_cfg_init = _early_cfg_init,
  .init = _init,
  .cleanup = _cleanup,
  .initiate_shutdown = _initiate_shutdown,
  .cfg_section = &_olsrv2_section,
};

static struct _config _olsrv2_config;

/* timer for TC generation */
static struct oonf_timer_info _tc_timer_class = {
  .name = "TC generation",
  .periodic = true,
  .callback = _cb_generate_tc,
};

static struct oonf_timer_entry _tc_timer = {
  .info = &_tc_timer_class,
};

/* global interface listener */
struct oonf_interface_listener _if_listener = {
  .process = _cb_if_event,
};

/* global variables */
static struct oonf_rfc5444_protocol *_protocol;

static uint16_t _ansn;

/* Additional logging sources */
enum oonf_log_source LOG_OLSRV2_R;
enum oonf_log_source LOG_OLSRV2_W;

/**
 * Initialize additional logging sources for NHDP
 */
static void
_early_cfg_init(void) {
  LOG_OLSRV2_R = oonf_log_register_source(OLSRV2_NAME "_r");
  LOG_OLSRV2_W = oonf_log_register_source(OLSRV2_NAME "_w");
}

/**
 * Initialize OLSRV2 subsystem
 * @return -1 if an error happened, 0 otherwise
 */
static int
_init(void) {
  size_t i;

  _protocol = oonf_rfc5444_add_protocol(RFC5444_PROTOCOL, true);
  if (_protocol == NULL) {
    return -1;
  }

  if (olsrv2_writer_init(_protocol)) {
    oonf_rfc5444_remove_protocol(_protocol);
    return -1;
  }

  /* activate interface listener */
  oonf_interface_add_listener(&_if_listener);

  /* activate the rest of the olsrv2 protocol */
  olsrv2_lan_init();
  olsrv2_originator_init();
  olsrv2_reader_init(_protocol);
  olsrv2_tc_init();
  olsrv2_routing_init();

  /* initialize timer */
  oonf_timer_add(&_tc_timer_class);

#ifdef USE_TELNET
  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    oonf_telnet_add(&_cmds[i]);
  }
#endif
  _ansn = os_core_random() & 0xffff;
  return 0;
}

/**
 * Begin shutdown by deactivating reader and writer. Also flush all routes
 */
static void
_initiate_shutdown(void) {
  olsrv2_writer_cleanup();
  olsrv2_reader_cleanup();
  olsrv2_routing_initiate_shutdown();
}

/**
 * Cleanup OLSRV2 subsystem
 */
static void
_cleanup(void) {
#ifdef USE_TELNET
  size_t i;

  /* release telnet commands */
  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    oonf_telnet_remove(&_cmds[i]);
  }
#endif

  /* remove interface listener */
  oonf_interface_remove_listener(&_if_listener);

  /* cleanup configuration */
  netaddr_acl_remove(&_olsrv2_config.routable);
  netaddr_acl_remove(&_olsrv2_config.originator_v4_acl);
  netaddr_acl_remove(&_olsrv2_config.originator_v6_acl);

  /* cleanup all parts of olsrv2 */
  olsrv2_routing_cleanup();
  olsrv2_originator_cleanup();
  olsrv2_tc_cleanup();
  olsrv2_lan_cleanup();

  /* free protocol instance */
  oonf_rfc5444_remove_protocol(_protocol);
}

/**
 * @return interval between two tcs
 */
uint64_t
olsrv2_get_tc_interval(void) {
  return _olsrv2_config.tc_interval;
}

/**
 * @return validity of the local TCs
 */
uint64_t
olsrv2_get_tc_validity(void) {
  return _olsrv2_config.tc_validity;
}

/**
 * @return acl for checking if an address is routable
 */
const struct netaddr_acl *
olsrv2_get_routable(void) {
    return &_olsrv2_config.routable;
}

/**
 * default implementation for rfc5444 processing handling according
 * to MPR settings.
 * @param context
 * @param vtime
 * @return
 */
bool
olsrv2_mpr_shall_process(
    struct rfc5444_reader_tlvblock_context *context, uint64_t vtime) {
  enum oonf_duplicate_result dup_result;
  bool process;
#ifdef OONF_LOG_DEBUG_INFO
  struct netaddr_str buf;
#endif

  /* check if message has originator and sequence number */
  if (!context->has_origaddr || !context->has_seqno) {
    OONF_DEBUG(LOG_OLSRV2, "Do not process message type %u,"
        " originator or sequence number is missing!",
        context->msg_type);
    return false;
  }

  /* check forwarding set */
  dup_result = oonf_duplicate_entry_add(&_protocol->processed_set,
      context->msg_type, &context->orig_addr,
      context->seqno, vtime + _olsrv2_config.f_hold_time);
  process = dup_result == OONF_DUPSET_NEW || dup_result == OONF_DUPSET_NEWEST;

  OONF_DEBUG(LOG_OLSRV2, "Do %sprocess message type %u from %s"
      " with seqno %u (dupset result: %u)",
      process ? "" : "not ",
      context->msg_type,
      netaddr_to_string(&buf, &context->orig_addr),
      context->seqno, dup_result);
  return process;
}

/**
 * default implementation for rfc5444 forwarding handling according
 * to MPR settings.
 * @param context
 * @param vtime
 * @return
 */
bool
olsrv2_mpr_shall_forwarding(
    struct rfc5444_reader_tlvblock_context *context, uint64_t vtime) {
  struct nhdp_interface *interf;
  struct nhdp_laddr *laddr;
  struct nhdp_neighbor *neigh;
  enum oonf_duplicate_result dup_result;
  bool forward;
#ifdef OONF_LOG_DEBUG_INFO
  struct netaddr_str buf;
#endif

  /* check if message has originator and sequence number */
  if (!context->has_origaddr || !context->has_seqno) {
    OONF_DEBUG(LOG_OLSRV2, "Do not forward message type %u,"
        " originator or sequence number is missing!",
        context->msg_type);
    return false;
  }

  /* check forwarding set */
  dup_result = oonf_duplicate_entry_add(&_protocol->forwarded_set,
      context->msg_type, &context->orig_addr,
      context->seqno, vtime + _olsrv2_config.f_hold_time);
  if (dup_result != OONF_DUPSET_NEW && dup_result != OONF_DUPSET_NEWEST) {
    OONF_DEBUG(LOG_OLSRV2, "Do not forward message type %u from %s"
        " with seqno %u (dupset result: %u)",
        context->msg_type,
        netaddr_to_string(&buf, &context->orig_addr),
        context->seqno, dup_result);
    return false;
  }

  /* check input interface */
  if (_protocol->input_interface == NULL) {
    OONF_DEBUG(LOG_OLSRV2, "Do not forward because input interface is not set");
    return false;
  }

  /* checp input source address */
  if (_protocol->input_address == NULL) {
    OONF_DEBUG(LOG_OLSRV2, "Do not forward because input source is not set");
    return false;
  }

  /* get NHDP interface */
  interf = nhdp_interface_get(_protocol->input_interface->name);
  if (interf == NULL) {
    OONF_DEBUG(LOG_OLSRV2, "Do not forward because NHDP does not handle"
        " interface '%s'", _protocol->input_interface->name);
    return false;
  }

  /* get NHDP link address corresponding to source */
  laddr = nhdp_interface_get_link_addr(interf, _protocol->input_address);
  if (laddr == NULL) {
    OONF_DEBUG(LOG_OLSRV2, "Do not forward because source IP %s is"
        " not a direct neighbor",
        netaddr_to_string(&buf, _protocol->input_address));
    return false;
  }

  /* get NHDP neighbor */
  neigh = laddr->link->neigh;

  /* forward if this neighbor has selected us as a flooding MPR */
  forward = neigh->local_is_flooding_mpr && neigh->symmetric > 0;
  OONF_DEBUG(LOG_OLSRV2, "Do %sforward message type %u from %s"
      " with seqno %u",
      forward ? "" : "not ",
      context->msg_type,
      netaddr_to_string(&buf, &context->orig_addr),
      context->seqno);
  return forward;
}

/**
 * default implementation for rfc5444 forwarding target selection
 * according to MPR settings
 * @param rfc5444_target
 * @return
 */
bool
olsrv2_mpr_forwarding_selector(struct rfc5444_writer_target *rfc5444_target) {
  struct oonf_rfc5444_target *target;
  struct nhdp_interface *interf;
  bool is_ipv4, flood;
#ifdef OONF_LOG_DEBUG_INFO
  struct netaddr_str buf;
#endif
  target = container_of(rfc5444_target, struct oonf_rfc5444_target, rfc5444_target);

  /* test if this is the ipv4 multicast target */
  is_ipv4 = target == target->interface->multicast4;

  /* only forward to multicast targets */
  if (!is_ipv4 && target != target->interface->multicast6) {
    return false;
  }

  /* get NHDP interface for target */
  interf = nhdp_interface_get(target->interface->name);
  if (interf == NULL) {
    OONF_DEBUG(LOG_OLSRV2, "Do not forward message"
        " to interface %s: its unknown to NHDP",
        target->interface->name);
    return NULL;
  }

  /* lookup flooding cache in NHDP interface */
  if (is_ipv4) {
    flood = interf->use_ipv4_for_flooding;
  }
  else {
    flood =  interf->use_ipv6_for_flooding;
  }

  OONF_DEBUG(LOG_OLSRV2, "Flooding to target %s: %s",
      netaddr_to_string(&buf, &target->dst), flood ? "yes" : "no");

  return flood;
}

/**
 * @return current answer set number for local topology database
 */
uint16_t
olsrv2_get_ansn(void) {
  return _ansn;
}

/**
 * Update answer set number if metric of a neighbor changed since last update.
 * @return new answer set number, might be the same if no metric changed.
 */
uint16_t
olsrv2_update_ansn(void) {
  struct nhdp_domain *domain;
  bool changed;

  changed = false;
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    if (domain->metric_changed) {
      changed = true;
      domain->metric_changed = false;
    }
  }

  if (changed) {
    _ansn++;
  }
  return _ansn;
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
olsrv2_validate_lan(const struct cfg_schema_entry *entry,
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
  if (data.dist > 255) {
    cfg_append_printable_line(out,
        "Distance %u for prefix %s must be between 0 and 255", data.dist, buf.buf);
    return -1;
  }

  return 0;
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
  unsigned ext;

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
      ext = strtoul(&buffer[7], NULL, 10);
      if ((ext == 0 && errno != 0) || ext > 255) {
        return "an illegal domain parameter";
      }
      dst->domain = nhdp_domain_get_by_ext(ext);
      if (dst->domain == NULL) {
        return "an unknown domain extension number";
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
 * Takes a named configuration section, extracts the attached network
 * array and apply it
 * @param section pointer to configuration section.
 * @param add true if new lan entries should be created, false if
 *   existing entries should be removed.
 */
static void
_parse_lan_array(struct cfg_named_section *section, bool add) {
  struct netaddr_str addr_buf;
  struct netaddr prefix;
  struct _lan_data data;

  const char *value, *ptr;
  struct cfg_entry *entry;

  if (section == NULL) {
    return;
  }

  entry = cfg_db_get_entry(section, _LOCAL_ATTACHED_NETWORK_KEY);
  if (entry == NULL) {
    return;
  }

  FOR_ALL_STRINGS(&entry->val, value) {
    /* extract data */
    ptr = str_cpynextword(addr_buf.buf, value, sizeof(addr_buf));
    if (netaddr_from_string(&prefix, addr_buf.buf)) {
      continue;
    }
    if (_parse_lan_parameters(&data, ptr)) {
      continue;
    }

    if (add) {
      olsrv2_lan_add(data.domain, &prefix, data.metric, data.dist);
    }
    else {
      olsrv2_lan_remove(data.domain, &prefix);
    }
  }
}

/**
 * Callback to trigger normal tc generation with timer
 * @param ptr
 */
static void
_cb_generate_tc(void *ptr __attribute__((unused))) {
  olsrv2_writer_send_tc();
}

#ifdef USE_TELNET
/**
 * Telnet command to output topology
 * @param con
 * @return
 */
static enum oonf_telnet_result
_cb_topology(struct oonf_telnet_data *con) {
  /* TODO: move this command (or a similar one) to a plugin */
  struct olsrv2_tc_node *node;
  struct olsrv2_tc_edge *edge;
  struct olsrv2_tc_attachment *end;
  struct nhdp_domain *domain;
  struct netaddr_str nbuf;
  struct fraction_str tbuf;

  avl_for_each_element(&olsrv2_tc_tree, node, _originator_node) {
    abuf_appendf(con->out, "Node originator %s: vtime=%s ansn=%u\n",
        netaddr_to_string(&nbuf, &node->target.addr),
        oonf_clock_toIntervalString(&tbuf,
            oonf_timer_get_due(&node->_validity_time)),
        node->ansn);

    avl_for_each_element(&node->_edges, edge, _node) {
      abuf_appendf(con->out, "\tlink to %s%s: (ansn=%u)\n",
          netaddr_to_string(&nbuf, &edge->dst->target.addr),
          edge->virtual ? " (virtual)" : "",
          edge->ansn);

      list_for_each_element(&nhdp_domain_list, domain, _node) {
        abuf_appendf(con->out, "\t\tmetric '%s': %d\n",
            domain->metric->name, edge->cost[domain->index]);
      }
    }

    avl_for_each_element(&node->_endpoints, end, _src_node) {
      abuf_appendf(con->out, "\tlink to endpoint %s: (ansn=%u)\n",
          netaddr_to_string(&nbuf, &end->dst->target.addr),
          end->ansn);

        list_for_each_element(&nhdp_domain_list, domain, _node) {
          abuf_appendf(con->out, "\t\tmetric '%s': %d\n",
              domain->metric->name, end->cost[domain->index]);
        }
    }
  }

  return TELNET_RESULT_ACTIVE;
}
#endif

/**
 * Check if current originators are still valid and
 * lookup new one if necessary.
 */
static void
_update_originators(void) {
  const struct netaddr *originator_v4, *originator_v6;
  struct nhdp_interface *n_interf;
  struct oonf_interface *interf;
  struct netaddr new_v4, new_v6;
  bool keep_v4, keep_v6;
  size_t i;
#ifdef OONF_LOG_DEBUG_INFO
  struct netaddr_str buf;
#endif

  OONF_DEBUG(LOG_OLSRV2, "Updating OLSRV2 originators");

  originator_v4 = olsrv2_originator_get(AF_INET);
  originator_v6 = olsrv2_originator_get(AF_INET6);

  keep_v4 = false;
  keep_v6 = false;

  netaddr_invalidate(&new_v4);
  netaddr_invalidate(&new_v6);

  avl_for_each_element(&nhdp_interface_tree, n_interf, _node) {
    interf = nhdp_interface_get_coreif(n_interf);

    /* check if originator is still valid */
    for (i=0; i<interf->data.addrcount; i++) {
      struct netaddr *addr = &interf->data.addresses[i];

      keep_v4 |= netaddr_cmp(originator_v4, addr) == 0;
      keep_v6 |= netaddr_cmp(originator_v6, addr) == 0;

      if (!keep_v4 && netaddr_get_address_family(&new_v4) == AF_UNSPEC
          && netaddr_get_address_family(addr) == AF_INET
          && netaddr_acl_check_accept(&_olsrv2_config.originator_v4_acl, addr)) {
        memcpy(&new_v4, addr, sizeof(new_v4));
      }
      if (!keep_v6 && netaddr_get_address_family(&new_v6) == AF_UNSPEC
          && netaddr_get_address_family(addr) == AF_INET6
          && netaddr_acl_check_accept(&_olsrv2_config.originator_v6_acl, addr)) {
        memcpy(&new_v6, addr, sizeof(new_v6));
      }
    }
  }

  if (!keep_v4) {
    OONF_DEBUG(LOG_OLSRV2, "Set IPv4 originator to %s",
        netaddr_to_string(&buf, &new_v4));
    olsrv2_originator_set(&new_v4);
  }

  if (!keep_v6) {
    OONF_DEBUG(LOG_OLSRV2, "Set IPv6 originator to %s",
        netaddr_to_string(&buf, &new_v6));
    olsrv2_originator_set(&new_v6);
  }
}

/**
 * Callback for interface events
 * @param listener pointer to interface listener
 */
static void
_cb_if_event(struct oonf_interface_listener *listener __attribute__((unused))) {
  _update_originators();
}

/**
 * Callback fired when olsrv2 section changed
 */
static void
_cb_cfg_olsrv2_changed(void) {
  if (cfg_schema_tobin(&_olsrv2_config, _olsrv2_section.post,
      _olsrv2_entries, ARRAYSIZE(_olsrv2_entries))) {
    OONF_WARN(LOG_OLSRV2, "Cannot convert OLSRV2 configuration.");
    return;
  }

  /* set tc timer interval */
  oonf_timer_set(&_tc_timer, _olsrv2_config.tc_interval);

  /* check if we have to change the originators */
  _update_originators();

  /* run through all pre-update LAN entries and remove them */
  _parse_lan_array(_olsrv2_section.pre, false);

  /* run through all post-update LAN entries and add them */
  _parse_lan_array(_olsrv2_section.post, true);
}

/**
 * Callback fired when domain section changed
 */
static void
_cb_cfg_domain_changed(void) {
  struct olsrv2_routing_domain rtdomain;
  struct nhdp_domain *domain;
  char *error = NULL;
  int ext;

  ext = strtol(_rt_domain_section.section_name, &error, 10);
  if (error != NULL && *error != 0) {
    /* illegal domain name */
    return;
  }

  if (ext < 0 || ext > 255) {
    /* name out of range */
    return;
  }

  domain = nhdp_domain_add(ext);
  if (domain == NULL) {
    return;
  }

  if (cfg_schema_tobin(&rtdomain, _rt_domain_section.post,
      _rt_domain_entries, ARRAYSIZE(_rt_domain_entries))) {
    OONF_WARN(LOG_NHDP, "Cannot convert OLSRV2 routing domain parameters.");
    return;
  }

  olsrv2_routing_set_domain_parameter(domain, &rtdomain);
}
