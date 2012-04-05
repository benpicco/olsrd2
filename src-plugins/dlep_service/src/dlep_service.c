
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
#include "packetbb/pbb_reader.h"
#include "packetbb/pbb_writer.h"
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

/* constants */
#define _CFG_SECTION "dlep"

#define DLEP_MESSAGE_ID 42

enum dlep_orders {
  DLEP_ORDER_INTERFACE_DISCOVERY,
  DLEP_ORDER_CONNECT_ROUTER,
  DLEP_ORDER_DISCONNECT,
  DLEP_ORDER_NEIGHBOR_UP,
  DLEP_ORDER_NEIGHBOR_DOWN,
  DLEP_ORDER_NEIGHBOR_UPDATE,
};

/* DLEP TLV types */
enum dlep_msgtlv_types {
  MSGTLV_VTIME = 1,
  DLEP_TLV_ORDER = 192,
  DLEP_TLV_PEER_TYPE,
  DLEP_TLV_STATUS,
//  DLEP_TLV_CUR_BC_RATE,
//  DLEP_TLV_MAX_BC_RATE,
};

enum dlep_addrtlv_types {
  DLEP_ADDRTLV_CUR_RATE = 192,
  DLEP_ADDRTLV_THROUGHPUT,
//  DLEP_ADDRTLV_MAX_RATE,
//  DLEP_ADDRTLV_IPv4,
//  DLEP_ADDRTLV_IPv6,
};

/* DLEP TLV array index */
enum dlep_tlv_idx {
  IDX_TLV_ORDER,
  IDX_TLV_VTIME,
  IDX_TLV_PEER_TYPE,
  IDX_TLV_STATUS,
//  IDX_TLV_CUR_BC_RATE,
//  IDX_TLV_MAX_BC_RATE,
};

/* definitions */
struct _dlep_config {
  char dlep_if[IF_NAMESIZE];
  char radio_if[IF_NAMESIZE];

  struct olsr_packet_managed_config socket;

  char peer_type[81];

  uint64_t discovery_interval, discovery_validity;
  uint64_t neighbor_interval, neighbor_validity;
};

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static enum pbb_result _cb_parse_dlep_message(
    struct pbb_reader_tlvblock_consumer *consumer,
    struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_parse_dlep_message_failed(
    struct pbb_reader_tlvblock_consumer *consumer,
    struct pbb_reader_tlvblock_context *context);
static void _cb_receive_dlep(struct olsr_packet_socket *,
      union netaddr_socket *from, size_t length);

static void _cb_ifdiscovery_addMessageTLVs(struct pbb_writer *,
    struct pbb_writer_content_provider *);

static void _cb_sendMulticast(struct pbb_writer *,
    struct pbb_writer_interface *, void *, size_t);

static void _cb_dlep_router_timerout(void *);
static void _cb_interface_discovery(void *);
static void _cb_neighbor_update(void *);

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
  CFG_MAP_STRING_LEN(_dlep_config, dlep_if, "dlep_if", "lo",
      "List of interfaces to sent DLEP broadcasts to", IF_NAMESIZE),
  CFG_MAP_STRING_LEN(_dlep_config, radio_if, "radio_id", "wlan0",
      "List of interfaces to to query link layer data from", IF_NAMESIZE),

  CFG_MAP_ACL_V46(_dlep_config, socket.acl, "acl", "127.0.0.1",
      "Access control list for dlep interface"),
  CFG_MAP_NETADDR_V4(_dlep_config, socket.bindto_v4, "bindto_v4", "127.0.0.1",
      "Bind dlep ipv4 socket to this address", false),
  CFG_MAP_NETADDR_V6(_dlep_config, socket.bindto_v6, "bindto_v6", "::1",
      "Bind dlep ipv6 socket to this address", false),
  CFG_MAP_INT_MINMAX(_dlep_config, socket.port, "port", "2001",
      "Network port for dlep interface", 1, 65535),

  CFG_MAP_STRING_ARRAY(_dlep_config, peer_type, "peer_type", "",
    "String for identifying this DLEP service", 80),

  CFG_MAP_CLOCK_MIN(_dlep_config, discovery_interval, "discovery_interval", "0",
    "Interval in seconds between interface discovery messages", 100),
  CFG_MAP_CLOCK_MIN(_dlep_config, discovery_validity, "discovery_validity", "0",
    "Validity time in seconds for interface discovery messages", 100),

  CFG_MAP_CLOCK_MIN(_dlep_config, neighbor_interval, "neighbor_interval", "0",
    "Interval in seconds between neighbor up/update messages", 100),
  CFG_MAP_CLOCK_MIN(_dlep_config, neighbor_validity, "neighbor_validity", "0",
    "Validity time in seconds for neighbor up/update messages", 100),
};

static struct _dlep_config _config;
static struct olsr_packet_managed _dlep_socket = {
  .config.receive_data = _cb_receive_dlep,
};

/* DLEP reader data */
static struct pbb_reader _dlep_reader;

static struct pbb_reader_tlvblock_consumer _dlep_message_consumer = {
  .block_callback = _cb_parse_dlep_message,
  .block_callback_failed_constraints = _cb_parse_dlep_message_failed,
};

static struct pbb_reader_tlvblock_consumer_entry _dlep_message_tlvs[] = {
  [IDX_TLV_ORDER] =       { .type = DLEP_TLV_ORDER, .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_VTIME] =       { .type = MSGTLV_VTIME, .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_PEER_TYPE] =   { .type = DLEP_TLV_PEER_TYPE, .min_length = 0, .match_length = true },
  [IDX_TLV_STATUS] =      { .type = DLEP_TLV_STATUS, .min_length = 1, .match_length = true },
};

/* DLEP writer data */
static struct pbb_writer _dlep_writer;

static struct pbb_writer_message *_dlep_message = NULL;
static struct pbb_writer_tlvtype *_dlep_addrprv_curent_datarate = NULL;

static struct pbb_writer_content_provider _dlep_msgcontent_provider = {
  .addMessageTLVs = _cb_ifdiscovery_addMessageTLVs,
};

static struct pbb_writer_interface _dlep_multicast = {
  .sendPacket =_cb_sendMulticast,
};

/* temporary variables for parsing DLEP messages */
static enum dlep_orders _current_order;
static union netaddr_socket *_peer_socket;

/* DLEP session data */
struct _dlep_session {
  struct avl_node _node;

  union netaddr_socket router_socket;
  struct olsr_timer_entry router_vtime;
};

static struct avl_tree _session_tree;

/* infrastructure */
struct olsr_timer_info _tinfo_router_vtime = {
  .name = "dlep router vtime",
  .callback = _cb_dlep_router_timerout,
};

struct olsr_timer_info _tinfo_interface_discovery = {
  .name = "dlep interface discovery",
  .callback = _cb_interface_discovery,
};
struct olsr_timer_entry _tentry_interface_discovery = {
  .info = &_tinfo_interface_discovery,
};

struct olsr_timer_info _tinfo_neighbor_update = {
  .name = "dlep neighbor update",
  .callback = _cb_neighbor_update,
};
struct olsr_timer_entry _tentry_neighbor_update = {
  .info = &_tinfo_neighbor_update,
};

/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  cfg_schema_add_section(olsr_cfg_get_schema(), &_dlep_section,
      _dlep_entries, ARRAYSIZE(_dlep_entries));

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
  if (pbb_writer_init(&_dlep_writer, 1280, 1280)) {
    OLSR_WARN(LOG_PLUGINS, "Could not init pbb writer");
    return -1;
  }

  if (pbb_writer_register_interface(&_dlep_writer, &_dlep_multicast, 1280)) {
    OLSR_WARN(LOG_PLUGINS, "Could not register DLEP interface");
    pbb_writer_cleanup(&_dlep_writer);
    return -1;
  }

  _dlep_message = pbb_writer_register_message(&_dlep_writer, DLEP_MESSAGE_ID, true, 6);
  if (_dlep_message == NULL) {
    OLSR_WARN(LOG_PLUGINS, "Could not register DLEP message");
    pbb_writer_unregister_interface(&_dlep_writer, &_dlep_multicast);
    pbb_writer_cleanup(&_dlep_writer);
    return -1;
  }

  /* cannot fail because we allocated the message above */
  pbb_writer_register_msgcontentprovider(&_dlep_writer,
      &_dlep_msgcontent_provider, DLEP_MESSAGE_ID, 0);

  _dlep_addrprv_curent_datarate =
      pbb_writer_register_addrtlvtype(&_dlep_writer,
          DLEP_MESSAGE_ID, DLEP_ADDRTLV_CUR_RATE, 0);
  if (_dlep_addrprv_curent_datarate == NULL) {
    OLSR_WARN(LOG_PLUGINS, "Count not register DLEP addrtlv");
    pbb_writer_unregister_content_provider(&_dlep_writer, &_dlep_msgcontent_provider);
    pbb_writer_unregister_message(&_dlep_writer, _dlep_message);
    pbb_writer_unregister_interface(&_dlep_writer, &_dlep_multicast);
    pbb_writer_cleanup(&_dlep_writer);
    return -1;
  }

  avl_init(&_session_tree, netaddr_socket_avlcmp, false, NULL);

  olsr_timer_add(&_tinfo_interface_discovery);
  olsr_timer_add(&_tinfo_neighbor_update);

  pbb_reader_init(&_dlep_reader);
  pbb_reader_add_message_consumer(&_dlep_reader, &_dlep_message_consumer,
      _dlep_message_tlvs, ARRAYSIZE(_dlep_message_tlvs), DLEP_MESSAGE_ID, 0);

  olsr_packet_add_managed(&_dlep_socket);
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  olsr_packet_remove_managed(&_dlep_socket, true);

  pbb_reader_remove_message_consumer(&_dlep_reader, &_dlep_message_consumer);
  pbb_reader_cleanup(&_dlep_reader);

  pbb_writer_unregister_addrtlvtype(&_dlep_writer, _dlep_addrprv_curent_datarate);
  pbb_writer_unregister_content_provider(&_dlep_writer, &_dlep_msgcontent_provider);
  pbb_writer_unregister_message(&_dlep_writer, _dlep_message);
  return 0;
}

static enum pbb_result
_parse_order_disconnect(void) {
  struct _dlep_session *session;

  session = avl_find_element(&_session_tree, _peer_socket, session, _node);
  if (session == NULL) {
    OLSR_INFO(LOG_PLUGINS, "Received DLEP disconnect from unknown peer");
    return PBB_DROP_MESSAGE;
  }

  /* call vtime callback */
  olsr_timer_stop(&session->router_vtime);
  _cb_dlep_router_timerout(session);

  return PBB_OKAY;
}

static enum pbb_result
_parse_order_connect_router(void) {
  struct _dlep_session *session;
  uint8_t encoded_vtime;
  uint64_t vtime;

  encoded_vtime = _dlep_message_tlvs[IDX_TLV_VTIME].tlv->single_value[0];

  /* TODO: decode vtime according to RFC 5497 */
  vtime = 0 * encoded_vtime + 5000;

  session = avl_find_element(&_session_tree, _peer_socket, session, _node);
  if (session == NULL) {
    /* allocate new session */
    session = calloc(1, sizeof(*session));
    if (session == NULL) {
      OLSR_WARN_OOM(LOG_PLUGINS);
      return PBB_DROP_MESSAGE;
    }

    /* initialize new session */
    session->_node.key = &session->router_socket;
    memcpy(&session->router_socket, _peer_socket, sizeof(*_peer_socket));

    session->router_vtime.cb_context = session;
    session->router_vtime.info = &_tinfo_router_vtime;
  }

  /* reset validity time for router session */
  olsr_timer_set(&session->router_vtime, vtime);

  return PBB_OKAY;
}

static enum pbb_result
_cb_parse_dlep_message(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  if (context->addr_len != 6) {
    OLSR_WARN(LOG_PLUGINS, "Address length of DLEP message should be 6 (but was %d)",
        context->addr_len);
    return PBB_DROP_MESSAGE;
  }

  _current_order = _dlep_message_tlvs[IDX_TLV_ORDER].tlv->single_value[0];
  switch (_current_order) {
    case DLEP_ORDER_DISCONNECT:
      return _parse_order_disconnect();
#if 1 /* only received by DLEP-interface */
    case DLEP_ORDER_CONNECT_ROUTER:
      return _parse_order_connect_router();
#endif
#if 0 /* only received by DLEP-router */
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      break;
    case DLEP_ORDER_NEIGHBOR_UP:
      break;
    case DLEP_ORDER_NEIGHBOR_DOWN:
      break;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      break;
#endif
    default:
      OLSR_WARN(LOG_PLUGINS, "Unknown order in DLEP message: %d", _current_order);
      return PBB_DROP_MESSAGE;
  }
  return PBB_OKAY;
}

static enum pbb_result
_cb_parse_dlep_message_failed(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  OLSR_WARN(LOG_PLUGINS, "Constraints of incoming DLEP message were not fulfilled!");
  return PBB_OKAY;
}

/**
 * Receive UDP data with DLEP protocol
 * @param
 * @param from
 * @param length
 */
static void
_cb_receive_dlep(struct olsr_packet_socket *s __attribute__((unused)),
      union netaddr_socket *from,
      size_t length __attribute__((unused))) {
  enum pbb_result result;
  uint8_t dummy[] __attribute__((unused)) = {
      /* packet header */
      0x00,

      /* message header */
      DLEP_MESSAGE_ID, 0x10, 0x00, 24, 0x11, 0x11,

      /* tlv block */
      0x00, 16,

      /* order TLV */
      DLEP_TLV_ORDER, 0x10, 1, DLEP_ORDER_CONNECT_ROUTER,

      /* VTIME tlv */
      MSGTLV_VTIME, 0x10, 1, 0x10,

      /* peer type tlv */
      DLEP_TLV_PEER_TYPE, 0x10, 5, 'h', 'e', 'l', 'l', 'o',
  };

  OLSR_DEBUG(LOG_PLUGINS, "Parsing DLEP packet");

  _peer_socket = from;

  //  result = pbb_reader_handle_packet(&_dlep_reader, s->config.input_buffer, length);
  result = pbb_reader_handle_packet(&_dlep_reader, dummy, sizeof(dummy));
  if (result) {
    OLSR_WARN(LOG_PLUGINS, "Error while parsing DLEP packet: %s (%d)",
        pbb_strerror(result), result);
  }

  _peer_socket = NULL;
}

static void
_cb_ifdiscovery_addMessageTLVs(struct pbb_writer *writer,
    struct pbb_writer_content_provider *prv __attribute__((unused))) {
  uint8_t encoded_vtime = 0;

  pbb_writer_add_messagetlv(writer,
      DLEP_TLV_ORDER, DLEP_ORDER_INTERFACE_DISCOVERY, NULL, 0);
  pbb_writer_add_messagetlv(writer,
      MSGTLV_VTIME, 0, &encoded_vtime, sizeof(encoded_vtime));

  if (_config.peer_type[0]) {
    pbb_writer_add_messagetlv(writer, DLEP_TLV_PEER_TYPE, 0,
        _config.peer_type, strlen(_config.peer_type));
  }
}

static void
_cb_dlep_router_timerout(void *ptr) {
  struct _dlep_session *session = ptr;

  OLSR_DEBUG(LOG_PLUGINS, "Removing DLEP session");
  avl_remove(&_session_tree, &session->_node);
  free(session);
}

static void
_cb_interface_discovery(void *ptr __attribute__((unused))) {

}

static void
_cb_neighbor_update(void *ptr __attribute__((unused))) {

}

static void
_cb_sendMulticast(struct pbb_writer *writer __attribute__((unused)),
    struct pbb_writer_interface *interf __attribute__((unused)),
    void *ptr __attribute__((unused)),
    size_t len __attribute__((unused))) {

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
  olsr_timer_set(&_tentry_interface_discovery, _config.discovery_interval);
  olsr_timer_set(&_tentry_neighbor_update, _config.neighbor_interval);
}
