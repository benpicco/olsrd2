
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

#define _LOG_NHDP_NAME "nhdp"

struct _nhdp_config {
  struct netaddr originator;
};

static enum olsr_telnet_result _cb_nhdb_neigh(struct olsr_telnet_data *con);
static enum olsr_telnet_result _cb_nhdb_link(struct olsr_telnet_data *con);

static void _cb_cfg_nhdp_changed(void);

/* configuration options for nhdp section */
static struct cfg_schema_section _nhdp_section = {
  .type = CFG_NHDP_SECTION,
  .mode = CFG_SSMODE_UNNAMED,
  .cb_delta_handler = _cb_cfg_nhdp_changed,
};

static struct cfg_schema_entry _nhdp_entries[] = {
  CFG_MAP_NETADDR_V46(_nhdp_config, originator, "originator", "-",
      "Originator address for all NHDP messages", false, true),
};

static struct _nhdp_config _config;

struct olsr_telnet_command _cmds[] = {
    TELNET_CMD("nhdp_neighbor", _cb_nhdb_neigh, "NHDP neighbor output"),
    TELNET_CMD("nhdp_link", _cb_nhdb_link, "NHDP link output"),
};

OLSR_SUBSYSTEM_STATE(_nhdp_state);

enum log_source LOG_NHDP = LOG_MAIN;
static struct olsr_rfc5444_protocol *_protocol;

int
nhdp_init(void) {
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

  /* add additional configuration for interface section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_nhdp_section,
      _nhdp_entries, ARRAYSIZE(_nhdp_entries));

  memset(&_config, 0, sizeof(_config));

  olsr_telnet_add(&_cmds[0]);
  olsr_telnet_add(&_cmds[1]);

  olsr_subsystem_init(&_nhdp_state);
  return 0;
}

void
nhdp_cleanup(void) {
  if (olsr_subsystem_cleanup(&_nhdp_state)) {
    return;
  }

  olsr_telnet_remove(&_cmds[0]);
  olsr_telnet_remove(&_cmds[1]);

  cfg_schema_remove_section(olsr_cfg_get_schema(), &_nhdp_section);

  nhdp_db_cleanup();
  nhdp_interfaces_cleanup();

  nhdp_writer_cleanup();
  nhdp_reader_cleanup();
}

const struct netaddr *
nhdp_get_originator(void) {
  return &_config.originator;
}

static enum olsr_telnet_result
_cb_nhdb_neigh(struct olsr_telnet_data *con) {
  struct nhdp_neighbor *neigh;
  struct nhdp_addr *naddr;
  struct netaddr_str buf;

  list_for_each_element(&nhdp_neigh_list, neigh, _node) {
    abuf_appendf(con->out, "Neighbor: %s\n", neigh->symmetric > 0 ? "symmetric" : "");

    avl_for_each_element(&neigh->_addresses, naddr, _neigh_node) {
      abuf_appendf(con->out, "\t%s\n", netaddr_to_string(&buf, &naddr->if_addr));
    }
  }

  return TELNET_RESULT_ACTIVE;
}

static enum olsr_telnet_result
_cb_nhdb_link(struct olsr_telnet_data *con) {
  static const char *PENDING = "pending";
  static const char *HEARD = "heard";
  static const char *SYMMETRIC = "symmetric";
  static const char *LOST = "lost";

  struct nhdp_neighbor *neigh;
  struct nhdp_link *lnk;
  struct nhdp_addr *naddr;
  struct nhdp_2hop *twohop;
  struct netaddr_str buf;
  const char *status;

  list_for_each_element(&nhdp_neigh_list, neigh, _node) {
    abuf_appendf(con->out, "Neighbor: %s\n", neigh->symmetric > 0 ? "symmetric" : "");

    list_for_each_element(&neigh->_links, lnk, _neigh_node) {
      if (lnk->status == NHDP_LINK_PENDING) {
          status = PENDING;
      }
      else if (lnk->status == RFC5444_LINKSTATUS_HEARD) {
          status = HEARD;
      }
      else if (lnk->status == RFC5444_LINKSTATUS_SYMMETRIC) {
          status = SYMMETRIC;
      }
      else {
        status = LOST;
      }
      abuf_appendf(con->out, "\tLink: status=%s localif=%s"
          " vtime=%"PRINTF_SSIZE_T_SPECIFIER
          " heard=%"PRINTF_SSIZE_T_SPECIFIER
          " symmetric=%"PRINTF_SSIZE_T_SPECIFIER
          "%s%s\n",
          status,
          nhdp_interface_get_name(lnk->local_if),
          olsr_timer_get_due(&lnk->vtime),
          olsr_timer_get_due(&lnk->heard_time),
          olsr_timer_get_due(&lnk->sym_time),
          lnk->hyst_pending ? " pending" : "",
          lnk->hyst_lost ? " lost" : "");

      abuf_appendf(con->out, "\t    Link addresses:\n");
      avl_for_each_element(&lnk->_addresses, naddr, _link_node) {
        abuf_appendf(con->out, "\t\t%s\n", netaddr_to_string(&buf, &naddr->if_addr));
      }
      abuf_appendf(con->out, "\t    2-Hop addresses:\n");
      avl_for_each_element(&lnk->_2hop, twohop, _link_node) {
        abuf_appendf(con->out, "\t\t%s\n", netaddr_to_string(&buf, &twohop->neigh_addr));
      }
    }
  }
  return TELNET_RESULT_ACTIVE;
}

/**
 * Configuration has changed, handle the changes
 */
static void
_cb_cfg_nhdp_changed(void) {
  if (cfg_schema_tobin(&_config, _nhdp_section.post,
      _nhdp_entries, ARRAYSIZE(_nhdp_entries))) {
    OLSR_WARN(LOG_NHDP, "Cannot convert NHDP settings.");
    return;
  }
}
