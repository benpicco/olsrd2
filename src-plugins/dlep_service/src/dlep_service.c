
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

#include <errno.h>
#include <endian.h>

#include "common/common_types.h"
#include "common/avl.h"
#include "config/cfg_schema.h"
#include "packetbb/pbb_conversion.h"
#include "packetbb/pbb_iana.h"
#include "packetbb/pbb_reader.h"
#include "packetbb/pbb_writer.h"
#include "olsr_callbacks.h"
#include "olsr_cfg.h"
#include "olsr_clock.h"
#include "olsr_layer2.h"
#include "olsr_logging.h"
#include "olsr_netaddr_acl.h"
#include "olsr_packet_socket.h"
#include "olsr_plugins.h"
#include "olsr_timer.h"
#include "olsr_telnet.h"
#include "os_system.h"
#include "olsr.h"

#include "dlep_iana.h"
#include "dlep_incoming.h"
#include "dlep_outgoing.h"
#include "dlep_service.h"

/* constants */
#define _CFG_SECTION "dlep_service"

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static void _cb_dlep_router_timerout(void *);

static void _cb_neighbor_added(void *);
static void _cb_neighbor_removed(void *);
static void _cb_config_changed(void);

/* plugin declaration */
OLSR_PLUGIN7 {
  .descr = "OLSRD DLEP (see IETF manet WG) service plugin",
  .author = "Henning Rogge",

  .load = _cb_plugin_load,
  .unload = _cb_plugin_unload,
  .enable = _cb_plugin_enable,
  .disable = _cb_plugin_disable,

  .deactivate = true,
};

/* configuration */
static struct cfg_schema_section _dlep_section = {
  .type = _CFG_SECTION,
  .cb_delta_handler = _cb_config_changed,
};

static struct cfg_schema_entry _dlep_entries[] = {
  CFG_MAP_ACL_V46(_dlep_config, socket.acl, "acl", "default_accept",
    "Access control list for dlep interface"),
  CFG_MAP_NETADDR_V4(_dlep_config, socket.bindto_v4, "bindto_v4", "127.0.0.1",
    "Bind dlep ipv4 socket to this address", false),
  CFG_MAP_NETADDR_V6(_dlep_config, socket.bindto_v6, "bindto_v6", "::1",
    "Bind dlep ipv6 socket to this address", false),
  CFG_MAP_NETADDR_V4(_dlep_config, socket.multicast_v4, "multicast_v4", "224.0.0.2",
    "ipv4 multicast address of this socket", false),
  CFG_MAP_NETADDR_V6(_dlep_config, socket.multicast_v6, "multicast_v6", "ff01::2",
    "ipv6 multicast address of this socket", false),
  CFG_MAP_INT_MINMAX(_dlep_config, socket.port, "port", "2001",
    "Multicast Network port for dlep interface", 1, 65535),
  CFG_MAP_STRING_ARRAY(_dlep_config, socket.interface, "interface", "",
    "Specifies socket interface (necessary for linklocal communication)", IF_NAMESIZE),
  CFG_MAP_BOOL(_dlep_config, socket.loop_multicast, "loop_multicast", "false",
    "Allow discovery broadcasts to be received by clients on the same node"),

  CFG_MAP_STRING_ARRAY(_dlep_config, peer_type, "peer_type", "",
    "String for identifying this DLEP service", 80),

  CFG_MAP_CLOCK_MIN(_dlep_config, discovery_interval, "discovery_interval", "2.000",
    "Interval in seconds between interface discovery messages", 100),
  CFG_MAP_CLOCK_MINMAX(_dlep_config, discovery_validity, "discovery_validity", "5.000",
    "Validity time in seconds for interface discovery messages", 100, PBB_TIMETLV_MAX),

  CFG_MAP_CLOCK_MIN(_dlep_config, metric_interval, "metric_interval", "1.000",
    "Interval in seconds between neighbor update messages", 100),
  CFG_MAP_CLOCK_MINMAX(_dlep_config, metric_validity, "metric_validity", "5.000",
    "Validity time in seconds for neighbor update messages", 100, PBB_TIMETLV_MAX),

  CFG_MAP_BOOL(_dlep_config, always_send, "always_send", "false",
    "Set to true to send neighbor updates even without connected clients"),
};

struct _dlep_config _config;
static struct olsr_packet_managed _dlep_socket = {
  .config.receive_data = cb_receive_dlep,
};

/* DLEP session data */
struct avl_tree _session_tree;

/* infrastructure */
struct olsr_timer_info _tinfo_router_vtime = {
  .name = "dlep router vtime",
  .callback = _cb_dlep_router_timerout,
};

/* callback consumer for layer-2 data */
struct olsr_callback_consumer _layer2_neighbor_consumer = {
  .name = "dlep-service",
  .provider = CALLBACK_ID_LAYER2_NEIGHBOR,

  .cb_add = _cb_neighbor_added,
  .cb_remove = _cb_neighbor_removed,
};

struct olsr_callback_consumer _layer2_network_consumer = {
  .name = "dlep-service",
  .provider = CALLBACK_ID_LAYER2_NETWORK,

  .cb_add = NULL,
  .cb_remove = NULL,
};

/* dlep service logging source */
enum log_source LOG_DLEP_SERVICE;

/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  cfg_schema_add_section(olsr_cfg_get_schema(), &_dlep_section,
      _dlep_entries, ARRAYSIZE(_dlep_entries));

  LOG_DLEP_SERVICE = olsr_log_register_source("dlep-service");
  return 0;
}

/**
 * Destructor of plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_unload(void) {
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_dlep_section);
  return 0;
}

/**
 * Enable plugin
 * @return -1 if netlink socket could not be opened, 0 otherwise
 */
static int
_cb_plugin_enable(void) {
  avl_init(&_session_tree, netaddr_socket_avlcmp, false, NULL);

  olsr_callback_register_consumer(&_layer2_neighbor_consumer);
  olsr_callback_register_consumer(&_layer2_network_consumer);

  olsr_packet_add_managed(&_dlep_socket);

  dlep_incoming_init();
  dlep_outgoing_init();
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  struct _dlep_session *session, *s_it;

  avl_for_each_element_safe(&_session_tree, session, _node, s_it) {
    _cb_dlep_router_timerout(session);
  }

  dlep_outgoing_cleanup();
  dlep_incoming_cleanup();

  olsr_packet_remove_managed(&_dlep_socket, true);

  olsr_callback_unregister_consumer(&_layer2_neighbor_consumer);
  olsr_callback_unregister_consumer(&_layer2_network_consumer);

  olsr_acl_remove(&_config.socket.acl);
  return 0;
}

/**
 * Adds a DLEP session to the session tree or resets its vtime if
 * it already exists
 * @param peer_socket socket of the connected router
 * @param vtime validity time of the socket connection
 * @return pointer to dlep session, NULL if out of memory
 */
struct _dlep_session *
dlep_add_router_session(union netaddr_socket *peer_socket, uint64_t vtime) {
  struct _dlep_session *session;
  struct netaddr_str buf;

  session = avl_find_element(&_session_tree, peer_socket, session, _node);
  if (session == NULL) {
    OLSR_DEBUG(LOG_DLEP_SERVICE, "New DLEP router session for %s",
        netaddr_socket_to_string(&buf, peer_socket));

    /* allocate new session */
    session = calloc(1, sizeof(*session));
    if (session == NULL) {
      OLSR_WARN(LOG_DLEP_SERVICE, "Not enough memory for new dlep session");
      return NULL;
    }

    /* initialize new session */
    session->_node.key = &session->router_socket;
    memcpy(&session->router_socket, peer_socket, sizeof(*peer_socket));

    session->router_vtime.cb_context = session;
    session->router_vtime.info = &_tinfo_router_vtime;

    avl_insert(&_session_tree, &session->_node);
  }

  /* reset validity time for router session */
  olsr_timer_set(&session->router_vtime, vtime);
  return session;
}

/**
 * Callback for stored router session timeouts
 * @param ptr
 */
static void
_cb_dlep_router_timerout(void *ptr) {
  struct _dlep_session *session = ptr;

  OLSR_DEBUG(LOG_DLEP_SERVICE, "Removing DLEP router session");

  /* might have been called directly */
  olsr_timer_stop(&session->router_vtime);

  avl_remove(&_session_tree, &session->_node);
  free(session);
}

/**
 * Callback for sending out the generated DLEP multicast packet
 * @param writer
 * @param interf
 * @param ptr
 * @param len
 */
void
_cb_sendMulticast(struct pbb_writer *writer __attribute__((unused)),
    struct pbb_writer_interface *interf __attribute__((unused)),
    void *ptr, size_t len) {
  if (config_global.ipv4
      && olsr_packet_send_managed_multicast(&_dlep_socket, ptr, len, AF_INET) < 0) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Could not sent DLEP IPv4 packet to socket: %s (%d)",
        strerror(errno), errno);
  }
  if (config_global.ipv6
      && olsr_packet_send_managed_multicast(&_dlep_socket, ptr, len, AF_INET6) < 0) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Could not sent DLEP IPv6 packet to socket: %s (%d)",
        strerror(errno), errno);
  }
}

/**
 * Callback for receiving 'neighbor added' events from layer2 db
 * @param ptr
 */
static void
_cb_neighbor_added(void *ptr) {
  struct olsr_layer2_neighbor *nbr;
  struct netaddr_str buf1, buf2;

  nbr = ptr;

  OLSR_DEBUG(LOG_DLEP_SERVICE, "Layer 2 neighbor %s added on radio %s",
      netaddr_to_string(&buf1, &nbr->key.neighbor_mac),
      netaddr_to_string(&buf2, &nbr->key.radio_mac));

  dlep_trigger_metric_update();
}

/**
 * Callback for receiving 'neigbor removed' events from layer2 db
 * @param ptr
 */
static void
_cb_neighbor_removed(void *ptr) {
  struct olsr_layer2_neighbor *nbr;
  struct netaddr_str buf1, buf2;

  nbr = ptr;

  OLSR_DEBUG(LOG_DLEP_SERVICE, "Layer 2 neighbor %s removed on radio %s",
      netaddr_to_string(&buf1, &nbr->key.neighbor_mac),
      netaddr_to_string(&buf2, &nbr->key.radio_mac));

  dlep_trigger_metric_update();
}

/**
 * Update configuration of dlep-service plugin
 */
static void
_cb_config_changed(void) {
  int result;

  result = cfg_schema_tobin(&_config, _dlep_section.post,
      _dlep_entries, ARRAYSIZE(_dlep_entries));
  if (result) {
    OLSR_WARN(LOG_CONFIG, "Could not convert dlep_listener config to binary (%d)", -(result+1));
    return;
  }

  /* configure socket */
  olsr_packet_apply_managed(&_dlep_socket, &_config.socket);

  /* reconfigure timers */
  dlep_reconfigure_timers();
}
