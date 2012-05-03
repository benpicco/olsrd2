
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

#include "packetbb/pbb_conversion.h"
#include "packetbb/pbb_iana.h"
#include "packetbb/pbb_reader.h"
#include "core/olsr_logging.h"
#include "core/olsr.h"

#include "olsr_layer2.h"

#include "dlep_iana.h"
#include "dlep_client.h"
#include "dlep_client_incoming.h"

/* DLEP TLV array index */
enum dlep_tlv_idx {
  IDX_TLV_VTIME,
  IDX_TLV_ORDER,
  IDX_TLV_PEER_TYPE,
  IDX_TLV_SSID,
  IDX_TLV_LAST_SEEN,
  IDX_TLV_FREQUENCY,
  IDX_TLV_SUPPORTED_RATES,
};

enum dlep_addrtlv_idx {
  IDX_ADDRTLV_LINK_STATUS,
  IDX_ADDRTLV_SIGNAL,
  IDX_ADDRTLV_LAST_SEEN,
  IDX_ADDRTLV_RX_BITRATE,
  IDX_ADDRTLV_RX_BYTES,
  IDX_ADDRTLV_RX_PACKETS,
  IDX_ADDRTLV_TX_BITRATE,
  IDX_ADDRTLV_TX_BYTES,
  IDX_ADDRTLV_TX_PACKETS,
  IDX_ADDRTLV_TX_RETRIES,
  IDX_ADDRTLV_TX_FAILED,
};

/* callback prototypes */
static enum pbb_result _cb_parse_dlep_message(
    struct pbb_reader_tlvblock_consumer *consumer,
    struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_parse_dlep_message_failed(
    struct pbb_reader_tlvblock_consumer *consumer,
    struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_parse_dlep_addresses(
    struct pbb_reader_tlvblock_consumer *consumer,
    struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_parse_dlep_addresses_failed(
    struct pbb_reader_tlvblock_consumer *consumer,
    struct pbb_reader_tlvblock_context *context);

/* DLEP reader data */
static struct pbb_reader _dlep_reader;

static struct pbb_reader_tlvblock_consumer _dlep_message_consumer = {
  .block_callback = _cb_parse_dlep_message,
  .block_callback_failed_constraints = _cb_parse_dlep_message_failed,
};

static struct pbb_reader_tlvblock_consumer_entry _dlep_message_tlvs[] = {
  [IDX_TLV_ORDER]           = { .type = DLEP_TLV_ORDER, .mandatory = true, .min_length = 0, .match_length = true },
  [IDX_TLV_VTIME]           = { .type = PBB_MSGTLV_VALIDITY_TIME, .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_PEER_TYPE]       = { .type = DLEP_TLV_PEER_TYPE, .min_length = 0, .max_length = 80, .match_length = true },
  [IDX_TLV_SSID]            = { .type = DLEP_TLV_SSID, .min_length = 6, .match_length = true },
  [IDX_TLV_LAST_SEEN]       = { .type = DLEP_TLV_LAST_SEEN, .min_length = 4, .match_length = true },
  [IDX_TLV_FREQUENCY]       = { .type = DLEP_TLV_FREQUENCY, .min_length = 8, .match_length = true },
  [IDX_TLV_SUPPORTED_RATES] = { .type = DLEP_TLV_SUPPORTED_RATES },
};

static struct pbb_reader_tlvblock_consumer _dlep_address_consumer = {
  .block_callback = _cb_parse_dlep_addresses,
  .block_callback_failed_constraints = _cb_parse_dlep_addresses_failed,
};

static struct pbb_reader_tlvblock_consumer_entry _dlep_address_tlvs[] = {
  [IDX_ADDRTLV_LINK_STATUS] = { .type = PBB_ADDRTLV_LINK_STATUS, .min_length = 1, .match_length = true },
  [IDX_ADDRTLV_SIGNAL]      = { .type = DLEP_ADDRTLV_SIGNAL, .min_length = 2 , .match_length = true },
  [IDX_ADDRTLV_LAST_SEEN]   = { .type = DLEP_ADDRTLV_LAST_SEEN, .min_length = 4, .match_length = true },
  [IDX_ADDRTLV_RX_BITRATE]  = { .type = DLEP_ADDRTLV_RX_BITRATE, .min_length = 8, .match_length = true },
  [IDX_ADDRTLV_RX_BYTES]    = { .type = DLEP_ADDRTLV_RX_BYTES, .min_length = 4, .match_length = true },
  [IDX_ADDRTLV_RX_PACKETS]  = { .type = DLEP_ADDRTLV_RX_PACKETS, .min_length = 4, .match_length = true },
  [IDX_ADDRTLV_TX_BITRATE]  = { .type = DLEP_ADDRTLV_TX_BITRATE, .min_length = 8, .match_length = true },
  [IDX_ADDRTLV_TX_BYTES]    = { .type = DLEP_ADDRTLV_TX_BYTES, .min_length = 4, .match_length = true },
  [IDX_ADDRTLV_TX_PACKETS]  = { .type = DLEP_ADDRTLV_TX_PACKETS, .min_length = 4, .match_length = true },
  [IDX_ADDRTLV_TX_RETRIES]  = { .type = DLEP_ADDRTLV_TX_RETRIES, .min_length = 4, .match_length = true },
  [IDX_ADDRTLV_TX_FAILED]   = { .type = DLEP_ADDRTLV_TX_FAILED, .min_length = 4, .match_length = true },
};

/* temporary variables for parsing DLEP messages */
static enum dlep_orders _message_order;
static union netaddr_socket *_message_peer_socket;
static uint64_t _message_vtime;
static bool _message_multicast;

/* incoming subsystem */
OLSR_SUBSYSTEM_STATE(_dlep_client_incoming);

/**
 * Initialize DLEP Client RFC5444 processing
 */
void
dlep_client_incoming_init(void) {
  if (olsr_subsystem_init(&_dlep_client_incoming))
    return;

  pbb_reader_init(&_dlep_reader);
  pbb_reader_add_message_consumer(&_dlep_reader, &_dlep_message_consumer,
      _dlep_message_tlvs, ARRAYSIZE(_dlep_message_tlvs), DLEP_MESSAGE_ID, 0);
  pbb_reader_add_address_consumer(&_dlep_reader, &_dlep_address_consumer,
      _dlep_address_tlvs, ARRAYSIZE(_dlep_address_tlvs), DLEP_MESSAGE_ID, 1);
}

/**
 * Cleanup all data allocated for RFC 5444 processing
 */
void
dlep_client_incoming_cleanup(void) {
  if (olsr_subsystem_cleanup(&_dlep_client_incoming))
    return;

  pbb_reader_remove_address_consumer(&_dlep_reader, &_dlep_address_consumer);
  pbb_reader_remove_message_consumer(&_dlep_reader, &_dlep_message_consumer);
  pbb_reader_cleanup(&_dlep_reader);
}

/**
 * Parse incoming DLEP packet
 * @param ptr pointer to binary packet
 * @param length length of packet
 * @param from socket the packet came from
 * @param multicast true if packet was received by the multicast socket
 */
void
dlep_service_incoming_parse(void *ptr, size_t length,
    union netaddr_socket *from, bool multicast) {
  enum pbb_result result;
#if !defined(REMOVE_LOG_DEBUG)
  struct netaddr_str buf;
#endif

  OLSR_DEBUG(LOG_DLEP_CLIENT, "Parsing DLEP packet from %s (%s)",
      netaddr_socket_to_string(&buf, from),
      multicast ? "multicast" : "unicast");

  _message_peer_socket = from;
  _message_multicast = multicast;

  result = pbb_reader_handle_packet(&_dlep_reader, ptr, length);
  if (result) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Error while parsing DLEP packet: %s (%d)",
        pbb_strerror(result), result);
  }


  _message_peer_socket = NULL;
}

/**
 * Parse message TLVs of incoming Interface Discovery DLEP messages
 * @param radio_mac originator MAC of message
 * @return PBB_OKAY if message was okay, PBB_DROP_MESSAGE otherwise
 */
static enum pbb_result
_parse_msg_interface_discovery(struct netaddr *radio_mac) {
  if (!dlep_add_interface_session(_message_peer_socket, radio_mac, _message_vtime)) {
    return PBB_DROP_MESSAGE;
  }
  return PBB_OKAY;
}

/**
 * Parse message TLVs of incoming Interface Discovery DLEP messages
 * @param radio_mac originator MAC of message
 * @return PBB_OKAY if message was okay, PBB_DROP_MESSAGE otherwise
 */
static enum pbb_result
_parse_msg_neighbor_update(struct netaddr *radio_mac) {
  struct olsr_layer2_network *net;
  struct netaddr_str buf;

  OLSR_DEBUG(LOG_DLEP_CLIENT, "Got layer2 network %s",
      netaddr_to_string(&buf, radio_mac));

  net = olsr_layer2_add_network(radio_mac, 0, _message_vtime);
  if (net == NULL) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Cannot allocate new layer2 network %s",
        netaddr_to_string(&buf, radio_mac));
    return PBB_DROP_MESSAGE;
  }

  olsr_layer2_network_clear(net);

  if (_dlep_message_tlvs[IDX_TLV_SSID].tlv) {
    struct netaddr ssid;

    netaddr_from_binary(&ssid, _dlep_message_tlvs[IDX_TLV_SSID].tlv->single_value, 6, AF_MAC48);
    olsr_layer2_network_set_ssid(net, &ssid);
  }
  if (_dlep_message_tlvs[IDX_TLV_LAST_SEEN].tlv) {
    int32_t last_seen;

    memcpy(&last_seen,
        _dlep_message_tlvs[IDX_TLV_LAST_SEEN].tlv->single_value,
        sizeof(last_seen));

    last_seen = ntohl(last_seen);
    olsr_layer2_network_set_last_seen(net, last_seen);
  }
  if (_dlep_message_tlvs[IDX_TLV_FREQUENCY].tlv) {
    uint64_t freq;

    memcpy(&freq,
        _dlep_message_tlvs[IDX_TLV_FREQUENCY].tlv->single_value,
        sizeof(freq));

    freq = be64toh(freq);
    olsr_layer2_network_set_frequency(net, freq);
  }
  // TODO: add supported datarates
  return PBB_OKAY;
}

/**
 * Callback for parsing message TLVs of incoming DLEP messages
 * @param consumer
 * @param context
 * @return
 */
static enum pbb_result
_cb_parse_dlep_message(struct pbb_reader_tlvblock_consumer *consumer __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context) {
  struct netaddr radio_mac;
  uint8_t encoded_vtime;

  OLSR_DEBUG(LOG_DLEP_CLIENT, "Parse DLEP message");
  if (context->addr_len != 6) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Address length of DLEP message should be 6 (but was %d)",
        context->addr_len);
    return PBB_DROP_MESSAGE;
  }

  if (netaddr_from_binary(&radio_mac, context->orig_addr, context->addr_len, AF_MAC48)) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Cannot parse DLEP originator address");
    return PBB_DROP_MESSAGE;
  }

  encoded_vtime = _dlep_message_tlvs[IDX_TLV_VTIME].tlv->single_value[0];

  /* decode vtime according to RFC 5497 */
  _message_vtime = pbb_timetlv_decode(encoded_vtime);


  _message_order = _dlep_message_tlvs[IDX_TLV_ORDER].tlv->type_ext;
  switch (_message_order) {
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      return _parse_msg_interface_discovery(&radio_mac);
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      return _parse_msg_neighbor_update(&radio_mac);
    default:
      OLSR_WARN(LOG_DLEP_CLIENT, "Unknown order in DLEP message: %d", _message_order);
      return PBB_DROP_MESSAGE;
  }
  return PBB_OKAY;
}

/**
 * Parse TLVs of one address for incoming DLEP Neighbor Updates
 * @param radio_mac originator MAC of message
 * @param neigh_mac address of TLVs
 * @return PBB_OKAY if message was okay, PBB_DROP_MESSAGE otherwise
 */
static enum pbb_result
_parse_addr_neighbor_update(struct netaddr *radio_mac, struct netaddr *neigh_mac) {
  struct olsr_layer2_neighbor *neigh;
  struct netaddr_str buf1, buf2;
  char link_status;

  if (_dlep_address_tlvs[IDX_ADDRTLV_LINK_STATUS].tlv == NULL) {
    /* ignore */
    return PBB_OKAY;
  }

  OLSR_DEBUG(LOG_DLEP_CLIENT, "Got layer2 neighbor %s (seen by %s)",
      netaddr_to_string(&buf1, neigh_mac), netaddr_to_string(&buf2, radio_mac));

  memcpy(&link_status,
      _dlep_address_tlvs[IDX_ADDRTLV_LINK_STATUS].tlv->single_value,
      sizeof(link_status));

  if (link_status == PBB_LINKSTATUS_LOST) {
    /* remove entry from database */
    neigh = olsr_layer2_get_neighbor(radio_mac, neigh_mac);
    if (neigh != NULL && neigh->active) {
      olsr_layer2_remove_neighbor(neigh);
    }
    return PBB_OKAY;
  }

  neigh = olsr_layer2_add_neighbor(radio_mac, neigh_mac, 0, _message_vtime);
  if (neigh == NULL) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Cannot allocate new layer2 neighbor");
    return PBB_DROP_MESSAGE;
  }

  olsr_layer2_neighbor_clear(neigh);

  if (_dlep_address_tlvs[IDX_ADDRTLV_SIGNAL].tlv) {
    uint16_t sig_encoded;

    memcpy(&sig_encoded,
        _dlep_address_tlvs[IDX_ADDRTLV_SIGNAL].tlv->single_value,
        sizeof(sig_encoded));

    olsr_layer2_neighbor_set_signal(neigh, (int16_t)ntohs(sig_encoded));
  }
  if (_dlep_address_tlvs[IDX_ADDRTLV_LAST_SEEN].tlv) {
    int32_t last_seen;

    memcpy(&last_seen,
        _dlep_address_tlvs[IDX_ADDRTLV_LAST_SEEN].tlv->single_value,
        sizeof(last_seen));

    last_seen = ntohl(last_seen);
    olsr_layer2_neighbor_set_last_seen(neigh, last_seen);
  }
  if (_dlep_address_tlvs[IDX_ADDRTLV_RX_BITRATE].tlv) {
    uint64_t rate;

    memcpy(&rate, _dlep_address_tlvs[IDX_ADDRTLV_RX_BITRATE].tlv->single_value, sizeof(rate));

    rate = be64toh(rate);
    olsr_layer2_neighbor_set_rx_bitrate(neigh, rate);
  }
  if (_dlep_address_tlvs[IDX_ADDRTLV_RX_BYTES].tlv) {
    uint32_t bytes;

    memcpy(&bytes,
        _dlep_address_tlvs[IDX_ADDRTLV_RX_BYTES].tlv->single_value,
        sizeof(bytes));

    bytes = ntohl(bytes);
    olsr_layer2_neighbor_set_rx_bytes(neigh, bytes);
  }
  if (_dlep_address_tlvs[IDX_ADDRTLV_RX_PACKETS].tlv) {
    uint32_t packets;

    memcpy(&packets,
        _dlep_address_tlvs[IDX_ADDRTLV_RX_PACKETS].tlv->single_value,
        sizeof(packets));

    packets = ntohl(packets);
    olsr_layer2_neighbor_set_rx_packets(neigh, packets);
  }
  if (_dlep_address_tlvs[IDX_ADDRTLV_TX_BITRATE].tlv) {
    uint64_t rate;

    memcpy(&rate, _dlep_address_tlvs[IDX_ADDRTLV_TX_BITRATE].tlv->single_value, sizeof(rate));

    rate = be64toh(rate);
    olsr_layer2_neighbor_set_tx_bitrate(neigh, rate);
  }
  if (_dlep_address_tlvs[IDX_ADDRTLV_TX_BYTES].tlv) {
    uint32_t bytes;

    memcpy(&bytes,
        _dlep_address_tlvs[IDX_ADDRTLV_TX_BYTES].tlv->single_value,
        sizeof(bytes));

    bytes = ntohl(bytes);
    olsr_layer2_neighbor_set_tx_bytes(neigh, bytes);
  }
  if (_dlep_address_tlvs[IDX_ADDRTLV_TX_PACKETS].tlv) {
    uint32_t packets;

    memcpy(&packets,
        _dlep_address_tlvs[IDX_ADDRTLV_TX_PACKETS].tlv->single_value,
        sizeof(packets));

    packets = ntohl(packets);
    olsr_layer2_neighbor_set_tx_packets(neigh, packets);
  }
  if (_dlep_address_tlvs[IDX_ADDRTLV_TX_RETRIES].tlv) {
    uint32_t retries;

    memcpy(&retries,
        _dlep_address_tlvs[IDX_ADDRTLV_TX_RETRIES].tlv->single_value,
        sizeof(retries));

    retries = ntohl(retries);
    olsr_layer2_neighbor_set_tx_retries(neigh, retries);
  }
  if (_dlep_address_tlvs[IDX_ADDRTLV_TX_FAILED].tlv) {
    uint32_t failed;

    memcpy(&failed,
        _dlep_address_tlvs[IDX_ADDRTLV_TX_FAILED].tlv->single_value,
        sizeof(failed));

    failed = ntohl(failed);
    olsr_layer2_neighbor_set_tx_fails(neigh, failed);
  }

  return PBB_OKAY;
}

/**
 * Callback for parsing addresses of incoming DLEP messages
 * @param consumer
 * @param context
 * @return
 */
static enum pbb_result
_cb_parse_dlep_addresses(struct pbb_reader_tlvblock_consumer *consumer __attribute__ ((unused)),
    struct pbb_reader_tlvblock_context *context) {
  struct netaddr radio_mac;
  struct netaddr neigh_mac;

  OLSR_DEBUG(LOG_DLEP_CLIENT, "Parse DLEP addresses");

  if (context->addr_len != 6) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Address length of DLEP message should be 6 (but was %d)",
        context->addr_len);
    return PBB_DROP_MESSAGE;
  }

  if (netaddr_from_binary(&radio_mac, context->orig_addr, context->addr_len, AF_MAC48)) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Cannot parse DLEP originator address");
    return PBB_DROP_MESSAGE;
  }

  if (netaddr_from_binary(&neigh_mac, context->addr, context->addr_len, AF_MAC48)) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Cannot parse DLEP neighbor address");
    return PBB_DROP_MESSAGE;
  }

  /* current order has already been read by message parsing */
  switch (_message_order) {
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      /* not interested in addresses */
      break;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      return _parse_addr_neighbor_update(&radio_mac, &neigh_mac);
      break;
    default:
      OLSR_WARN(LOG_DLEP_CLIENT, "Unknown order in DLEP message: %d", _message_order);
      return PBB_DROP_MESSAGE;
  }
  return PBB_OKAY;

}

/**
 * Debug callback for message TLVs that failed the set constraints
 * TODO: Remove before shipping?
 * @param consumer
 * @param context
 * @return
 */
static enum pbb_result
_cb_parse_dlep_message_failed(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  size_t i;
  OLSR_WARN(LOG_DLEP_CLIENT, "Constraints of incoming DLEP message were not fulfilled!");

  for (i=0; i < ARRAYSIZE(_dlep_message_tlvs); i++) {
    OLSR_WARN(LOG_DLEP_CLIENT, "block %zu: %s", i, _dlep_message_tlvs[i].tlv == NULL ? "no" : "yes");
    if (_dlep_message_tlvs[i].tlv) {
      OLSR_WARN_NH(LOG_DLEP_CLIENT, "\tvalue length: %u", _dlep_message_tlvs[i].tlv->length);
    }
  }
  return PBB_DROP_MESSAGE;
}

/**
 * Debug callback for address TLVs that failed the set constraints
 * TODO: Remove before shipping?
 * @param consumer
 * @param context
 * @return
 */
static enum pbb_result
_cb_parse_dlep_addresses_failed(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
    struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  size_t i;
  OLSR_WARN(LOG_DLEP_CLIENT, "Constraints of incoming DLEP address were not fulfilled!");

  for (i=0; i < ARRAYSIZE(_dlep_address_tlvs); i++) {
    OLSR_WARN(LOG_DLEP_CLIENT, "block %zu: %s", i, _dlep_address_tlvs[i].tlv == NULL ? "no" : "yes");
    if (_dlep_address_tlvs[i].tlv) {
      OLSR_WARN_NH(LOG_DLEP_CLIENT, "\tvalue length: %u", _dlep_address_tlvs[i].tlv->length);
    }
  }
  return PBB_DROP_ADDRESS;
}
