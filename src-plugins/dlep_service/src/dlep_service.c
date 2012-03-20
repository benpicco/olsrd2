
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
  DLEP_ORDER_PEER_DISCOVERY,
  DLEP_ORDER_PEER_OFFER,
  DLEP_ORDER_PEER_TERMINATION,
  DLEP_ORDER_PEER_UPDATE,
  DLEP_ORDER_LINK_CHARACTERISTICS_REQUEST,
  DLEP_ORDER_HEARTBEAT,
  DLEP_ORDER_NEIGHBOR_UP,
  DLEP_ORDER_NEIGHBOR_DOWN,
  DLEP_ORDER_NEIGHBOR_UPDATE,
  DLEP_ORDER_NEIGHBOR_ADDRESS_UPDATE,
  DLEP_ORDER_ACK,
};

/* DLEP TLV types */
enum dlep_msgtlv_types {
  DLEP_TLV_ORDER = 192,
  DLEP_TLV_ID,
  DLEP_TLV_VERSION,
  DLEP_TLV_PEER_TYPE,
  DLEP_TLV_REFERENCE,
  DLEP_TLV_STATUS,
  DLEP_TLV_HB_INTERVAL,
  DLEP_TLV_HB_VTIME,

  DLEP_TLV_MAX_RATE,
  DLEP_TLV_CUR_RATE,
  DLEP_TLV_LATENCY,
  DLEP_TLV_EXP_FORW,
  DLEP_TLV_RESOURCES,
  DLEP_TLV_REL_LQ,
};

enum dlep_addrtlv_types {
  DLEP_ADDRTLV_ADDRESSTYPE,
};

/* DLEP TLV array index */
enum dlep_tlv_idx {
  DLEP_IDX_ORDER,
  DLEP_IDX_ID,
  DLEP_IDX_VERSION,
  DLEP_IDX_PEER_TYPE,
  DLEP_IDX_REFERENCE,
  DLEP_IDX_STATUS,
  DLEP_IDX_HB_INTERVAL,
  DLEP_IDX_HB_VTIME,

  DLEP_IDX_MAX_RATE,
  DLEP_IDX_CUR_RATE,
  DLEP_IDX_LATENCY,
  DLEP_IDX_EXP_FORW,
  DLEP_IDX_RESOURCES,
  DLEP_IDX_REL_LQ,
};
/* definitions */
struct _dlep_config {
  char dlep_if[IF_NAMESIZE];
  struct strarray radio_id;
  struct olsr_packet_managed_config socket;

  char peer_type[81];

  uint64_t peer_discovery_interval;
  uint64_t heartbeat_interval, heartbeat_vtime;
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
  CFG_MAP_STRINGLIST(_dlep_config, dlep_if, "dlep_if", "lo",
      "List of interfaces to sent DLEP broadcasts to"),
  CFG_MAP_STRING_ARRAY(_dlep_config, radio_id, "radio_id", "wlan0",
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
  CFG_MAP_CLOCK(_dlep_config, peer_discovery_interval, "discovery_interval", "0",
    "Interval in seconds between peer discovery messages (0 for no usage of PEER DISCOVERY message"),
  CFG_MAP_CLOCK(_dlep_config, heartbeat_interval, "heartbeat_interval", "0",
    "Interval in seconds between heartbeat messages (0 for no usage of HEARTBEAT message)"),
  CFG_MAP_CLOCK(_dlep_config, heartbeat_vtime, "heartbeat_vtime", "0",
    "Validity time of heartbeat in seconds (0 for validity three times as long as interval)"),
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
  [DLEP_IDX_ORDER] =       { .type = DLEP_TLV_ORDER, .mandatory = true, .min_length = 1, .match_length = true },
  [DLEP_IDX_ID] =          { .type = DLEP_TLV_ID, .min_length = 8, .match_length = true },
  [DLEP_IDX_VERSION] =     { .type = DLEP_TLV_VERSION, .min_length = 4, .match_length = true },
  [DLEP_IDX_PEER_TYPE] =   { .type = DLEP_TLV_PEER_TYPE, .min_length = 0, .match_length = true },
  [DLEP_IDX_REFERENCE] =   { .type = DLEP_TLV_REFERENCE, .min_length = 2, .match_length = true },
  [DLEP_IDX_STATUS] =      { .type = DLEP_TLV_STATUS, .min_length = 1, .match_length = true },
  [DLEP_IDX_HB_INTERVAL] = { .type = DLEP_TLV_HB_INTERVAL, .min_length = 1, .match_length = true },
  [DLEP_IDX_HB_VTIME] =    { .type = DLEP_TLV_HB_VTIME, .min_length = 1, .match_length = true },

  [DLEP_IDX_MAX_RATE] =    { .type = DLEP_TLV_MAX_RATE, .min_length = 8, .match_length = true },
  [DLEP_IDX_CUR_RATE] =    { .type = DLEP_TLV_CUR_RATE, .min_length = 8, .match_length = true },
  [DLEP_IDX_LATENCY] =     { .type = DLEP_TLV_LATENCY, .min_length = 2, .match_length = true },
  [DLEP_IDX_EXP_FORW] =    { .type = DLEP_TLV_EXP_FORW, .min_length = 4, .match_length = true },
  [DLEP_IDX_RESOURCES] =   { .type = DLEP_TLV_RESOURCES, .min_length = 1, .match_length = true },
  [DLEP_IDX_REL_LQ] =      { .type = DLEP_TLV_REL_LQ, .min_length = 1, .match_length = true },
};

/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  cfg_schema_add_section(olsr_cfg_get_schema(), &_dlep_section,
      _dlep_entries, ARRAYSIZE(_dlep_entries));

  strarray_init(&_config.radio_id);
  return 0;
}

/**
 * Destructor of plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_unload(void) {
  strarray_free(&_config.radio_id);

  cfg_schema_remove_section(olsr_cfg_get_schema(), &_dlep_section);
  return 0;
}

/**
 * Enable plugin
 * @return -1 if netlink socket could not be opened, 0 otherwise
 */
static int
_cb_plugin_enable(void) {
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
  pbb_reader_cleanup(&_dlep_reader);
  return 0;
}

static void
_handle_dlep_peer_offer(void) {
  uint32_t router_id, interface_id;

  /*
   * Message TLVs:
   * - DLEP-Order TLV (mandatory)
   * - Identification TLV (optional)
   * - Version TLV (optional)
   * - Peer Type TLV (optional)
   * - Heartbeat Interval TLV (optional)
   * - Heartbeat Validity Time TLV (optional)
   */

  if (_dlep_message_tlvs[DLEP_IDX_ID].tlv == NULL) {
    OLSR_WARN(LOG_PLUGINS, "Received DLEP peer offer without identification");
    return;
  }

  memcpy(&interface_id, _dlep_message_tlvs[DLEP_IDX_ID].tlv->single_value, sizeof(interface_id));
  memcpy(&router_id, _dlep_message_tlvs[DLEP_IDX_ID].tlv->single_value + 4, sizeof(router_id));
  interface_id = ntohl(interface_id);
  router_id = ntohl(router_id);

}

static enum pbb_result
_cb_parse_dlep_message(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  uint8_t order;

  order = _dlep_message_tlvs[DLEP_IDX_ORDER].tlv->single_value[0];
  switch (order) {
    case DLEP_ORDER_PEER_OFFER:
      _handle_dlep_peer_offer();
      break;
    case DLEP_ORDER_PEER_TERMINATION:
      break;
    case DLEP_ORDER_LINK_CHARACTERISTICS_REQUEST:
      break;
    case DLEP_ORDER_HEARTBEAT:
      break;
    case DLEP_ORDER_ACK:
      break;
    default:
      OLSR_WARN(LOG_PLUGINS, "Unknown order in DLEP message: %d", order);
      break;
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
      union netaddr_socket *from __attribute__((unused)),
      size_t length __attribute__((unused))) {
  enum pbb_result result;
  uint8_t dummy[] __attribute__((unused)) = {
      /* packet header */
      0x00,

      /* message header */
      DLEP_MESSAGE_ID, 0x10, 0x00, 23, 0x11, 0x11,

      /* tlv block */
      0x00, 15,

      /* order TLV */
      DLEP_TLV_ORDER, 0x10, 1, DLEP_ORDER_PEER_OFFER,

      /* ID tlv */
      DLEP_TLV_ID, 0x10, 8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
  };

  OLSR_DEBUG(LOG_PLUGINS, "Parsing DLEP packet");

  //  result = pbb_reader_handle_packet(&_dlep_reader, s->config.input_buffer, length);
  result = pbb_reader_handle_packet(&_dlep_reader, dummy, sizeof(dummy));
  if (result) {
    OLSR_WARN(LOG_PLUGINS, "Error while parsing DLEP packet: %s (%d)",
        pbb_strerror(result), result);
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
}
