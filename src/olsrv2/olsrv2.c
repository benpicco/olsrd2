
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

#include "common/common_types.h"
#include "common/netaddr.h"
#include "config/cfg_schema.h"
#include "core/olsr_logging.h"
#include "core/olsr_netaddr_acl.h"
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

struct _config {
  uint64_t tc_interval;
  uint64_t tc_validity;

  uint64_t f_hold_time;
  uint64_t p_hold_time;
  struct olsr_netaddr_acl routable;
};

/* prototypes */
static void _cb_cfg_changed(void);
static void _cb_generate_tc(void *);

/* prototypes */
static enum olsr_telnet_result _cb_topology(struct olsr_telnet_data *con);

/* nhdp telnet commands */
static struct olsr_telnet_command _cmds[] = {
    TELNET_CMD("olsrv2", _cb_topology,
        "OLSRv2 database information command\n"),
};

/* olsrv2 configuration */
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
};

static struct cfg_schema_section _olsrv2_section = {
  .type = CFG_OLSRV2_SECTION,
  .cb_delta_handler = _cb_cfg_changed,
  .entries = _olsrv2_entries,
  .entry_count = ARRAYSIZE(_olsrv2_entries),
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

OLSR_SUBSYSTEM_STATE(_olsrv2_state);

/**
 * Initialize OLSRv2 subsystem
 */
int
olsrv2_init(void) {
  size_t i;

  if (olsr_subsystem_init(&_olsrv2_state)) {
    return 0;
  }

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

  /* add configuration for olsrv2 section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_olsrv2_section);

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
void
olsrv2_cleanup(void) {
  size_t i;

  if (olsr_subsystem_cleanup(&_olsrv2_state)) {
    return;
  }

  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    olsr_telnet_remove(&_cmds[i]);
  }

  /* cleanup configuration */
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_olsrv2_section);
  olsr_acl_remove(&_olsrv2_config.routable);

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

const struct olsr_netaddr_acl *
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
