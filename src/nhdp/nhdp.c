
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
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_logging.h"
#include "core/olsr_subsystem.h"
#include "tools/olsr_rfc5444.h"
#include "tools/olsr_telnet.h"

#include "nhdp/nhdp_hysteresis.h"
#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp_reader.h"
#include "nhdp/nhdp_writer.h"
#include "nhdp/nhdp.h"

/* definitions */
#define _LOG_NHDP_NAME "nhdp"

/* prototypes */
static enum olsr_telnet_result _cb_nhdp(struct olsr_telnet_data *con);
static enum olsr_telnet_result _telnet_nhdp_neighbor(struct olsr_telnet_data *con);
static enum olsr_telnet_result _telnet_nhdp_neighlink(struct olsr_telnet_data *con);
static enum olsr_telnet_result _telnet_nhdp_iflink(struct olsr_telnet_data *con);
static enum olsr_telnet_result _telnet_nhdp_interface(struct olsr_telnet_data *con);

/* nhdp telnet commands */
struct olsr_telnet_command _cmds[] = {
    TELNET_CMD("nhdp", _cb_nhdp,
        "NHDP database information command\n"
        "\"nhdp iflink\": shows all nhdp links sorted by interfaces including interface and 2-hop neighbor addresses\n"
        "\"nhdp neighlink\": shows all nhdp links sorted by neighbors including interface and 2-hop neighbor addresses\n"
        "\"nhdp neighbor\": shows all nhdp neighbors including addresses\n"
        "\"nhdp interface\": shows all local nhdp interfaces including addresses\n"),
};

/* other global variables */
OLSR_SUBSYSTEM_STATE(_nhdp_state);

enum log_source LOG_NHDP = LOG_MAIN;
static struct olsr_rfc5444_protocol *_protocol;

/* NHDP originator address, might be undefined */
static struct netaddr _originator_v4, _originator_v6;

/**
 * Initialize NHDP subsystem
 * @return 0 if initialized, -1 if an error happened
 */
int
nhdp_init(void) {
  size_t i;

  if (olsr_subsystem_is_initialized(&_nhdp_state)) {
    return 0;
  }

  LOG_NHDP = olsr_log_register_source(_LOG_NHDP_NAME);

  _protocol = olsr_rfc5444_add_protocol(RFC5444_PROTOCOL, true);
  if (_protocol == NULL) {
    return -1;
  }

  if (nhdp_writer_init(_protocol)) {
    olsr_rfc5444_remove_protocol(_protocol);
    return -1;
  }

  nhdp_db_init();
  nhdp_reader_init(_protocol);
  nhdp_interfaces_init(_protocol);
  nhdp_domain_init(_protocol);

  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    olsr_telnet_add(&_cmds[i]);
  }

  olsr_subsystem_init(&_nhdp_state);
  return 0;
}

/**
 * Cleanup NHDP subsystem
 */
void
nhdp_cleanup(void) {
  size_t i;
  if (olsr_subsystem_cleanup(&_nhdp_state)) {
    return;
  }

  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    olsr_telnet_remove(&_cmds[i]);
  }

  nhdp_domain_cleanup();
  nhdp_writer_cleanup();
  nhdp_reader_cleanup();
  nhdp_interfaces_cleanup();
  nhdp_db_cleanup();
}

/**
 * Sets the originator address used by NHDP to a new value.
 * @param NHDP originator.
 */
void
nhdp_set_originator(const struct netaddr *addr) {
  struct netaddr_str buf;

  OLSR_DEBUG(LOG_NHDP, "Set originator to %s", netaddr_to_string(&buf, addr));
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
static enum olsr_telnet_result
_cb_nhdp(struct olsr_telnet_data *con) {
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
static enum olsr_telnet_result
_telnet_nhdp_neighbor(struct olsr_telnet_data *con) {
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
            olsr_clock_toIntervalString(&tbuf, olsr_timer_get_due(&naddr->_lost_vtime)));
      }
    }
  }

  return TELNET_RESULT_ACTIVE;
}

static void
_print_link(struct olsr_telnet_data *con, struct nhdp_link *lnk,
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
      olsr_clock_toIntervalString(&tbuf1, olsr_timer_get_due(&lnk->vtime)),
      olsr_clock_toIntervalString(&tbuf2, olsr_timer_get_due(&lnk->heard_time)),
      olsr_clock_toIntervalString(&tbuf3, olsr_timer_get_due(&lnk->sym_time)),
      lnk->dualstack_partner != NULL ? "dualstack " : "",
      nhdp_hysteresis_to_string(&hbuf, lnk));

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

static void
_print_neigh(struct olsr_telnet_data *con, struct nhdp_neighbor *neigh) {
  struct nhdp_naddr *naddr;
  struct nhdp_link *lnk;
  struct netaddr_str nbuf;

  abuf_appendf(con->out, "%s %s%s\n",
      nhdp_db_neighbor_is_ipv6_dualstack(neigh) ? "         " : "Neighbor:",
      neigh->symmetric > 0 ? "symmetric" : "",
      neigh->dualstack_partner != NULL ? "dualstack" : "");

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
 * @param con
 * @return
 */
static enum olsr_telnet_result
_telnet_nhdp_neighlink(struct olsr_telnet_data *con) {
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
 * @param con
 * @return
 */
static enum olsr_telnet_result
_telnet_nhdp_iflink(struct olsr_telnet_data *con) {
  struct nhdp_interface *interf;
  struct nhdp_interface_addr *addr;

  struct nhdp_link *lnk;

  struct netaddr_str nbuf;
  struct fraction_str tbuf1, tbuf2;

  avl_for_each_element(&nhdp_interface_tree, interf, _node) {

    abuf_appendf(con->out, "Interface '%s': hello_interval=%s hello_vtime=%s\n",
        nhdp_interface_get_name(interf),
        olsr_clock_toIntervalString(&tbuf1, interf->refresh_interval),
        olsr_clock_toIntervalString(&tbuf2, interf->h_hold_time));

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
 * @param con
 * @return
 */
static enum olsr_telnet_result
_telnet_nhdp_interface(struct olsr_telnet_data *con) {
  struct nhdp_interface *interf;
  struct nhdp_interface_addr *addr;
  struct fraction_str tbuf1, tbuf2;
  struct netaddr_str nbuf;

  avl_for_each_element(&nhdp_interface_tree, interf, _node) {

    abuf_appendf(con->out, "Interface '%s': hello_interval=%s hello_vtime=%s\n",
        nhdp_interface_get_name(interf),
        olsr_clock_toIntervalString(&tbuf1, interf->refresh_interval),
        olsr_clock_toIntervalString(&tbuf2, interf->h_hold_time));

    avl_for_each_element(&interf->_if_addresses, addr, _if_node) {
      if (!addr->removed) {
        abuf_appendf(con->out, "\tAddress: %s\n", netaddr_to_string(&nbuf, &addr->if_addr));
      }
    }

    avl_for_each_element(&interf->_if_addresses, addr, _if_node) {
      if (addr->removed) {
        abuf_appendf(con->out, "\tRemoved address: %s (vtime=%s)\n",
            netaddr_to_string(&nbuf, &addr->if_addr),
            olsr_clock_toIntervalString(&tbuf1, olsr_timer_get_due(&addr->_vtime)));
      }
    }
  }
  return TELNET_RESULT_ACTIVE;
}
