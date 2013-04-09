
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

#include "nhdp/nhdp_interfaces.h"

#include "olsrv2/olsrv2.h"
#include "olsrv2/olsrv2_lan.h"
#include "olsrv2/olsrv2_originator.h"
#include "olsrv2/olsrv2_reader.h"
#include "olsrv2/olsrv2_writer.h"

/* definitions */
#define _LOG_OLSRV2_NAME "olsrv2"

struct _config {
  uint64_t tc_interval;
  uint64_t tc_validity;

  uint64_t f_hold_time;
  struct olsr_netaddr_acl routable;
};

/* prototypes */
static void _cb_cfg_changed(void);
static void _cb_generate_tc(void *);

/* olsrv2 configuration */
static struct cfg_schema_section _olsrv2_section = {
  .type = CFG_OLSRV2_SECTION,
  .cb_delta_handler = _cb_cfg_changed,
};

static struct cfg_schema_entry _olsrv2_entries[] = {
  CFG_MAP_CLOCK_MIN(_config, tc_interval, "tc_interval", "5.0",
    "Time between two TC messages", 100),
  CFG_MAP_CLOCK_MIN(_config, tc_validity, "tc_validity", "15.0",
    "Validity time of a TC messages", 100),
  CFG_MAP_CLOCK_MIN(_config, f_hold_time, "forward_hold_time", "30.0",
    "Holdtime for forwarding set information", 100),
  CFG_MAP_ACL_V46(_config, routable, "routable",
      OLSRV2_ROUTABLE_IPV4 OLSRV2_ROUTABLE_IPV6 ACL_DEFAULT_ACCEPT,
    "Filter to decide which addresses are considered routable"),
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

OLSR_SUBSYSTEM_STATE(_olsrv2_state);

/**
 * Initialize OLSRv2 subsystem
 */
int
olsrv2_init(void) {
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

  /* add configuration for olsrv2 section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_olsrv2_section,
      _olsrv2_entries, ARRAYSIZE(_olsrv2_entries));

  /* initialize timer */
  olsr_timer_add(&_tc_timer_class);

  return 0;
}

/**
 * Cleanup OLSRv2 subsystem
 */
void
olsrv2_cleanup(void) {
  if (olsr_subsystem_cleanup(&_olsrv2_state)) {
    return;
  }

  /* cleanup configuration */
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_olsrv2_section);

  olsrv2_reader_cleanup();
  olsrv2_originator_cleanup();
  olsrv2_lan_cleanup();
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
olsrv2_mpr_forwarding_callback(struct rfc5444_reader_tlvblock_context *context) {
  struct nhdp_neighbor *neigh;
  struct netaddr originator;
  enum olsr_duplicate_result dup_result;

  OLSR_DEBUG(LOG_OLSRV2, "forwarding callback");

  /* check if message has originator and sequence number */
  if (!context->has_origaddr || !context->has_seqno) {
    OLSR_DEBUG(LOG_OLSRV2, "Originator/Sequence number is missing!");
    return false;
  }

  /* convert originator into binary */
  if (netaddr_from_binary(&originator, context->orig_addr, context->addr_len, 0)) {
    OLSR_DEBUG(LOG_OLSRV2, "cannot convert originator");
    return false;
  }

  /* check forwarding set */
  dup_result = olsr_duplicate_entry_add(&_protocol->forwarded_set,
      context->msg_type, &originator, context->seqno, _olsrv2_config.f_hold_time);
  if (dup_result != OLSR_DUPSET_NEW && dup_result != OLSR_DUPSET_NEWEST) {
    OLSR_DEBUG(LOG_OLSRV2, "Dupset retuned %d", dup_result);
    return false;
  }

  /* get NHDP neighbor */
  neigh = nhdp_db_neighbor_get_by_originator(&originator);
  if (neigh == NULL) {
    OLSR_DEBUG(LOG_OLSRV2, "Cannot find neighbor for originator");
    return false;
  }

  /* forward if this neighbor has selected us as a flooding MPR */
  OLSR_DEBUG(LOG_OLSRV2, "Local flodding is '%s'",
      neigh->local_is_flooding_mpr ? "on" : "off");
  return neigh->local_is_flooding_mpr;
}

bool
olsrv2_mpr_forwarding_selector(struct rfc5444_writer_target *rfc5444_target) {
  struct olsr_rfc5444_target *target;
  struct nhdp_interface *interf;
  bool is_ipv4, flood;
  struct netaddr_str buf;

  target = container_of(rfc5444_target, struct olsr_rfc5444_target, rfc5444_target);

  OLSR_DEBUG(LOG_OLSRV2, "forwarding selector %s", target->interface->name);

  /* test if this is the ipv4 multicast target */
  is_ipv4 = target == target->interface->multicast4;

  /* only forward to multicast targets */
  if (!is_ipv4 && target != target->interface->multicast6) {
    OLSR_DEBUG(LOG_OLSRV2, "Target is unicast");
    return false;
  }

  /* get NHDP interface for target */
  interf = nhdp_interface_get(target->interface->name);
  if (interf == NULL) {
    OLSR_DEBUG(LOG_OLSRV2, "Cannot find interface %s", target->interface->name);
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
