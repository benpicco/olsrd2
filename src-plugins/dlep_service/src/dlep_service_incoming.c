/*
 * dlep_incoming.c
 *
 *  Created on: May 3, 2012
 *      Author: rogge
 */

#include "common/common_types.h"
#include "packetbb/pbb_iana.h"
#include "packetbb/pbb_conversion.h"
#include "packetbb/pbb_reader.h"
#include "core/olsr.h"

#include "dlep_iana.h"
#include "dlep_service.h"
#include "dlep_service_incoming.h"

/* DLEP TLV array index */
enum dlep_tlv_idx {
  IDX_TLV_ORDER,
  IDX_TLV_VTIME,
  IDX_TLV_PEER_TYPE,
  IDX_TLV_SSID,
  IDX_TLV_LAST_SEEN,
  IDX_TLV_FREQUENCY,
  IDX_TLV_SUPPORTED_RATES,
};

/* callback prototypes */
static enum pbb_result _cb_parse_dlep_message(
    struct pbb_reader_tlvblock_consumer *consumer,
    struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_parse_dlep_message_failed(
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

/* temporary variables for parsing DLEP messages */
static enum dlep_orders _current_order;
static union netaddr_socket *_peer_socket;

/* incoming subsystem */
OLSR_SUBSYSTEM_STATE(_dlep_service_incoming);

/**
 * Initialize subsystem for RFC5444 processing
 */
void
dlep_incoming_init(void) {
  if (olsr_subsystem_init(&_dlep_service_incoming))
    return;

  pbb_reader_init(&_dlep_reader);
  pbb_reader_add_message_consumer(&_dlep_reader, &_dlep_message_consumer,
      _dlep_message_tlvs, ARRAYSIZE(_dlep_message_tlvs), DLEP_MESSAGE_ID, 0);
}

/**
 * Cleanup all data allocated for RFC 5444 processing
 */
void
dlep_incoming_cleanup(void) {
  if (olsr_subsystem_cleanup(&_dlep_service_incoming))
    return;

  pbb_reader_remove_message_consumer(&_dlep_reader, &_dlep_message_consumer);
  pbb_reader_cleanup(&_dlep_reader);
}

/**
 * Receive UDP data with DLEP protocol
 * (see olsr socket packet)
 * @param
 * @param from
 * @param length
 */
void
cb_receive_dlep(struct olsr_packet_socket *s __attribute__((unused)),
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

/**
 * parse message TLVs of "connect router" message and add it to
 * session database
 * @return PBB_OKAY if message was okay, PBB_DROP_MESSAGE otherwise
 */
static enum pbb_result
_parse_order_connect_router(void) {
  uint8_t encoded_vtime;
  uint64_t vtime;

  encoded_vtime = _dlep_message_tlvs[IDX_TLV_VTIME].tlv->single_value[0];

  /* decode vtime according to RFC 5497 */
  vtime = pbb_timetlv_decode(encoded_vtime);

  /* add new session */
  if (!dlep_add_router_session(_peer_socket, vtime)) {
    return PBB_DROP_MESSAGE;
  }
  return PBB_OKAY;
}


/**
 * Callback for parsing the message TLVs incoming over the DLEP port
 * (see packetbb reader API)
 * @param consumer
 * @param context
 * @return
 */
static enum pbb_result
_cb_parse_dlep_message(struct pbb_reader_tlvblock_consumer *consumer  __attribute__ ((unused)),
      struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  if (context->addr_len != 6) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Address length of DLEP message should be 6 (but was %d)",
        context->addr_len);
    return PBB_DROP_MESSAGE;
  }

  _current_order = _dlep_message_tlvs[IDX_TLV_ORDER].tlv->type_ext;
  switch (_current_order) {
    case DLEP_ORDER_CONNECT_ROUTER:
      return _parse_order_connect_router();
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      /* ignore our own discovery packets if we work with multicast loop */
      return PBB_OKAY;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      /* ignore our own discovery packets if we work with multicast loop */
      break;
    default:
      OLSR_WARN(LOG_DLEP_SERVICE, "Unknown order in DLEP message: %d", _current_order);
      return PBB_DROP_MESSAGE;
  }
  return PBB_OKAY;
}

/**
 * Debugging callback for incoming messages that don't fulfill the contraints.
 * TODO: Remove before shipping?
 * @param consumer
 * @param context
 * @return
 */
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
