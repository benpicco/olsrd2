
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2012, the olsr.org team - see HISTORY file
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
#include "config/cfg.h"
#include "config/cfg_schema.h"
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_subsystem.h"
#include "tools/olsr_cfg.h"
#include "tools/olsr_rfc5444.h"
#include "tools/olsr_telnet.h"

#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_reader.h"
#include "nhdp/nhdp_writer.h"
#include "nhdp/nhdp.h"

/* definitions */
#define _LOG_NHDP_NAME "nhdp"

struct _nhdp_config {
  int mpr_willingness;
};

/* prototypes */
static enum olsr_telnet_result _cb_nhdp(struct olsr_telnet_data *con);
static enum olsr_telnet_result _telnet_nhdp_neighbor(struct olsr_telnet_data *con);
static enum olsr_telnet_result _telnet_nhdp_link(struct olsr_telnet_data *con);
static enum olsr_telnet_result _telnet_nhdp_interface(struct olsr_telnet_data *con);

static void _cb_cfg_changed(void);

/* configuration section */
static struct cfg_schema_section _nhdp_section = {
  .type = "nhdp",
  .cb_delta_handler = _cb_cfg_changed,
};

static struct cfg_schema_entry _nhdp_entries[] = {
  CFG_MAP_INT(_nhdp_config, mpr_willingness, "willingness",
      "7", "Willingness for MPR calculation"),
};

/* nhdp telnet commands */
struct olsr_telnet_command _cmds[] = {
    TELNET_CMD("nhdp", _cb_nhdp,
        "NHDP database information command\n"
        "\"nhdp link\": shows all nhdp links including interface and 2-hop neighbor addresses\n"
        "\"nhdp neighbor\": shows all nhdp neighbors including addresses\n"
        "\"nhdp interface\": shows all local nhdp interfaces including addresses\n"),
};

/* other global variables */
OLSR_SUBSYSTEM_STATE(_nhdp_state);

enum log_source LOG_NHDP = LOG_MAIN;
static struct olsr_rfc5444_protocol *_protocol;

/* MPR handlers */
static struct nhdp_mpr_handler *_flooding_mpr, *_routing_mpr;
static int _mpr_active_counter;
static int _mpr_willingness, _mpr_default_willingness;

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

  nhdp_reader_init(_protocol);
  if (nhdp_writer_init(_protocol)) {
    nhdp_reader_cleanup();
    olsr_rfc5444_remove_protocol(_protocol);
    return -1;
  }

  nhdp_interfaces_init(_protocol);
  nhdp_db_init();

  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    olsr_telnet_add(&_cmds[i]);
  }

  /* add additional configuration for interface section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_nhdp_section,
      _nhdp_entries, ARRAYSIZE(_nhdp_entries));

  _flooding_mpr = NULL;
  _routing_mpr = NULL;
  _mpr_active_counter = 0;

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

  cfg_schema_remove_section(olsr_cfg_get_schema(), &_nhdp_section);

  for (i=0; i<ARRAYSIZE(_cmds); i++) {
    olsr_telnet_remove(&_cmds[i]);
  }

  nhdp_db_cleanup();
  nhdp_interfaces_cleanup();

  nhdp_writer_cleanup();
  nhdp_reader_cleanup();
}

/**
 * Register a user of MPR TLVs in NHDP Hellos
 */
void
nhdp_mpr_add(void) {
  _mpr_active_counter++;
  if (_mpr_active_counter == 1) {
    nhdp_mpr_update_flooding(NULL);
    nhdp_db_mpr_update_routing(NULL);
  }
}
/**
 * Unregister a user of MPR TLVs in NHDP Hellos
 */
void
nhdp_mpr_remove(void) {
  _mpr_active_counter--;
  if (_mpr_active_counter == 0) {
    nhdp_mpr_update_flooding(NULL);
    nhdp_db_mpr_update_routing(NULL);
  }
}

/**
 * @return true if MPRs are in use at the moment
 */
bool
nhdp_mpr_is_active(void) {
  return _mpr_active_counter > 0;
}

/**
 * Set the MPR willingness parameter of NHDP messages
 * @param will MPR willingness (0-7) or -1 to use default willingness
 */
void
nhdp_mpr_set_willingness(int will) {
  _mpr_willingness = will;
}

/**
 * @return current MPR willingness (0-7)
 */
int
nhdp_mpr_get_willingness(void) {
  if (_mpr_willingness == -1) {
    return _mpr_default_willingness;
  }
  return _mpr_willingness;
}

/**
 * Sets a new NHDP flooding MPR handler
 * @param mprh pointer to handler, NULL if no handler
 */
void
nhdp_mpr_set_flooding_handler(struct nhdp_mpr_handler *mprh) {
  _flooding_mpr = mprh;
  nhdp_mpr_update_flooding(NULL);
}

/**
 * Sets a new NHDP routing MPR handler
 * @param mprh pointer to handler, NULL if no handler
 */
void
nhdp_mpr_set_routing_handler(struct nhdp_mpr_handler *mprh) {
  _routing_mpr = mprh;
  nhdp_db_mpr_update_routing(NULL);
}

/**
 * Update the set of flooding MPRs
 * @param lnk pointer to link that changed, NULL if a change
 *   on multiple links might have happened
 */
void
nhdp_mpr_update_flooding(struct nhdp_link *lnk) {
  bool active = _mpr_active_counter > 0;

  if (active && _flooding_mpr != NULL) {
    _flooding_mpr->update_mprs(lnk);
    return;
  }

  if (lnk) {
    lnk->mpr_flooding = active;
    return;
  }

  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    lnk->mpr_flooding = active;
  }
}

/**
 * Update the set of routing MPRs
 * @param lnk pointer to link that changed, NULL if a change
 *   on multiple links might have happened
 */
void
nhdp_db_mpr_update_routing(struct nhdp_link *lnk) {
  bool active = _mpr_active_counter > 0;

  if (active && _routing_mpr != NULL) {
    _routing_mpr->update_mprs(lnk);
    return;
  }

  if (lnk) {
    lnk->mpr_routing = active;
    return;
  }

  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    lnk->mpr_routing = active;
  }
}

/**
 * Callback triggered when the nhdp telnet command is called
 * @param con
 * @return
 */
static enum olsr_telnet_result
_cb_nhdp(struct olsr_telnet_data *con) {
  const char *next;

  if ((next = str_hasnextword(con->parameter, "link"))) {
    return _telnet_nhdp_link(con);
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
  struct nhdp_addr *naddr;
  struct netaddr_str nbuf;
  struct timeval_str tbuf;

  list_for_each_element(&nhdp_neigh_list, neigh, _node) {
    abuf_appendf(con->out, "Neighbor: %s\n", neigh->symmetric > 0 ? "symmetric" : "");

    avl_for_each_element(&neigh->_addresses, naddr, _neigh_node) {
      if (!naddr->lost) {
        abuf_appendf(con->out, "\tAddress: %s\n", netaddr_to_string(&nbuf, &naddr->if_addr));
      }
    }
    avl_for_each_element(&neigh->_addresses, naddr, _neigh_node) {
      if (naddr->lost) {
        abuf_appendf(con->out, "\tLost address: %s (vtime=%s)\n",
            netaddr_to_string(&nbuf, &naddr->if_addr),
            olsr_clock_toIntervalString(&tbuf, olsr_timer_get_due(&naddr->vtime)));
      }
    }
  }

  return TELNET_RESULT_ACTIVE;
}

/**
 * Handle the "nhdp link" command
 * @param con
 * @return
 */
static enum olsr_telnet_result
_telnet_nhdp_link(struct olsr_telnet_data *con) {
  static const char *PENDING = "pending";
  static const char *HEARD = "heard";
  static const char *SYMMETRIC = "symmetric";
  static const char *LOST = "lost";

  struct nhdp_neighbor *neigh;
  struct nhdp_link *lnk;
  struct nhdp_addr *naddr;
  struct nhdp_2hop *twohop;
  const char *status;
  struct netaddr_str nbuf;
  struct timeval_str tbuf1, tbuf2, tbuf3;

  list_for_each_element(&nhdp_neigh_list, neigh, _node) {
    abuf_appendf(con->out, "Neighbor: %s\n", neigh->symmetric > 0 ? "symmetric" : "");

    list_for_each_element(&neigh->_links, lnk, _neigh_node) {
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
      abuf_appendf(con->out, "\tLink: status=%s localif=%s"
          " vtime=%s heard=%s symmetric=%s%s%s\n",
          status,
          nhdp_interface_get_name(lnk->local_if),
          olsr_clock_toIntervalString(&tbuf1, olsr_timer_get_due(&lnk->vtime)),
          olsr_clock_toIntervalString(&tbuf2, olsr_timer_get_due(&lnk->heard_time)),
          olsr_clock_toIntervalString(&tbuf3, olsr_timer_get_due(&lnk->sym_time)),
          lnk->hyst_pending ? " pending" : "",
          lnk->hyst_lost ? " lost" : "");

      abuf_appendf(con->out, "\t    Link addresses:\n");
      avl_for_each_element(&lnk->_addresses, naddr, _link_node) {
        abuf_appendf(con->out, "\t\t%s\n", netaddr_to_string(&nbuf, &naddr->if_addr));
      }
      abuf_appendf(con->out, "\t    2-Hop addresses:\n");
      avl_for_each_element(&lnk->_2hop, twohop, _link_node) {
        abuf_appendf(con->out, "\t\t%s\n", netaddr_to_string(&nbuf, &twohop->neigh_addr));
      }
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
  struct timeval_str tbuf1, tbuf2;
  struct netaddr_str nbuf;

  avl_for_each_element(&nhdp_interface_tree, interf, _node) {

    abuf_appendf(con->out, "Interface '%s': mode=%s hello_interval=%s hello_vtime=%s\n",
        nhdp_interface_get_name(interf), NHDP_INTERFACE_MODES[interf->mode],
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

/**
 * Configuration has changed, handle the changes
 */
static void
_cb_cfg_changed(void) {
  struct _nhdp_config config;

  if (cfg_schema_tobin(&config, _nhdp_section.post,
      _nhdp_entries, ARRAYSIZE(_nhdp_entries))) {
    OLSR_WARN(LOG_NHDP, "Cannot convert NHDP global settings.");
    return;
  }

  _mpr_default_willingness = config.mpr_willingness;
}
