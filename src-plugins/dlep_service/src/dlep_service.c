
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
#define _CFG_SECTION "dlep_service"

#define DLEP_MESSAGE_ID 42

enum dlep_orders {
  DLEP_ORDER_INTERFACE_DISCOVERY,
  DLEP_ORDER_CONNECT_ROUTER,
  DLEP_ORDER_DISCONNECT,
  DLEP_ORDER_NEIGHBOR_UP,
//  DLEP_ORDER_NEIGHBOR_DOWN,
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
// v DLEP_ADDRTLV_THROUGHPUT,
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

enum dlep_addrtlv_idx {
  IDX_ADDRTLV_CUR_RATE,
//  IDX_ADDRTLV_THROUGHPUT,
//  IDX_ADDRTLV_MAX_RATE,
//  IDX_ADDRTLV_IPv4,
//  IDX_ADDRTLV_IPv6,
};


/* definitions */
struct _dlep_config {
  struct olsr_packet_managed_config socket;

  char peer_type[81];

  uint64_t discovery_interval, discovery_validity;
  uint64_t address_interval, address_validity;
  uint64_t metric_interval, metric_validity;
};

struct _dlep_session {
  struct avl_node _node;

  union netaddr_socket router_socket;
  struct olsr_timer_entry router_vtime;
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
static void _cb_addAddresses(struct pbb_writer *,
    struct pbb_writer_content_provider *);

static void _cb_sendMulticast(struct pbb_writer *,
    struct pbb_writer_interface *, void *, size_t);

static void _cb_dlep_router_timerout(void *);
static void _cb_interface_discovery(void *);
static void _cb_address_update(void *);
static void _cb_metric_update(void *);

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
  CFG_MAP_CLOCK_MIN(_dlep_config, discovery_validity, "discovery_validity", "5.000",
    "Validity time in seconds for interface discovery messages", 100),

  CFG_MAP_CLOCK_MIN(_dlep_config, address_interval, "address_interval", "0.000",
    "Interval in seconds between neighbor up messages", 100),
  CFG_MAP_CLOCK_MIN(_dlep_config, address_validity, "address_validity", "5.000",
    "Validity time in seconds for neighbor up messages", 100),

  CFG_MAP_CLOCK_MIN(_dlep_config, metric_interval, "metric_interval", "1.000",
    "Interval in seconds between neighbor update messages", 100),
  CFG_MAP_CLOCK_MIN(_dlep_config, metric_validity, "metric_validity", "5.000",
    "Validity time in seconds for neighbor update messages", 100),
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
  [IDX_TLV_ORDER] =       { .type = DLEP_TLV_ORDER, .mandatory = true, .min_length = 0, .match_length = true },
  [IDX_TLV_VTIME] =       { .type = MSGTLV_VTIME, .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_PEER_TYPE] =   { .type = DLEP_TLV_PEER_TYPE, .min_length = 0, .max_length = 80, .match_length = true },
  [IDX_TLV_STATUS] =      { .type = DLEP_TLV_STATUS, .min_length = 1, .match_length = true },
};

/* DLEP writer data */
static uint8_t _msg_buffer[1500];
static uint8_t _msg_addrtlvs[5000];

static enum dlep_orders _msg_order;
static struct olsr_layer2_network *_msg_network;

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
  .addAddresses = _cb_addAddresses,
};

static struct pbb_writer_addrtlv_block _dlep_addrtlvs[] = {
  [IDX_ADDRTLV_CUR_RATE] = { .type = DLEP_ADDRTLV_CUR_RATE },
};

static uint8_t _packet_buffer[256];
static struct pbb_writer_interface _dlep_multicast = {
  .packet_buffer = _packet_buffer,
  .packet_size = sizeof(_packet_buffer),
  .sendPacket =_cb_sendMulticast,
};

/* temporary variables for parsing DLEP messages */
static enum dlep_orders _current_order;
static union netaddr_socket *_peer_socket;

/* DLEP session data */
static struct avl_tree _session_tree;

/* infrastructure */
struct olsr_timer_info _tinfo_router_vtime = {
  .name = "dlep router vtime",
  .callback = _cb_dlep_router_timerout,
};

struct olsr_timer_info _tinfo_interface_discovery = {
  .name = "dlep interface discovery",
  .callback = _cb_interface_discovery,
  .periodic = true,
};
struct olsr_timer_entry _tentry_interface_discovery = {
  .info = &_tinfo_interface_discovery,
};

struct olsr_timer_info _tinfo_address_update = {
  .name = "dlep address update",
  .callback = _cb_address_update,
  .periodic = true,
};
struct olsr_timer_entry _tentry_address_update = {
  .info = &_tinfo_address_update,
};

struct olsr_timer_info _tinfo_metric_update = {
  .name = "dlep metric update",
  .callback = _cb_metric_update,
  .periodic = true,
};
struct olsr_timer_entry _tentry_metric_update = {
  .info = &_tinfo_metric_update,
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
  pbb_writer_init(&_dlep_writer);

  _dlep_message = pbb_writer_register_message(&_dlep_writer, DLEP_MESSAGE_ID, true, 6);
  if (_dlep_message == NULL) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Could not register DLEP message");
    pbb_writer_cleanup(&_dlep_writer);
    return -1;
  }
  _dlep_message->addMessageHeader = _cb_addMessageHeader;

  if (pbb_writer_register_msgcontentprovider(&_dlep_writer,
      &_dlep_msgcontent_provider, _dlep_addrtlvs, ARRAYSIZE(_dlep_addrtlvs))) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Count not register DLEP msg contentprovider");
    pbb_writer_unregister_message(&_dlep_writer, _dlep_message);
    pbb_writer_cleanup(&_dlep_writer);
    return -1;
  }

  pbb_writer_register_interface(&_dlep_writer, &_dlep_multicast);

  avl_init(&_session_tree, netaddr_socket_avlcmp, false, NULL);

  olsr_timer_add(&_tinfo_interface_discovery);
  olsr_timer_add(&_tinfo_address_update);
  olsr_timer_add(&_tinfo_metric_update);

  pbb_reader_init(&_dlep_reader);
  pbb_reader_add_message_consumer(&_dlep_reader, &_dlep_message_consumer,
      _dlep_message_tlvs, ARRAYSIZE(_dlep_message_tlvs), DLEP_MESSAGE_ID, 0);

  olsr_packet_add_managed(&_dlep_socket);


  // TODO: remove!
  {
    struct netaddr radio_mac, n1_mac, n2_mac;
    struct olsr_layer2_network *net;
    struct olsr_layer2_neighbor *neigh1, *neigh2;

    if (netaddr_from_string(&radio_mac, "1:00:00:00:00:01")) {;}
    if (netaddr_from_string(&n1_mac, "2:00:00:00:00:01")) {;}
    if (netaddr_from_string(&n2_mac, "2:00:00:00:00:02")) {;}

    net = olsr_layer2_add_network(&radio_mac, 1, 0);
    olsr_layer2_network_set_last_seen(net, 1000);

    neigh1 = olsr_layer2_add_neighbor(&radio_mac, &n1_mac, 1, 0);
    olsr_layer2_neighbor_set_tx_bitrate(neigh1, 1000000);

    neigh2 = olsr_layer2_add_neighbor(&radio_mac, &n2_mac, 1, 0);
    olsr_layer2_neighbor_set_tx_bitrate(neigh2, 2000000);
  }

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

  olsr_packet_remove_managed(&_dlep_socket, true);

  pbb_reader_remove_message_consumer(&_dlep_reader, &_dlep_message_consumer);
  pbb_reader_cleanup(&_dlep_reader);

  pbb_writer_unregister_content_provider(&_dlep_writer, &_dlep_msgcontent_provider,
      _dlep_addrtlvs, ARRAYSIZE(_dlep_addrtlvs));
  pbb_writer_unregister_message(&_dlep_writer, _dlep_message);
  pbb_writer_cleanup(&_dlep_writer);

  olsr_timer_remove(&_tinfo_interface_discovery);
  olsr_timer_remove(&_tinfo_address_update);
  olsr_timer_remove(&_tinfo_metric_update);

  olsr_acl_remove(&_config.socket.acl);
  return 0;
}

static enum pbb_result
_parse_order_disconnect(void) {
  struct _dlep_session *session;

  session = avl_find_element(&_session_tree, _peer_socket, session, _node);
  if (session == NULL) {
    OLSR_INFO(LOG_DLEP_SERVICE, "Received DLEP disconnect from unknown peer");
    return PBB_DROP_MESSAGE;
  }

  OLSR_DEBUG(LOG_DLEP_SERVICE, "DLEP disconnect");

  /* call vtime callback */
  _cb_dlep_router_timerout(session);

  return PBB_OKAY;
}

static enum pbb_result
_parse_order_connect_router(void) {
  struct _dlep_session *session;
  uint8_t encoded_vtime;
  uint64_t vtime;
  struct netaddr_str buf;

  encoded_vtime = _dlep_message_tlvs[IDX_TLV_VTIME].tlv->single_value[0];

  /* TODO: decode vtime according to RFC 5497 */
  vtime = 0 * encoded_vtime + 10000;

  session = avl_find_element(&_session_tree, _peer_socket, session, _node);
  if (session == NULL) {
    OLSR_DEBUG(LOG_DLEP_SERVICE, "New DLEP router session for %s",
        netaddr_socket_to_string(&buf, _peer_socket));

    /* allocate new session */
    session = calloc(1, sizeof(*session));
    if (session == NULL) {
      OLSR_WARN(LOG_DLEP_SERVICE, "Not enough memory for new dlep session");
      return PBB_DROP_MESSAGE;
    }

    /* initialize new session */
    session->_node.key = &session->router_socket;
    memcpy(&session->router_socket, _peer_socket, sizeof(*_peer_socket));

    session->router_vtime.cb_context = session;
    session->router_vtime.info = &_tinfo_router_vtime;

    avl_insert(&_session_tree, &session->_node);
  }

  /* reset validity time for router session */
  olsr_timer_set(&session->router_vtime, vtime);
  return PBB_OKAY;
}

static enum pbb_result
_cb_parse_dlep_message(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  if (context->addr_len != 6) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Address length of DLEP message should be 6 (but was %d)",
        context->addr_len);
    return PBB_DROP_MESSAGE;
  }

  _msg_order = DLEP_ORDER_INTERFACE_DISCOVERY;

  _current_order = _dlep_message_tlvs[IDX_TLV_ORDER].tlv->type_ext;
  switch (_current_order) {
    case DLEP_ORDER_DISCONNECT:
      return _parse_order_disconnect();
    case DLEP_ORDER_CONNECT_ROUTER:
      return _parse_order_connect_router();
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      /* ignore our own discovery packets if we work with multicast loop */
      return PBB_OKAY;
    case DLEP_ORDER_NEIGHBOR_UP:
      /* ignore our own discovery packets if we work with multicast loop */
      break;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      /* ignore our own discovery packets if we work with multicast loop */
      break;
    default:
      OLSR_WARN(LOG_DLEP_SERVICE, "Unknown order in DLEP message: %d", _current_order);
      return PBB_DROP_MESSAGE;
  }
  return PBB_OKAY;
}

static enum pbb_result
_cb_parse_dlep_message_failed(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  size_t i;
  OLSR_WARN(LOG_DLEP_SERVICE, "Constraints of incoming DLEP message were not fulfilled!");

  for (i=0; i < ARRAYSIZE(_dlep_message_tlvs); i++) {
    OLSR_WARN(LOG_DLEP_SERVICE, "block %zu: %s", i, _dlep_message_tlvs[i].tlv == NULL ? "no" : "yes");
    if (_dlep_message_tlvs[i].tlv) {
      OLSR_WARN_NH(LOG_DLEP_SERVICE, "\tvalue length: %u", _dlep_message_tlvs[i].tlv->length);
    }
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
#if !defined(REMOVE_LOG_DEBUG)
  struct netaddr_str buf;
#endif
  OLSR_DEBUG(LOG_DLEP_SERVICE, "Parsing DLEP packet from %s",
      netaddr_socket_to_string(&buf, from));

  _peer_socket = from;

  result = pbb_reader_handle_packet(&_dlep_reader, s->config.input_buffer, length);
  if (result) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Error while parsing DLEP packet: %s (%d)",
        pbb_strerror(result), result);
  }

  _peer_socket = NULL;
}

static void
_add_ifdiscovery_msgtlvs(void) {
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
_add_neighborup_msgtlvs(void) {
  uint8_t encoded_vtime = 0;

  // TODO: calculate encoded vtime
  pbb_writer_add_messagetlv(&_dlep_writer,
      MSGTLV_VTIME, 0, &encoded_vtime, sizeof(encoded_vtime));
}

static void
_add_neighborupdate_msgtlvs(void) {
  uint8_t encoded_vtime = 0;

  // TODO: calculate encoded vtime
  pbb_writer_add_messagetlv(&_dlep_writer,
      MSGTLV_VTIME, 0, &encoded_vtime, sizeof(encoded_vtime));
}

static void
_cb_addMessageHeader(struct pbb_writer *writer, struct pbb_writer_message *msg) {
  static uint16_t seqno = 0;
  pbb_writer_set_msg_header(writer, msg, true, false, false, true);
  pbb_writer_set_msg_originator(writer, msg, netaddr_get_binptr(&_msg_network->radio_id));
  pbb_writer_set_msg_seqno(writer, msg, seqno++);
}

static void
_cb_addMessageTLVs(struct pbb_writer *writer,
    struct pbb_writer_content_provider *prv __attribute__((unused))) {

  pbb_writer_add_messagetlv(writer,
      DLEP_TLV_ORDER, _msg_order, NULL, 0);

  switch (_msg_order) {
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      _add_ifdiscovery_msgtlvs();
      break;
    case DLEP_ORDER_NEIGHBOR_UP:
      _add_neighborup_msgtlvs();
      break;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      _add_neighborupdate_msgtlvs();
      break;
    default:
      OLSR_WARN(LOG_DLEP_SERVICE, "DLEP Message order %d not implemented yet", _msg_order);
      break;
  }
}

static void
_add_neighborup_addresses(void) {
  struct olsr_layer2_neighbor *neigh, *neigh_it;
  struct netaddr_str buf1, buf2;

  OLSR_FOR_ALL_LAYER2_NEIGHBORS(neigh, neigh_it) {
    if (netaddr_cmp(&_msg_network->radio_id, &neigh->key.radio_mac) == 0) {
      pbb_writer_add_address(&_dlep_writer, _dlep_message,
          netaddr_get_binptr(&neigh->key.neighbor_mac), 48);

      OLSR_DEBUG(LOG_DLEP_SERVICE, "Added neighbor %s (seen by %s) to neigh-up",
          netaddr_to_string(&buf1, &neigh->key.neighbor_mac),
          netaddr_to_string(&buf2, &neigh->key.radio_mac));
    }
  }
}

static void
_add_neighborupdate_addresses(void) {
  struct olsr_layer2_neighbor *neigh, *neigh_it;
  struct pbb_writer_address *addr;
  struct netaddr_str buf1, buf2;

  OLSR_FOR_ALL_LAYER2_NEIGHBORS(neigh, neigh_it) {
    if (netaddr_cmp(&_msg_network->radio_id, &neigh->key.radio_mac) != 0) {
      continue;
    }

    addr = pbb_writer_add_address(&_dlep_writer, _dlep_message,
          netaddr_get_binptr(&neigh->key.neighbor_mac), 48);
    if (addr == NULL) {
      OLSR_WARN(LOG_DLEP_SERVICE, "Could not allocate address for neighbor update");
      break;
    }

    OLSR_DEBUG(LOG_DLEP_SERVICE, "Added neighbor %s (seen by %s) to neigh-up",
        netaddr_to_string(&buf1, &neigh->key.neighbor_mac),
        netaddr_to_string(&buf2, &neigh->key.radio_mac));

    if (olsr_layer2_neighbor_has_tx_bitrate(neigh)) {
      uint64_t rate;

      rate = neigh->tx_bitrate;
      // TODO: htonll ?
      pbb_writer_add_addrtlv(&_dlep_writer, addr,
          _dlep_addrtlvs[IDX_ADDRTLV_CUR_RATE]._tlvtype, &rate, sizeof(rate), false);

      OLSR_DEBUG(LOG_DLEP_SERVICE, "Added bitrate of %s (measured by %s): %"PRIu64,
          netaddr_to_string(&buf1, &neigh->key.neighbor_mac),
          netaddr_to_string(&buf2, &neigh->key.radio_mac), rate);
    }
  }
}

static void
_cb_addAddresses(struct pbb_writer *writer __attribute__((unused)),
    struct pbb_writer_content_provider *cpr __attribute__((unused))) {
  switch (_msg_order) {
    case DLEP_ORDER_INTERFACE_DISCOVERY:
    case DLEP_ORDER_DISCONNECT:
      /* no addresses and address TLVs */
      break;
    case DLEP_ORDER_NEIGHBOR_UP:
      _add_neighborup_addresses();
      break;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      _add_neighborupdate_addresses();
      break;
    default:
      OLSR_WARN(LOG_DLEP_SERVICE, "DLEP Message order %d not implemented yet", _msg_order);
      break;
  }
}

static void
_cb_dlep_router_timerout(void *ptr) {
  struct _dlep_session *session = ptr;

  OLSR_DEBUG(LOG_DLEP_SERVICE, "Removing DLEP session");

  /* might have been called directly */
  olsr_timer_stop(&session->router_vtime);

  avl_remove(&_session_tree, &session->_node);
  free(session);
}

static void
_cb_interface_discovery(void *ptr __attribute__((unused))) {
  struct olsr_layer2_network *net_it;
  struct netaddr_str buf;

  _msg_order = DLEP_ORDER_INTERFACE_DISCOVERY;
  OLSR_FOR_ALL_LAYER2_NETWORKS(_msg_network, net_it) {
    OLSR_DEBUG(LOG_DLEP_SERVICE, "Send interface discovery for radio %s",
        netaddr_to_string(&buf, &_msg_network->radio_id));
    pbb_writer_create_message_singleif(&_dlep_writer, DLEP_MESSAGE_ID, &_dlep_multicast);
    pbb_writer_flush(&_dlep_writer, &_dlep_multicast, false);
  }
}

static void
_cb_address_update(void *ptr __attribute__((unused))) {
  struct olsr_layer2_network *net_it;
  struct netaddr_str buf;

  if (avl_is_empty(&_session_tree))
    return;

  _msg_order = DLEP_ORDER_NEIGHBOR_UP;
  OLSR_FOR_ALL_LAYER2_NETWORKS(_msg_network, net_it) {
    OLSR_DEBUG(LOG_DLEP_SERVICE, "Send neighbor up for radio %s",
        netaddr_to_string(&buf, &_msg_network->radio_id));
    pbb_writer_create_message_singleif(&_dlep_writer, DLEP_MESSAGE_ID, &_dlep_multicast);
    pbb_writer_flush(&_dlep_writer, &_dlep_multicast, false);
  }
}

static void
_cb_metric_update(void *ptr __attribute__((unused))) {
  struct olsr_layer2_network *net_it;

  if (avl_is_empty(&_session_tree))
    return;

  _msg_order = DLEP_ORDER_NEIGHBOR_UPDATE;
  OLSR_FOR_ALL_LAYER2_NETWORKS(_msg_network, net_it) {
    pbb_writer_create_message_singleif(&_dlep_writer, DLEP_MESSAGE_ID, &_dlep_multicast);
    pbb_writer_flush(&_dlep_writer, &_dlep_multicast, false);
  }
}

static void
_cb_sendMulticast(struct pbb_writer *writer __attribute__((unused)),
    struct pbb_writer_interface *interf __attribute__((unused)),
    void *ptr, size_t len) {
  if (config_global.ipv4
      && olsr_packet_send_managed_multicast(&_dlep_socket, true, ptr, len) < 0) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Could not sent DLEP IPv4 packet to socket");
  }
  if (config_global.ipv6
      && olsr_packet_send_managed_multicast(&_dlep_socket, false, ptr, len) < 0) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Could not sent DLEP IPv6 packet to socket");
  }
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
  olsr_timer_set(&_tentry_interface_discovery, _config.discovery_interval);
  olsr_timer_set(&_tentry_address_update, _config.address_interval);
  olsr_timer_set(&_tentry_metric_update, _config.metric_interval);
}
