
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
#include "config/cfg_schema.h"
#include "rfc5444/rfc5444_writer.h"
#include "core/oonf_logging.h"
#include "core/oonf_subsystem.h"
#include "subsystems/oonf_rfc5444.h"
#include "subsystems/oonf_telnet.h"

#include "nhdp/nhdp_hysteresis.h"
#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp_reader.h"
#include "nhdp/nhdp_writer.h"
#include "nhdp/nhdp.h"

/* definitions */
#define _LOG_NHDP_NAME "nhdp"

struct _domain_parameters {
  char metric_name[NHDP_DOMAIN_METRIC_MAXLEN];
  char mpr_name[NHDP_DOMAIN_MPR_MAXLEN];
};

/* prototypes */
static int _init(void);
static void _initiate_shutdown(void);
static void _cleanup(void);

static enum oonf_telnet_result _cb_nhdp(struct oonf_telnet_data *con);
static enum oonf_telnet_result _telnet_nhdp_neighbor(struct oonf_telnet_data *con);
static enum oonf_telnet_result _telnet_nhdp_neighlink(struct oonf_telnet_data *con);
static enum oonf_telnet_result _telnet_nhdp_iflink(struct oonf_telnet_data *con);
static enum oonf_telnet_result _telnet_nhdp_interface(struct oonf_telnet_data *con);

static void _cb_cfg_domain_changed(void);
static void _cb_cfg_interface_changed(void);
static int _cb_validate_domain_section(const char *section_name,
    struct cfg_named_section *, struct autobuf *);

/* nhdp telnet commands */
static struct oonf_telnet_command _cmds[] = {
    TELNET_CMD("nhdp", _cb_nhdp,
        "NHDP database information command\n"
        "\"nhdp iflink\": shows all nhdp links sorted by interfaces including interface and 2-hop neighbor addresses\n"
        "\"nhdp neighlink\": shows all nhdp links sorted by neighbors including interface and 2-hop neighbor addresses\n"
        "\"nhdp neighbor\": shows all nhdp neighbors including addresses\n"
        "\"nhdp interface\": shows all local nhdp interfaces including addresses\n"),
};

/* subsystem definition */
static struct cfg_schema_entry _interface_entries[] = {
  CFG_MAP_ACL_V46(nhdp_interface, ifaddr_filter, "ifaddr_filter", ACL_DEFAULT_REJECT,
      "Filter for ip interface addresses that should be included in HELLO messages"),
  CFG_MAP_CLOCK_MIN(nhdp_interface, h_hold_time, "hello-validity", "20.0",
    "Validity time for NHDP Hello Messages", 100),
  CFG_MAP_CLOCK_MIN(nhdp_interface, refresh_interval, "hello-interval", "2.0",
    "Time interval between two NHDP Hello Messages", 100),
};

static struct cfg_schema_section _interface_section = {
  .type = CFG_INTERFACE_SECTION,
  .mode = CFG_SSMODE_NAMED_MANDATORY,
  .cb_delta_handler = _cb_cfg_interface_changed,
  .entries = _interface_entries,
  .entry_count = ARRAYSIZE(_interface_entries),
};

static struct cfg_schema_entry _domain_entries[] = {
  CFG_MAP_STRING_ARRAY(_domain_parameters, metric_name, "metric", CFG_DOMAIN_ANY_METRIC,
      "ID of the routing metric used for this domain. '"CFG_DOMAIN_NO_METRIC"'"
      " means no metric (hopcount!), '"CFG_DOMAIN_ANY_METRIC"' means any metric"
      " that is loaded (with fallback on '"CFG_DOMAIN_NO_METRIC"').",
      NHDP_DOMAIN_METRIC_MAXLEN),
  CFG_MAP_STRING_ARRAY(_domain_parameters, mpr_name,  "mpr", CFG_DOMAIN_ANY_MPR,
      "ID of the mpr algorithm used for this domain. '"CFG_DOMAIN_NO_MPR"'"
      " means no mpr algorithm(everyone is MPR), '"CFG_DOMAIN_ANY_MPR"' means"
      "any metric that is loaded (with fallback on '"CFG_DOMAIN_NO_MPR"').",
      NHDP_DOMAIN_MPR_MAXLEN),
};

static struct cfg_schema_section _domain_section = {
  .type = CFG_NHDP_DOMAIN_SECTION,
  .mode = CFG_SSMODE_NAMED_WITH_DEFAULT,
  .def_name = CFG_NHDP_DEFAULT_DOMAIN,

  .cb_delta_handler = _cb_cfg_domain_changed,
  .cb_validate = _cb_validate_domain_section,

  .entries = _domain_entries,
  .entry_count = ARRAYSIZE(_domain_entries),
  .next_section = &_interface_section,
};

struct oonf_subsystem nhdp_subsystem = {
  .init = _init,
  .cleanup = _cleanup,
  .initiate_shutdown = _initiate_shutdown,
  .cfg_section = &_domain_section,
};

/* other global variables */
enum log_source LOG_NHDP = LOG_MAIN;
static struct oonf_rfc5444_protocol *_protocol;

/* NHDP originator address, might be undefined */
static struct netaddr _originator_v4, _originator_v6;

/**
 * Initialize NHDP subsystem
 * @return 0 if initialized, -1 if an error happened
 */
static int
_init(void) {
  size_t i;

  LOG_NHDP = oonf_log_register_source(_LOG_NHDP_NAME);

  _protocol = oonf_rfc5444_add_protocol(RFC5444_PROTOCOL, true);
  if (_protocol == NULL) {
    return -1;
  }

  if (nhdp_writer_init(_protocol)) {
    oonf_rfc5444_remove_protocol(_protocol);
    return -1;
  }

  nhdp_db_init();
  nhdp_reader_init(_protocol);
  nhdp_interfaces_init(_protocol);
  nhdp_domain_init(_protocol);

  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    oonf_telnet_add(&_cmds[i]);
  }
  return 0;
}

/**
 * Begin shutdown by deactivating reader and writer
 */
static void
_initiate_shutdown(void) {
  nhdp_writer_cleanup();
  nhdp_reader_cleanup();
}

/**
 * Cleanup NHDP subsystem
 */
static void
_cleanup(void) {
  size_t i;

  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    oonf_telnet_remove(&_cmds[i]);
  }

  nhdp_domain_cleanup();
  nhdp_interfaces_cleanup();
  nhdp_db_cleanup();
}

/**
 * Sets the originator address used by NHDP to a new value.
 * @param addr NHDP originator.
 */
void
nhdp_set_originator(const struct netaddr *addr) {
#if OONF_LOGGING_LEVEL >= OONF_LOGGING_LEVEL_DEBUG
  struct netaddr_str buf;
#endif

  OONF_DEBUG(LOG_NHDP, "Set originator to %s", netaddr_to_string(&buf, addr));
  if (netaddr_get_address_family(addr) == AF_INET) {
    memcpy(&_originator_v4, addr, sizeof(*addr));
  }
  else if (netaddr_get_address_family(addr) == AF_INET6) {
    memcpy(&_originator_v6, addr, sizeof(*addr));
  }
}

/**
 * Remove the originator currently set
 * @param af_type address family type of the originator
 *   (AF_INET or AF_INET6)
 */
void
nhdp_reset_originator(int af_type) {
  if (af_type == AF_INET) {
    netaddr_invalidate(&_originator_v4);
  }
  else if (af_type == AF_INET6) {
    netaddr_invalidate(&_originator_v6);
  }
}

/**
 * @param af_type address family type of the originator
 *   (AF_INET or AF_INET6)
 * @return current NHDP originator
 */
const struct netaddr *
nhdp_get_originator(int af_type) {
  if (af_type == AF_INET) {
    return &_originator_v4;
  }
  else if (af_type == AF_INET6) {
    return &_originator_v6;
  }
  return NULL;
}

/**
 * Callback triggered when the nhdp telnet command is called
 * @param con
 * @return
 */
static enum oonf_telnet_result
_cb_nhdp(struct oonf_telnet_data *con) {
  const char *next;

  if ((next = str_hasnextword(con->parameter, "neighlink"))) {
    return _telnet_nhdp_neighlink(con);
  }
  if ((next = str_hasnextword(con->parameter, "iflink"))) {
    return _telnet_nhdp_iflink(con);
  }
  if ((next = str_hasnextword(con->parameter, "neighbor"))) {
    return _telnet_nhdp_neighbor(con);
  }
  if ((next = str_hasnextword(con->parameter, "interface"))) {
    return _telnet_nhdp_interface(con);
  }

  if (con->parameter == NULL || *con->parameter == 0) {
    abuf_puts(con->out, "Error, 'nhdp' needs a parameter\n");
  }
  abuf_appendf(con->out, "Wrong parameter in command: %s", con->parameter);
  return TELNET_RESULT_ACTIVE;
}

/**
 * handle the "nhdp neighbor" command
 * @param con
 * @return
 */
static enum oonf_telnet_result
_telnet_nhdp_neighbor(struct oonf_telnet_data *con) {
  struct nhdp_neighbor *neigh;
  struct nhdp_naddr *naddr;
  struct netaddr_str nbuf;
  struct fraction_str tbuf;

  list_for_each_element(&nhdp_neigh_list, neigh, _global_node) {
    abuf_appendf(con->out, "Neighbor: %s\n", neigh->symmetric > 0 ? "symmetric" : "");

    avl_for_each_element(&neigh->_neigh_addresses, naddr, _neigh_node) {
      if (!nhdp_db_neighbor_addr_is_lost(naddr)) {
        abuf_appendf(con->out, "\tAddress: %s\n", netaddr_to_string(&nbuf, &naddr->neigh_addr));
      }
    }
    avl_for_each_element(&neigh->_neigh_addresses, naddr, _neigh_node) {
      if (nhdp_db_neighbor_addr_is_lost(naddr)) {
        abuf_appendf(con->out, "\tLost address: %s (vtime=%s)\n",
            netaddr_to_string(&nbuf, &naddr->neigh_addr),
            oonf_clock_toIntervalString(&tbuf, oonf_timer_get_due(&naddr->_lost_vtime)));
      }
    }
  }

  return TELNET_RESULT_ACTIVE;
}

/**
 * Print the content of a NHDP link to the telnet console
 * @param con telnet data connection
 * @param lnk NHDP link
 * @param prefix text prefix for each line
 * @param other_addr true if the IP addresses not associated
 *   with this link (but with this neighbor) should be printed
 */
static void
_print_link(struct oonf_telnet_data *con, struct nhdp_link *lnk,
    const char *prefix, bool other_addr) {
  static const char *PENDING = "pending";
  static const char *HEARD = "heard";
  static const char *SYMMETRIC = "symmetric";
  static const char *LOST = "lost";

  struct nhdp_laddr *laddr;
  struct nhdp_l2hop *twohop;
  struct nhdp_naddr *naddr;

  const char *status;

  struct fraction_str tbuf1, tbuf2, tbuf3;
  struct nhdp_hysteresis_str hbuf;
  struct netaddr_str nbuf;

  if (lnk->status == NHDP_LINK_PENDING) {
      status = PENDING;
  }
  else if (lnk->status == NHDP_LINK_HEARD) {
      status = HEARD;
  }
  else if (lnk->status == NHDP_LINK_SYMMETRIC) {
      status = SYMMETRIC;
  }
  else {
    status = LOST;
  }
  abuf_appendf(con->out, "%s%s status=%s localif=%s"
      " vtime=%s heard=%s symmetric=%s %s%s\n",
      prefix,
      nhdp_db_link_is_ipv6_dualstack(lnk)  ? "     " : "Link:",
      status,
      nhdp_interface_get_name(lnk->local_if),
      oonf_clock_toIntervalString(&tbuf1, oonf_timer_get_due(&lnk->vtime)),
      oonf_clock_toIntervalString(&tbuf2, oonf_timer_get_due(&lnk->heard_time)),
      oonf_clock_toIntervalString(&tbuf3, oonf_timer_get_due(&lnk->sym_time)),
      lnk->dualstack_partner != NULL ? "dualstack " : "",
      nhdp_hysteresis_to_string(&hbuf, lnk));
  if (netaddr_get_address_family(&lnk->neigh->originator) != AF_UNSPEC) {
    abuf_appendf(con->out, "%s\tOriginator: %s\n", prefix,
        netaddr_to_string(&nbuf, &lnk->neigh->originator));
  }

  avl_for_each_element(&lnk->_addresses, laddr, _link_node) {
    abuf_appendf(con->out, "%s\tLink addresses: %s\n",
        prefix, netaddr_to_string(&nbuf, &laddr->link_addr));
  }
  if (other_addr) {
    avl_for_each_element(&lnk->neigh->_neigh_addresses, naddr, _neigh_node) {
      if (!nhdp_db_neighbor_addr_is_lost(naddr) && avl_find(&lnk->_addresses, &naddr->neigh_addr) == NULL) {
        abuf_appendf(con->out, "%s\tOther addresses: %s\n",
            prefix, netaddr_to_string(&nbuf, &naddr->neigh_addr));
      }
    }
  }
  avl_for_each_element(&lnk->_2hop, twohop, _link_node) {
    abuf_appendf(con->out, "%s\t2-Hop addresses: %s\n",
        prefix, netaddr_to_string(&nbuf, &twohop->twohop_addr));
  }
}

/**
 * Print a NHDP neighbor to the telnet console
 * @param con telnet data connection
 * @param neigh NHDP neighbor
 */
static void
_print_neigh(struct oonf_telnet_data *con, struct nhdp_neighbor *neigh) {
  struct nhdp_naddr *naddr;
  struct nhdp_link *lnk;
  struct netaddr_str nbuf;
  struct nhdp_domain *domain;

  abuf_appendf(con->out, "%s %s%s\n",
      nhdp_db_neighbor_is_ipv6_dualstack(neigh) ? "         " : "Neighbor:",
      neigh->symmetric > 0 ? "symmetric" : "",
      neigh->dualstack_partner != NULL ? "dualstack" : "");

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    abuf_appendf(con->out, "\tMetric '%s': in=%d, out=%d, MPR=%s, MPRS=%s, will=%d\n",
        domain->metric->name,
        nhdp_domain_get_neighbordata(domain, neigh)->metric.in,
        nhdp_domain_get_neighbordata(domain, neigh)->metric.out,
        nhdp_domain_get_neighbordata(domain, neigh)->neigh_is_mpr ? "yes" : "no",
        nhdp_domain_get_neighbordata(domain, neigh)->local_is_mpr ? "yes" : "no",
        nhdp_domain_get_neighbordata(domain, neigh)->willingness);
  }

  list_for_each_element(&neigh->_links, lnk, _neigh_node) {
    if (!nhdp_db_link_is_ipv6_dualstack(lnk)) {
      _print_link(con, lnk, "\t",  false);
    }
    if (nhdp_db_link_is_ipv4_dualstack(lnk)) {
      _print_link(con, lnk->dualstack_partner, "\t", false);
    }
  }

  avl_for_each_element(&neigh->_neigh_addresses, naddr, _neigh_node) {
    if (avl_find(&neigh->_link_addresses, &naddr->neigh_addr) == NULL) {
      abuf_appendf(con->out, "\tAddress on other interface: %s",
          netaddr_to_string(&nbuf, &naddr->neigh_addr));
    }
  }
}
/**
 * Handle the "nhdp neighlink" command
 * @param con telnet data connection
 * @return always TELNET_RESULT_ACTIVE
 */
static enum oonf_telnet_result
_telnet_nhdp_neighlink(struct oonf_telnet_data *con) {
  struct nhdp_neighbor *neigh;

  list_for_each_element(&nhdp_neigh_list, neigh, _global_node) {
    if (!nhdp_db_neighbor_is_ipv6_dualstack(neigh)) {
      _print_neigh(con, neigh);
    }
    if (nhdp_db_neighbor_is_ipv4_dualstack(neigh)) {
      _print_neigh(con, neigh->dualstack_partner);
    }
  }
  return TELNET_RESULT_ACTIVE;
}

/**
 * Handle the "nhdp iflink" command
 * @param con telnet data connection
 * @return always TELNET_RESULT_ACTIVE
 */
static enum oonf_telnet_result
_telnet_nhdp_iflink(struct oonf_telnet_data *con) {
  struct nhdp_interface *interf;
  struct nhdp_interface_addr *addr;

  struct nhdp_link *lnk;

  struct netaddr_str nbuf;
  struct fraction_str tbuf1, tbuf2;

  avl_for_each_element(&nhdp_interface_tree, interf, _node) {

    abuf_appendf(con->out, "Interface '%s': hello_interval=%s hello_vtime=%s\n",
        nhdp_interface_get_name(interf),
        oonf_clock_toIntervalString(&tbuf1, interf->refresh_interval),
        oonf_clock_toIntervalString(&tbuf2, interf->h_hold_time));

    avl_for_each_element(&interf->_if_addresses, addr, _if_node) {
      if (!addr->removed) {
        abuf_appendf(con->out, "\tAddress: %s\n", netaddr_to_string(&nbuf, &addr->if_addr));
      }
    }

    list_for_each_element(&interf->_links, lnk, _if_node) {
      _print_link(con, lnk, "\t", true);
    }
  }
  return TELNET_RESULT_ACTIVE;
}

/**
 * Handle the "nhdp interface" telnet command
 * @param con telnet data connection
 * @return always TELNET_RESULT_ACTIVE
 */
static enum oonf_telnet_result
_telnet_nhdp_interface(struct oonf_telnet_data *con) {
  struct nhdp_interface *interf;
  struct nhdp_interface_addr *addr;
  struct fraction_str tbuf1, tbuf2;
  struct netaddr_str nbuf;

  avl_for_each_element(&nhdp_interface_tree, interf, _node) {

    abuf_appendf(con->out, "Interface '%s': hello_interval=%s hello_vtime=%s\n",
        nhdp_interface_get_name(interf),
        oonf_clock_toIntervalString(&tbuf1, interf->refresh_interval),
        oonf_clock_toIntervalString(&tbuf2, interf->h_hold_time));

    avl_for_each_element(&interf->_if_addresses, addr, _if_node) {
      if (!addr->removed) {
        abuf_appendf(con->out, "\tAddress: %s\n", netaddr_to_string(&nbuf, &addr->if_addr));
      }
    }

    avl_for_each_element(&interf->_if_addresses, addr, _if_node) {
      if (addr->removed) {
        abuf_appendf(con->out, "\tRemoved address: %s (vtime=%s)\n",
            netaddr_to_string(&nbuf, &addr->if_addr),
            oonf_clock_toIntervalString(&tbuf1, oonf_timer_get_due(&addr->_vtime)));
      }
    }
  }
  return TELNET_RESULT_ACTIVE;
}

/**
 * Configuration of a NHDP domain changed
 */
static void
_cb_cfg_domain_changed(void) {
  struct _domain_parameters param;
  int ext;

  OONF_INFO(LOG_NHDP, "Received domain cfg change for name '%s': %s %s",
      _domain_section.section_name,
      _domain_section.pre != NULL ? "pre" : "-",
      _domain_section.post != NULL ? "post" : "-");

  ext = strtol(_domain_section.section_name, NULL, 10);

  if (cfg_schema_tobin(&param, _domain_section.post,
      _domain_entries, ARRAYSIZE(_domain_entries))) {
    OONF_WARN(LOG_NHDP, "Cannot convert NHDP domain configuration.");
    return;
  }

  nhdp_domain_configure(ext, param.metric_name, param.mpr_name);
}

/**
 * Configuration has changed, handle the changes
 */
static void
_cb_cfg_interface_changed(void) {
  struct nhdp_interface *interf;

  OONF_DEBUG(LOG_NHDP, "Configuration of NHDP interface %s changed",
      _interface_section.section_name);

  /* get interface */
  interf = nhdp_interface_get(_interface_section.section_name);

  if (_interface_section.post == NULL) {
    /* section was removed */
    if (interf != NULL) {
      nhdp_interface_remove(interf);
    }
    return;
  }

  if (interf == NULL) {
    interf = nhdp_interface_add(_interface_section.section_name);
  }

  if (cfg_schema_tobin(interf, _interface_section.post,
      _interface_entries, ARRAYSIZE(_interface_entries))) {
    OONF_WARN(LOG_NHDP, "Cannot convert NHDP configuration for interface.");
    return;
  }

  /* apply new settings to interface */
  nhdp_interface_apply_settings(interf);
}

/**
 * Validate that the name of the domain section is valid
 * @param section_name name of section including type
 * @param named cfg named section
 * @param out output buffer for errors
 * @return -1 if invalid, 0 otherwise
 */
static int
_cb_validate_domain_section(const char *section_name,
    struct cfg_named_section *named, struct autobuf *out) {
  char *error = NULL;
  int ext;

  ext = strtol(named->name, &error, 10);
  if (error != NULL && *error != 0) {
    /* illegal domain name */
    abuf_appendf(out, "name of section '%s' must be a number between 0 and 255",
        section_name);
    return -1;
  }

  if (ext < 0 || ext > 255) {
    /* name out of range */
    abuf_appendf(out, "name of section '%s' must be a number between 0 and 255",
        section_name);
    return -1;
  }
  return 0;
}
