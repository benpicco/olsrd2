
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

#include "config/cfg_schema.h"
#include "packetbb/pbb_conversion.h"
#include "packetbb/pbb_iana.h"
#include "packetbb/pbb_reader.h"
#include "packetbb/pbb_writer.h"
#include "packetbb/pbb_print.h"
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
#include "dlep_client_incoming.h"
#include "dlep_client_outgoing.h"
#include "dlep_client.h"

/* constants */
#define _CFG_SECTION "dlep_client"
#define DLEP_PKT_BUFFER_SIZE 1500

#define DLEP_MESSAGE_ID 42

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static void _cb_receive_dlep(struct olsr_packet_socket *s,
      union netaddr_socket *from, size_t length);

static void _cb_send_dlep(struct pbb_writer *,
    struct pbb_writer_interface *, void *, size_t);

static void _cb_dlep_interface_timerout(void *);

static void _cb_config_changed(void);

/* plugin declaration */
OLSR_PLUGIN7 {
  .descr = "OLSRD DLEP (see IETF manet WG) client plugin",
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
    "Access control list for dlep client"),
  CFG_MAP_NETADDR_V4(_dlep_config, socket.bindto_v4, "bindto_v4", "127.0.0.1",
    "Bind dlep ipv4 socket to this address", false),
  CFG_MAP_NETADDR_V6(_dlep_config, socket.bindto_v6, "bindto_v6", "::1",
    "Bind dlep ipv6 socket to this address", false),
  CFG_MAP_NETADDR_V4(_dlep_config, socket.multicast_v4, "multicast_v4", "224.0.0.2",
    "ipv4 multicast address of this socket", false),
  CFG_MAP_NETADDR_V6(_dlep_config, socket.multicast_v6, "multicast_v6", "ff01::2",
    "ipv6 multicast address of this socket", false),
  CFG_MAP_INT_MINMAX(_dlep_config, socket.multicast_port, "port", "2001",
    "Multicast Network port for dlep interface", 1, 65535),
  CFG_MAP_STRING_ARRAY(_dlep_config, socket.interface, "interface", "",
    "Specifies socket interface (necessary for linklocal communication)", IF_NAMESIZE),

  CFG_MAP_STRING_ARRAY(_dlep_config, peer_type, "peer_type", "",
    "String for identifying this DLEP service", 80),

  CFG_MAP_CLOCK_MIN(_dlep_config, connect_interval, "connect_interval", "0.000",
    "Interval in seconds between router connect messages", 100),
  CFG_MAP_CLOCK_MINMAX(_dlep_config, connect_validity, "connect_validity", "5.000",
    "Validity time in seconds for router connect messages", 100, PBB_TIMETLV_MAX),
};

struct _dlep_config _config;
static struct olsr_packet_managed _dlep_socket = {
  .config.receive_data = _cb_receive_dlep,
};

/* DLEP session data */
struct avl_tree _session_tree;

/* infrastructure */
struct olsr_timer_info _tinfo_interface_vtime = {
  .name = "dlep interface vtime",
  .callback = _cb_dlep_interface_timerout,
};

/* dlep client logging source */
enum log_source LOG_DLEP_CLIENT;


/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  cfg_schema_add_section(olsr_cfg_get_schema(), &_dlep_section,
      _dlep_entries, ARRAYSIZE(_dlep_entries));

  LOG_DLEP_CLIENT = olsr_log_register_source("dlep-client");

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
  avl_init(&_session_tree, netaddr_avlcmp, false, NULL);

  if (dlep_client_outgoing_init()) {
    return -1;
  }

  dlep_client_incoming_init();

  olsr_packet_add_managed(&_dlep_socket);
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  struct _dlep_session *session, *s_it;

  /* remove all running sessions */
  avl_for_each_element_safe(&_session_tree, session, _node, s_it) {
    _cb_dlep_interface_timerout(session);
  }

  /* remove UDP socket */
  olsr_packet_remove_managed(&_dlep_socket, true);

  /* remove pbb handling */
  dlep_client_incoming_cleanup();
  dlep_client_outgoing_cleanup();

  return 0;
}

/**
 * Get a session for a remote dlep interface and reset its validity time
 * @param peer_socket socket of remote interface
 * @param radio_mac mac address of remote interface
 * @param vtime validity time
 * @return pointer to session, NULL if out of memory
 */
struct _dlep_session *
dlep_add_interface_session(union netaddr_socket *peer_socket,
  struct netaddr *radio_mac, uint64_t vtime) {
  struct _dlep_session *session;
  struct netaddr_str buf;

  session = avl_find_element(&_session_tree, radio_mac, session, _node);
  if (session == NULL) {
    /* allocate new session */
    OLSR_DEBUG(LOG_DLEP_CLIENT, "New DLEP session for %s",
        netaddr_to_string(&buf, radio_mac));

    session = calloc(1, sizeof(*session));
    if (session == NULL) {
      OLSR_WARN(LOG_DLEP_CLIENT, "Not enough memory for new dlep client session");
      return NULL;
    }

    session->out_if.packet_buffer = calloc(1, DLEP_PKT_BUFFER_SIZE);
    if (session->out_if.packet_buffer == NULL) {
      free(session);
      OLSR_WARN(LOG_DLEP_CLIENT, "Not enough memory for packetbb output buffer");
      return NULL;
    }
    session->out_if.packet_size = DLEP_PKT_BUFFER_SIZE;

    /* initialize new session */
    session->_node.key = &session->radio_mac;
    memcpy(&session->interface_socket, peer_socket, sizeof(*peer_socket));
    memcpy(&session->radio_mac, radio_mac, sizeof(*radio_mac));
    avl_insert(&_session_tree, &session->_node);

    session->interface_vtime.cb_context = session;
    session->interface_vtime.info = &_tinfo_interface_vtime;

    /* initialize interface */
    session->out_if.sendPacket =_cb_send_dlep,

    /* register packetbb outgoing queue */
    dlep_client_registerif(&session->out_if);
  }

  /* reset validity time for router session */
  olsr_timer_set(&session->interface_vtime, vtime);
  return session;
}

/**
 * Receive UDP data with DLEP protocol
 * @param
 * @param from
 * @param length
 */
static void
_cb_receive_dlep(struct olsr_packet_socket *s,
      union netaddr_socket *from, size_t length) {
  dlep_service_incoming_parse(s->config.input_buffer, length, from,
      s == &_dlep_socket.multicast_v4 || s == &_dlep_socket.multicast_v6);
}

/**
 * Callback for DLEP interface timeouts
 * @param ptr
 */
static void
_cb_dlep_interface_timerout(void *ptr) {
  struct _dlep_session *session = ptr;

  OLSR_DEBUG(LOG_DLEP_CLIENT, "Removing DLEP session");

  /* might have been called directly */
  olsr_timer_stop(&session->interface_vtime);

  dlep_client_unregisterif(&session->out_if);
  free(session->out_if.packet_buffer);

  avl_remove(&_session_tree, &session->_node);
  free(session);
}

/**
 * Callback for sending out the generated DLEP packet
 * @param writer
 * @param interf
 * @param ptr
 * @param len
 */
static void
_cb_send_dlep(struct pbb_writer *writer __attribute__((unused)),
    struct pbb_writer_interface *interf,
    void *ptr, size_t len) {
  struct _dlep_session *session;

  session = container_of(interf, struct _dlep_session, out_if);

  if (olsr_packet_send_managed(&_dlep_socket, &session->interface_socket, ptr, len)) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Could not sent DLEP packet to socket");
  }
}

/**
 * Update configuration of dlep-service plugin
 */
static void
_cb_config_changed(void) {
  if (cfg_schema_tobin(&_config, _dlep_section.post,
      _dlep_entries, ARRAYSIZE(_dlep_entries))) {
    OLSR_WARN(LOG_CONFIG, "Could not convert dlep_listener config to bin");
    return;
  }

  /* configure socket */
  olsr_packet_apply_managed(&_dlep_socket, &_config.socket);

  /* reconfigure timers */
  dlep_client_reconfigure_timers();
}
