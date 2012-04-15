
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
#define _CFG_SECTION "dlep_client"
#define DLEP_PKT_BUFFER_SIZE 1500

#define DLEP_MESSAGE_ID 42

enum dlep_orders {
  DLEP_ORDER_INTERFACE_DISCOVERY,
  DLEP_ORDER_CONNECT_ROUTER,
  DLEP_ORDER_DISCONNECT,
  DLEP_ORDER_NEIGHBOR_UP,
//   DLEP_ORDER_NEIGHBOR_DOWN,
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
  struct olsr_packet_managed_config socket;
  char peer_type[81];

  uint64_t connect_interval, connect_validity;
};

struct _dlep_session {
  struct avl_node _node;

  union netaddr_socket interface_socket;
  struct pbb_writer_interface out_if;

  struct netaddr radio_mac;
  struct olsr_timer_entry interface_vtime;
  uint16_t seqno;
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

static void _cb_addMessageHeader(struct pbb_writer *,
    struct pbb_writer_message *);
static void _cb_addMessageTLVs(struct pbb_writer *,
    struct pbb_writer_content_provider *);

static void _cb_send_dlep(struct pbb_writer *,
    struct pbb_writer_interface *, void *, size_t);

static void _cb_dlep_interface_timerout(void *);
static void _cb_router_connect(void *);

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
  CFG_MAP_ACL_V46(_dlep_config, socket.acl, "acl", NULL,
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
  CFG_MAP_CLOCK_MIN(_dlep_config, connect_validity, "connect_validity", "5.000",
    "Validity time in seconds for router connect messages", 100),
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
  [IDX_TLV_PEER_TYPE] =   { .type = DLEP_TLV_PEER_TYPE, .min_length = 1, .max_length = 80, .match_length = true },
  [IDX_TLV_STATUS] =      { .type = DLEP_TLV_STATUS, .min_length = 1, .match_length = true },
};

/* DLEP writer data */
static uint8_t _msg_buffer[1500];
static uint8_t _msg_addrtlvs[5000];

static enum dlep_orders _msg_order;
static struct _dlep_session *_msg_session;

static struct pbb_writer _dlep_writer = {
  .msg_buffer = _msg_buffer,
  .msg_size = sizeof(_msg_buffer),
  .addrtlv_buffer = _msg_addrtlvs,
  .addrtlv_size = sizeof(_msg_addrtlvs),
};

static struct pbb_writer_message *_dlep_message = NULL;

static struct pbb_writer_content_provider _dlep_msgcontent_provider = {
  .msg_type = DLEP_MESSAGE_ID,
  .addMessageTLVs = _cb_addMessageTLVs,
};

static struct pbb_writer_addrtlv_block _dlep_addrtlvs[] = {
  { .type = DLEP_ADDRTLV_CUR_RATE },
};

/* temporary variables for parsing DLEP messages */
static enum dlep_orders _current_order;
static union netaddr_socket *_peer_socket;

/* DLEP session data */
static struct avl_tree _session_tree;

/* infrastructure */
struct olsr_timer_info _tinfo_interface_vtime = {
  .name = "dlep interface vtime",
  .callback = _cb_dlep_interface_timerout,
};

struct olsr_timer_info _tinfo_router_connect = {
  .name = "dlep interface discovery",
  .callback = _cb_router_connect,
  .periodic = true,
};
struct olsr_timer_entry _tentry_router_connect = {
  .info = &_tinfo_router_connect,
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
  pbb_writer_init(&_dlep_writer);

  _dlep_message = pbb_writer_register_message(&_dlep_writer, DLEP_MESSAGE_ID, true, 6);
  if (_dlep_message == NULL) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Could not register DLEP message");
    pbb_writer_cleanup(&_dlep_writer);
    return -1;
  }
  _dlep_message->addMessageHeader = _cb_addMessageHeader;

  if (pbb_writer_register_msgcontentprovider(&_dlep_writer,
      &_dlep_msgcontent_provider, _dlep_addrtlvs, ARRAYSIZE(_dlep_addrtlvs))) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Count not register DLEP msg contentprovider");
    pbb_writer_unregister_message(&_dlep_writer, _dlep_message);
    pbb_writer_cleanup(&_dlep_writer);
    return -1;
  }

  avl_init(&_session_tree, netaddr_socket_avlcmp, false, NULL);

  olsr_timer_add(&_tinfo_router_connect);

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
  struct _dlep_session *session, *s_it;

  /* remove all running sessions */
  avl_for_each_element_safe(&_session_tree, session, _node, s_it) {
    olsr_timer_stop(&session->interface_vtime);
    _cb_dlep_interface_timerout(session);
  }

  /* remove UDP socket */
  olsr_packet_remove_managed(&_dlep_socket, true);

  /* remove pbb reader */
  pbb_reader_remove_message_consumer(&_dlep_reader, &_dlep_message_consumer);
  pbb_reader_cleanup(&_dlep_reader);

  /* remove pbb writer */
  pbb_writer_unregister_content_provider(&_dlep_writer, &_dlep_msgcontent_provider,
      _dlep_addrtlvs, ARRAYSIZE(_dlep_addrtlvs));
  pbb_writer_unregister_message(&_dlep_writer, _dlep_message);
  pbb_writer_cleanup(&_dlep_writer);
  return 0;
}

static enum pbb_result
_parse_order_disconnect(void) {
  struct _dlep_session *session;

  session = avl_find_element(&_session_tree, _peer_socket, session, _node);
  if (session == NULL) {
    OLSR_INFO(LOG_DLEP_CLIENT, "Received DLEP disconnect from unknown peer");
    return PBB_DROP_MESSAGE;
  }

  /* call vtime callback */
  olsr_timer_stop(&session->interface_vtime);
  _cb_dlep_interface_timerout(session);

  return PBB_OKAY;
}

static enum pbb_result
_parse_order_interface_discovery(struct netaddr *radio_mac) {
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
      OLSR_WARN(LOG_DLEP_CLIENT, "Not enough memory for new dlep client session");
      return PBB_DROP_MESSAGE;
    }

    session->out_if.packet_buffer = calloc(1, DLEP_PKT_BUFFER_SIZE);
    if (session->out_if.packet_buffer == NULL) {
      free(session);
      OLSR_WARN(LOG_DLEP_CLIENT, "Not enough memory for packetbb output buffer");
      return PBB_DROP_MESSAGE;
    }
    session->out_if.packet_size = DLEP_PKT_BUFFER_SIZE;

    /* initialize new session */
    session->_node.key = &session->radio_mac;
    memcpy(&session->interface_socket, _peer_socket, sizeof(*_peer_socket));
    memcpy(&session->radio_mac, radio_mac, sizeof(*radio_mac));
    avl_insert(&_session_tree, &session->_node);

    session->interface_vtime.cb_context = session;
    session->interface_vtime.info = &_tinfo_interface_vtime;

    /* initialize interface */
    session->out_if.packet_buffer = NULL;
    session->out_if.packet_size = 0;
    session->out_if.sendPacket =_cb_send_dlep,

    /* register packetbb outgoing queue */
    pbb_writer_register_interface(&_dlep_writer, &session->out_if);
  }

  /* reset validity time for router session */
  olsr_timer_set(&session->interface_vtime, vtime);

  return PBB_OKAY;
}

static enum pbb_result
_cb_parse_dlep_message(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  struct netaddr radio_mac;

  if (context->addr_len != 6) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Address length of DLEP message should be 6 (but was %d)",
        context->addr_len);
    return PBB_DROP_MESSAGE;
  }

  if (netaddr_from_binary(&radio_mac, context->orig_addr, context->addr_len, AF_MAC48)) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Cannot parse DLEP originator address");
    return PBB_DROP_MESSAGE;
  }

  _current_order = _dlep_message_tlvs[IDX_TLV_ORDER].tlv->single_value[0];
  switch (_current_order) {
    /* received by both interface and router */
    case DLEP_ORDER_DISCONNECT:
      return _parse_order_disconnect();

#if 0
    /* only received by DLEP-interface */
    case DLEP_ORDER_CONNECT_ROUTER:
      return _parse_order_connect_router();

#endif
    /* only received by DLEP-router */
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      return _parse_order_interface_discovery(&radio_mac);
    case DLEP_ORDER_NEIGHBOR_UP:
      break;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      break;

    default:
      OLSR_WARN(LOG_DLEP_CLIENT, "Unknown order in DLEP message: %d", _current_order);
      return PBB_DROP_MESSAGE;
  }
  return PBB_OKAY;
}

static enum pbb_result
_cb_parse_dlep_message_failed(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  size_t i;
  OLSR_WARN(LOG_DLEP_CLIENT, "Constraints of incoming DLEP message were not fulfilled!");

  for (i=0; i < ARRAYSIZE(_dlep_message_tlvs); i++) {
    OLSR_WARN(LOG_DLEP_CLIENT, "block %zu: %s", i, _dlep_message_tlvs[i].tlv == NULL ? "no" : "yes");
  }
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
  OLSR_DEBUG(LOG_DLEP_CLIENT, "Parsing DLEP packet");

  _peer_socket = from;

  result = pbb_reader_handle_packet(&_dlep_reader, s->config.input_buffer, length);
  if (result) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Error while parsing DLEP packet: %s (%d)",
        pbb_strerror(result), result);
  }

  _peer_socket = NULL;
}

static void
_add_connectrouter_msgtlvs(void) {
  uint8_t encoded_vtime = 0;

  // TODO: calculate encoded vtime
  pbb_writer_add_messagetlv(&_dlep_writer,
      MSGTLV_VTIME, 0, &encoded_vtime, sizeof(encoded_vtime));

  if (_config.peer_type[0]) {
    pbb_writer_add_messagetlv(&_dlep_writer, DLEP_TLV_PEER_TYPE, 0,
        _config.peer_type, strlen(_config.peer_type));
  }
}

static void
_cb_addMessageHeader(struct pbb_writer *writer, struct pbb_writer_message *msg) {
  pbb_writer_set_msg_header(writer, msg, false, false, false, true);
  pbb_writer_set_msg_seqno(writer, msg, _msg_session->seqno++);
}

static void
_cb_addMessageTLVs(struct pbb_writer *writer,
    struct pbb_writer_content_provider *prv __attribute__((unused))) {

  pbb_writer_add_messagetlv(writer,
      DLEP_TLV_ORDER, _msg_order, NULL, 0);

  switch (_msg_order) {
    case DLEP_ORDER_CONNECT_ROUTER:
      _add_connectrouter_msgtlvs();
      break;
    default:
      OLSR_WARN(LOG_DLEP_CLIENT, "DLEP Message order %d not implemented yet", _msg_order);
      break;
  }
}
static void
_cb_dlep_interface_timerout(void *ptr) {
  struct _dlep_session *session = ptr;

  OLSR_DEBUG(LOG_DLEP_CLIENT, "Removing DLEP session");

  pbb_writer_unregister_interface(&_dlep_writer, &session->out_if);
  free(session->out_if.packet_buffer);

  avl_remove(&_session_tree, &session->_node);
  free(session);
}

static void
_cb_router_connect(void *ptr __attribute__((unused))) {
  if (avl_is_empty(&_session_tree))
    return;

  _msg_order = DLEP_ORDER_CONNECT_ROUTER;

  avl_for_each_element(&_session_tree, _msg_session, _node) {
    pbb_writer_create_message_singleif(&_dlep_writer, DLEP_MESSAGE_ID, &_msg_session->out_if);
    pbb_writer_flush(&_dlep_writer, &_msg_session->out_if, false);
  }
}

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
  olsr_timer_set(&_tentry_router_connect, _config.connect_interval);
}
