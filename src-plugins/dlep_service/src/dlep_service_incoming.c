
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
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444_conversion.h"
#include "rfc5444/rfc5444_reader.h"
#include "core/olsr.h"

#include "dlep_iana.h"
#include "dlep_service.h"
#include "dlep_service_incoming.h"

/* DLEP TLV array index */
enum dlep_tlv_idx {
  IDX_TLV_ORDER,
  IDX_TLV_VTIME,
  IDX_TLV_PEER_TYPE,
  IDX_TLV_UNICAST,
  IDX_TLV_BSSID,
  IDX_TLV_LAST_SEEN,
  IDX_TLV_FREQUENCY,
  IDX_TLV_SUPPORTED_RATES,
};

/* callback prototypes */
static enum rfc5444_result _cb_parse_dlep_message(
    struct rfc5444_reader_tlvblock_consumer *consumer,
    struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result _cb_parse_dlep_message_failed(
    struct rfc5444_reader_tlvblock_consumer *consumer,
    struct rfc5444_reader_tlvblock_context *context);

/* DLEP reader data */
static struct rfc5444_reader _dlep_reader;

static struct rfc5444_reader_tlvblock_consumer _dlep_message_consumer = {
  .block_callback = _cb_parse_dlep_message,
  .block_callback_failed_constraints = _cb_parse_dlep_message_failed,
};

static struct rfc5444_reader_tlvblock_consumer_entry _dlep_message_tlvs[] = {
  [IDX_TLV_ORDER]           = { .type = DLEP_TLV_ORDER, .mandatory = true, .min_length = 0, .match_length = true },
  [IDX_TLV_VTIME]           = { .type = RFC5444_MSGTLV_VALIDITY_TIME, .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_PEER_TYPE]       = { .type = DLEP_TLV_PEER_TYPE, .min_length = 0, .max_length = 80, .match_length = true },
  [IDX_TLV_UNICAST]         = { .type = DLEP_TLV_UNICAST, .min_length = 0, .match_length = true},
  [IDX_TLV_BSSID]           = { .type = DLEP_TLV_SSID, .min_length = 6, .match_length = true },
  [IDX_TLV_LAST_SEEN]       = { .type = DLEP_TLV_LAST_SEEN, .min_length = 4, .match_length = true },
  [IDX_TLV_FREQUENCY]       = { .type = DLEP_TLV_FREQUENCY, .min_length = 8, .match_length = true },
  [IDX_TLV_SUPPORTED_RATES] = { .type = DLEP_TLV_SUPPORTED_RATES },
};

/* temporary variables for parsing DLEP messages */
static enum dlep_orders _current_order;
static union netaddr_socket *_peer_socket;

/* incoming subsystem */
OLSR_SUBSYSTEM_STATE(_dlep_service_incoming);

/**
 * Initialize subsystem for RFC5444 processing
 */
void
dlep_service_incoming_init(void) {
  if (olsr_subsystem_init(&_dlep_service_incoming))
    return;

  rfc5444_reader_init(&_dlep_reader);
  rfc5444_reader_add_message_consumer(&_dlep_reader, &_dlep_message_consumer,
      _dlep_message_tlvs, ARRAYSIZE(_dlep_message_tlvs), DLEP_MESSAGE_ID, 0);
}

/**
 * Cleanup all data allocated for RFC 5444 processing
 */
void
dlep_service_incoming_cleanup(void) {
  if (olsr_subsystem_cleanup(&_dlep_service_incoming))
    return;

  rfc5444_reader_remove_message_consumer(&_dlep_reader, &_dlep_message_consumer);
  rfc5444_reader_cleanup(&_dlep_reader);
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
  enum rfc5444_result result;
#if !defined(REMOVE_LOG_DEBUG)
  struct netaddr_str buf;
#endif

  OLSR_DEBUG(LOG_DLEP_SERVICE, "Parsing DLEP packet from %s (%s)",
      netaddr_socket_to_string(&buf, from),
      multicast ? "multicast" : "unicast");

  _peer_socket = from;

  result = rfc5444_reader_handle_packet(&_dlep_reader, ptr, length);
  if (result) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Error while parsing DLEP packet: %s (%d)",
        rfc5444_strerror(result), result);
  }

  _peer_socket = NULL;
}

/**
 * parse message TLVs of "connect router" message and add it to
 * session database
 * @return RFC5444_OKAY if message was okay, RFC5444_DROP_MESSAGE otherwise
 */
static enum rfc5444_result
_parse_order_connect_router(void) {
  struct _router_session *session;
  uint8_t encoded_vtime;
  uint64_t vtime;
  bool unicast;
  encoded_vtime = _dlep_message_tlvs[IDX_TLV_VTIME].tlv->single_value[0];

  /* decode vtime according to RFC 5497 */
  vtime = rfc5444_timetlv_decode(encoded_vtime);

  /* see if we have to unicast this router */
  unicast = _dlep_message_tlvs[IDX_TLV_UNICAST].tlv != NULL;

  /* add new session */
  session = dlep_add_router_session(_peer_socket, unicast, vtime);
  if (!session) {
    return RFC5444_DROP_MESSAGE;
  }

  return RFC5444_OKAY;
}


/**
 * Callback for parsing the message TLVs incoming over the DLEP port
 * (see packetbb reader API)
 * @param consumer
 * @param context
 * @return
 */
static enum rfc5444_result
_cb_parse_dlep_message(struct rfc5444_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct rfc5444_reader_tlvblock_context *context) {
  if (context->addr_len != 6) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Address length of DLEP message should be 6 (but was %d)",
        context->addr_len);
    return RFC5444_DROP_MESSAGE;
  }

  _current_order = _dlep_message_tlvs[IDX_TLV_ORDER].tlv->type_ext;
  switch (_current_order) {
    case DLEP_ORDER_CONNECT_ROUTER:
      return _parse_order_connect_router();
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      /* ignore our own discovery packets if we work with multicast loop */
      return RFC5444_OKAY;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      /* ignore our own discovery packets if we work with multicast loop */
      break;
    default:
      OLSR_WARN(LOG_DLEP_SERVICE, "Unknown order in DLEP message: %d", _current_order);
      return RFC5444_DROP_MESSAGE;
  }
  return RFC5444_OKAY;
}

/**
 * Debugging callback for incoming messages that don't fulfill the contraints.
 * TODO: Remove before shipping?
 * @param consumer
 * @param context
 * @return
 */
static enum rfc5444_result
_cb_parse_dlep_message_failed(struct rfc5444_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  size_t i;
  OLSR_WARN(LOG_DLEP_SERVICE, "Constraints of incoming DLEP message were not fulfilled!");

  for (i=0; i < ARRAYSIZE(_dlep_message_tlvs); i++) {
    OLSR_WARN(LOG_DLEP_SERVICE, "block %zu: %s", i, _dlep_message_tlvs[i].tlv == NULL ? "no" : "yes");
    if (_dlep_message_tlvs[i].tlv) {
      OLSR_WARN_NH(LOG_DLEP_SERVICE, "\tvalue length: %u", _dlep_message_tlvs[i].tlv->length);
    }
  }
  return RFC5444_OKAY;
}
