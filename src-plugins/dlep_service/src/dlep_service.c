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

/* DLEP TLV types */
enum dlep_tlv_types {
  DLEP_TLV_ORDER = 20,
  DLEP_TLV_IDENTIFICATION,
  DLEP_TLV_VERSION,
  DLEP_TLV_PEER_TYPE,
  DLEP_TLV_MAC,
  DLEP_TLV_IPV4,
  DLEP_TLV_IPV6,
  DLEP_TLV_MAXIMUM_DATARATE,
  DLEP_TLV_CURRENT_DATARATE,
  DLEP_TLV_LATENCY,
  DLEP_TLV_RESOURCES,
  DLEP_TLV_EXPECTED_FORWARDING_TIME,
  DLEP_TLV_RELATIVE_LINK_QUALITY,
  DLEP_TLV_PEER_TERMINATION,
  DLEP_TLV_STATUS,
  DLEP_TLV_HEARTBEAT_INTERVAL,
  DLEP_TLV_HEARTBEAT_TRESHOLD,
  DLEP_TLV_LINK_CHARACTERISTICS_ACK,
  DLEP_TLV_CREDIT_WINDOW_STATUS,
  DLEP_TLV_CREDIT_GRANT,
  DLEP_TLV_CREDIT_REQUEST,
};

/* DLEP TLV types */
enum dlep_idx {
  DLEP_IDX_ORDER,
  DLEP_IDX_IDENTIFICATION,
  DLEP_IDX_VERSION,
  DLEP_IDX_PEER_TYPE,
  DLEP_IDX_MAC,
  DLEP_IDX_IPV4,
  DLEP_IDX_IPV6,
  DLEP_IDX_MAXIMUM_DATARATE,
  DLEP_IDX_CURRENT_DATARATE,
  DLEP_IDX_LATENCY,
  DLEP_IDX_RESOURCES,
  DLEP_IDX_EXPECTED_FORWARDING_TIME,
  DLEP_IDX_RELATIVE_LINK_QUALITY,
  DLEP_IDX_PEER_TERMINATION,
  DLEP_IDX_STATUS,
  DLEP_IDX_HEARTBEAT_INTERVAL,
  DLEP_IDX_HEARTBEAT_TRESHOLD,
  DLEP_IDX_LINK_CHARACTERISTICS_ACK,
  DLEP_IDX_CREDIT_WINDOW_STATUS,
  DLEP_IDX_CREDIT_GRANT,
  DLEP_IDX_CREDIT_REQUEST,
};
/* definitions */
struct _dlep_config {
  struct strarray interf;
  struct olsr_packet_managed_config socket;
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
  CFG_MAP_ACL_V46(_dlep_config,
    socket.acl, "acl", "127.0.0.1", "Access control list for dlep interface"),
  CFG_MAP_NETADDR_V4(_dlep_config,
    socket.bindto_v4, "bindto_v4", "127.0.0.1", "Bind dlep ipv4 socket to this address", false),
  CFG_MAP_NETADDR_V6(_dlep_config,
    socket.bindto_v6, "bindto_v6", "::1", "Bind dlep ipv6 socket to this address", false),
  CFG_MAP_INT_MINMAX(_dlep_config,
    socket.port, "port", "2001", "Network port for dlep interface", 1, 65535),
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
  { .type = DLEP_TLV_ORDER, .mandatory = true, .min_length = 1, .match_length = true, .copy_value = NULL },
  { .type = DLEP_TLV_IDENTIFICATION, .mandatory = true, .min_length = 8, .match_length = true },
  { .type = DLEP_TLV_VERSION, .min_length = 4, .match_length = true },
  { .type = DLEP_TLV_PEER_TYPE, .min_length = 0, .match_length = true },
  { .type = DLEP_TLV_MAC, .min_length = 6, .match_length = true },
  { .type = DLEP_TLV_IPV4, .min_length = 4, .match_length = true },
  { .type = DLEP_TLV_IPV6, .min_length = 16, .match_length = true },
  { .type = DLEP_TLV_MAXIMUM_DATARATE, .min_length = 8, .match_length = true },
  { .type = DLEP_TLV_CURRENT_DATARATE, .min_length = 8, .match_length = true },
  { .type = DLEP_TLV_LATENCY, .min_length = 2, .match_length = true },
  { .type = DLEP_TLV_RESOURCES, .min_length = 1, .match_length = true },
  { .type = DLEP_TLV_EXPECTED_FORWARDING_TIME, .min_length = 4, .match_length = true },
  { .type = DLEP_TLV_RELATIVE_LINK_QUALITY, .min_length = 1, .match_length = true },
  { .type = DLEP_TLV_PEER_TERMINATION, .min_length = 1, .match_length = true },
  { .type = DLEP_TLV_STATUS, .min_length = 1, .match_length = true },
  { .type = DLEP_TLV_HEARTBEAT_INTERVAL, .min_length = 1, .match_length = true },
  { .type = DLEP_TLV_HEARTBEAT_TRESHOLD, .min_length = 1, .match_length = true },
  { .type = DLEP_TLV_LINK_CHARACTERISTICS_ACK, .min_length = 1, .match_length = true },
  { .type = DLEP_TLV_CREDIT_WINDOW_STATUS, .min_length = 16, .match_length = true },
  { .type = DLEP_TLV_CREDIT_GRANT, .min_length = 8, .match_length = true },
  { .type = DLEP_TLV_CREDIT_REQUEST, .min_length = 0, .match_length = true },
};

/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  cfg_schema_add_section(olsr_cfg_get_schema(), &_dlep_section,
      _dlep_entries, ARRAYSIZE(_dlep_entries));

  strarray_init(&_config.interf);
  return 0;
}

/**
 * Destructor of plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_unload(void) {
  strarray_free(&_config.interf);

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

static enum pbb_result
_cb_parse_dlep_message(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {

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
      DLEP_TLV_ORDER, 0x10, 1, 0x42,

      /* ID tlv */
      DLEP_TLV_IDENTIFICATION, 0x10, 8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
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
