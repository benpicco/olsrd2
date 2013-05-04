
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

#include "common/common_types.h"
#include "common/netaddr.h"
#include "config/cfg_schema.h"
#include "rfc5444/rfc5444.h"
#include "core/olsr_logging.h"
#include "common/netaddr_acl.h"
#include "core/olsr_subsystem.h"
#include "core/olsr_timer.h"
#include "tools/olsr_cfg.h"
#include "tools/olsr_rfc5444.h"
#include "tools/olsr_telnet.h"

#include "nhdp/nhdp_interfaces.h"

#include "olsrv2/olsrv2.h"
#include "olsrv2/olsrv2_lan.h"
#include "olsrv2/olsrv2_originator.h"
#include "olsrv2/olsrv2_reader.h"
#include "olsrv2/olsrv2_tc.h"
#include "olsrv2/olsrv2_writer.h"

/* definitions */
#define _LOG_OLSRV2_NAME "olsrv2"
#define _LOCAL_ATTACHED_NETWORK_KEY "lan"

struct _config {
  uint64_t tc_interval;
  uint64_t tc_validity;

  uint64_t f_hold_time;
  uint64_t p_hold_time;
  struct netaddr_acl routable;
};

struct _lan_data {
  struct nhdp_domain *domain;
  uint32_t metric;
  uint32_t dist;
};

/* prototypes */
static int _init(void);
static void _cleanup(void);

static const char *_parse_lan_parameters(struct _lan_data *dst, const char *src);
static void _parse_lan_array(struct cfg_named_section *section, bool add);
static void _cb_cfg_changed(void);
static void _cb_generate_tc(void *);

/* prototypes */
static enum olsr_telnet_result _cb_topology(struct olsr_telnet_data *con);

/* nhdp telnet commands */
static struct olsr_telnet_command _cmds[] = {
    TELNET_CMD("olsrv2", _cb_topology,
        "OLSRv2 database information command\n"),
};

/* subsystem definition */
static struct cfg_schema_entry _olsrv2_entries[] = {
  CFG_MAP_CLOCK_MIN(_config, tc_interval, "tc_interval", "5.0",
    "Time between two TC messages", 100),
  CFG_MAP_CLOCK_MIN(_config, tc_validity, "tc_validity", "15.0",
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
};

static struct cfg_schema_section _olsrv2_section = {
  .type = CFG_OLSRV2_SECTION,
  .cb_delta_handler = _cb_cfg_changed,
  .entries = _olsrv2_entries,
  .entry_count = ARRAYSIZE(_olsrv2_entries),
};

struct oonf_subsystem olsrv2_subsystem = {
  .init = _init,
  .cleanup = _cleanup,
  .cfg_section = &_olsrv2_section,
};

static struct _config _olsrv2_config;

/* timer for TC generation */
static struct olsr_timer_info _tc_timer_class = {
  .name = "TC generation",
  .periodic = true,
  .callback = _cb_generate_tc,
};

static struct olsr_timer_entry _tc_timer = {
  .info = &_tc_timer_class,
};

/* global variables */
enum log_source LOG_OLSRV2 = LOG_MAIN;
static struct olsr_rfc5444_protocol *_protocol;

static uint16_t _ansn;

/**
 * Initialize OLSRv2 subsystem
 * @return -1 if an error happened, 0 otherwise
 */
static int
_init(void) {
  size_t i;

  LOG_OLSRV2 = olsr_log_register_source(_LOG_OLSRV2_NAME);

  _protocol = olsr_rfc5444_add_protocol(RFC5444_PROTOCOL, true);
  if (_protocol == NULL) {
    return -1;
  }

  if (olsrv2_writer_init(_protocol)) {
    olsr_rfc5444_remove_protocol(_protocol);
    return -1;
  }

  olsrv2_lan_init();
  olsrv2_originator_init();
  olsrv2_reader_init(_protocol);
  olsrv2_tc_init();
  olsrv2_routing_init();

  /* initialize timer */
  olsr_timer_add(&_tc_timer_class);

  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    olsr_telnet_add(&_cmds[i]);
  }

  _ansn = rand() & 0xffff;
  return 0;
}

/**
 * Cleanup OLSRv2 subsystem
 */
static void
_cleanup(void) {
  size_t i;

  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    olsr_telnet_remove(&_cmds[i]);
  }

  /* cleanup configuration */
  netaddr_acl_remove(&_olsrv2_config.routable);

  olsrv2_routing_cleanup();
  olsrv2_writer_cleanup();
  olsrv2_reader_cleanup();
  olsrv2_originator_cleanup();
  olsrv2_tc_cleanup();
  olsrv2_lan_cleanup();

  olsr_rfc5444_remove_protocol(_protocol);
}

uint64_t
olsrv2_get_tc_interval(void) {
  return _olsrv2_config.tc_interval;
}

uint64_t
olsrv2_get_tc_validity(void) {
  return _olsrv2_config.tc_validity;
}

const struct netaddr_acl *
olsrv2_get_routable(void) {
    return &_olsrv2_config.routable;
}

bool
olsrv2_mpr_shall_process(
    struct rfc5444_reader_tlvblock_context *context, uint64_t vtime) {
  enum olsr_duplicate_result dup_result;
  bool process;
  struct netaddr_str buf;

  /* check if message has originator and sequence number */
  if (!context->has_origaddr || !context->has_seqno) {
    OLSR_DEBUG(LOG_OLSRV2, "Do not process message type %u,"
        " originator or sequence number is missing!",
        context->msg_type);
    return false;
  }

  /* check forwarding set */
  dup_result = olsr_duplicate_entry_add(&_protocol->processed_set,
      context->msg_type, &context->orig_addr,
      context->seqno, vtime + _olsrv2_config.f_hold_time);
  process = dup_result == OLSR_DUPSET_NEW || dup_result == OLSR_DUPSET_NEWEST;

  OLSR_DEBUG(LOG_OLSRV2, "Do %sprocess message type %u from %s"
      " with seqno %u (dupset result: %u)",
      process ? "" : "not ",
      context->msg_type,
      netaddr_to_string(&buf, &context->orig_addr),
      context->seqno, dup_result);
  return process;
}

bool
olsrv2_mpr_shall_forwarding(
    struct rfc5444_reader_tlvblock_context *context, uint64_t vtime) {
  struct nhdp_interface *interf;
  struct nhdp_laddr *laddr;
  struct nhdp_neighbor *neigh;
  enum olsr_duplicate_result dup_result;
  bool forward;
  struct netaddr_str buf;

  /* check if message has originator and sequence number */
  if (!context->has_origaddr || !context->has_seqno) {
    OLSR_DEBUG(LOG_OLSRV2, "Do not forward message type %u,"
        " originator or sequence number is missing!",
        context->msg_type);
    return false;
  }

  /* check forwarding set */
  dup_result = olsr_duplicate_entry_add(&_protocol->forwarded_set,
      context->msg_type, &context->orig_addr,
      context->seqno, vtime + _olsrv2_config.f_hold_time);
  if (dup_result != OLSR_DUPSET_NEW && dup_result != OLSR_DUPSET_NEWEST) {
    OLSR_DEBUG(LOG_OLSRV2, "Do not forward message type %u from %s"
        " with seqno %u (dupset result: %u)",
        context->msg_type,
        netaddr_to_string(&buf, &context->orig_addr),
        context->seqno, dup_result);
    return false;
  }

  /* check input interface */
  if (_protocol->input_interface == NULL) {
    OLSR_DEBUG(LOG_OLSRV2, "Do not forward because input interface is not set");
    return false;
  }

  /* checp input source address */
  if (_protocol->input_address == NULL) {
    OLSR_DEBUG(LOG_OLSRV2, "Do not forward because input source is not set");
    return false;
  }

  /* get NHDP interface */
  interf = nhdp_interface_get(_protocol->input_interface->name);
  if (interf == NULL) {
    OLSR_DEBUG(LOG_OLSRV2, "Do not forward because NHDP does not handle"
        " interface '%s'", _protocol->input_interface->name);
    return false;
  }

  /* get NHDP link address corresponding to source */
  laddr = nhdp_interface_get_link_addr(interf, _protocol->input_address);
  if (laddr == NULL) {
    OLSR_DEBUG(LOG_OLSRV2, "Do not forward because source IP %s is"
        " not a direct neighbor",
        netaddr_to_string(&buf, _protocol->input_address));
    return false;
  }

  /* get NHDP neighbor */
  neigh = laddr->link->neigh;

  /* forward if this neighbor has selected us as a flooding MPR */
  forward = neigh->local_is_flooding_mpr && neigh->symmetric > 0;
  OLSR_DEBUG(LOG_OLSRV2, "Do %sforward message type %u from %s"
      " with seqno %u",
      forward ? "" : "not ",
      context->msg_type,
      netaddr_to_string(&buf, &context->orig_addr),
      context->seqno);
  return forward;
}

bool
olsrv2_mpr_forwarding_selector(struct rfc5444_writer_target *rfc5444_target) {
  struct olsr_rfc5444_target *target;
  struct nhdp_interface *interf;
  bool is_ipv4, flood;
  struct netaddr_str buf;

  target = container_of(rfc5444_target, struct olsr_rfc5444_target, rfc5444_target);

  /* test if this is the ipv4 multicast target */
  is_ipv4 = target == target->interface->multicast4;

  /* only forward to multicast targets */
  if (!is_ipv4 && target != target->interface->multicast6) {
    return false;
  }

  /* get NHDP interface for target */
  interf = nhdp_interface_get(target->interface->name);
  if (interf == NULL) {
    OLSR_DEBUG(LOG_OLSRV2, "Do not forward message"
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

  OLSR_DEBUG(LOG_OLSRV2, "Flooding to target %s: %s",
      netaddr_to_string(&buf, &target->dst), flood ? "yes" : "no");

  return flood;
}

uint16_t
olsrv2_get_ansn(void) {
  return _ansn;
}

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

static void
_cb_generate_tc(void *ptr __attribute__((unused))) {
  olsrv2_writer_send_tc();
}

static enum olsr_telnet_result
_cb_topology(struct olsr_telnet_data *con) {
  struct olsrv2_tc_node *node;
  struct olsrv2_tc_edge *edge;
  struct olsrv2_tc_attachment *end;
  struct nhdp_domain *domain;
  struct netaddr_str nbuf;
  struct fraction_str tbuf;

  avl_for_each_element(&olsrv2_tc_tree, node, _originator_node) {
    abuf_appendf(con->out, "Node originator %s: vtime=%s\n",
        netaddr_to_string(&nbuf, &node->target.addr),
        olsr_clock_toIntervalString(&tbuf,
            olsr_timer_get_due(&node->_validity_time)));

    avl_for_each_element(&node->_edges, edge, _node) {
      abuf_appendf(con->out, "\tlink to %s%s:\n",
          netaddr_to_string(&nbuf, &edge->dst->target.addr),
          edge->virtual ? " (virtual)" : "");

      list_for_each_element(&nhdp_domain_list, domain, _node) {
        abuf_appendf(con->out, "\t\tmetric '%s': %d\n",
            domain->metric->name, edge->cost[domain->index]);
      }
    }

    avl_for_each_element(&node->_endpoints, end, _src_node) {
      abuf_appendf(con->out, "\tlink to endpoint %s:\n",
          netaddr_to_string(&nbuf, &end->dst->target.addr));

        list_for_each_element(&nhdp_domain_list, domain, _node) {
          abuf_appendf(con->out, "\t\tmetric '%s': %d\n",
              domain->metric->name, end->cost[domain->index]);
        }

    }
  }

  return TELNET_RESULT_ACTIVE;
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

  olsr_timer_set(&_tc_timer, _olsrv2_config.tc_interval);

  /* run through all pre-update LAN entries and remove them */
  _parse_lan_array(_olsrv2_section.pre, false);

  /* run through all post-update LAN entries and add them */
  _parse_lan_array(_olsrv2_section.post, true);
}
