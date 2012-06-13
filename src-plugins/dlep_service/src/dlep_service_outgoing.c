
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
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_timer.h"
#include "core/olsr_subsystem.h"

#include "dlep_iana.h"
#include "dlep_service.h"
#include "dlep_service_outgoing.h"

#include "olsr_layer2.h"

/* index numbers of address TVLs */
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

/* prototypes of callbacks */
static void _cb_addMessageHeader(struct rfc5444_writer *,
    struct rfc5444_writer_message *);
static void _cb_addMessageTLVs(struct rfc5444_writer *,
    struct rfc5444_writer_content_provider *);
static void _cb_addAddresses(struct rfc5444_writer *,
    struct rfc5444_writer_content_provider *);

static void _cb_interface_discovery(void *);
static void _cb_metric_update(void *);

/* discovery and metric update timer data */
struct olsr_timer_info _tinfo_interface_discovery = {
  .name = "dlep interface discovery",
  .callback = _cb_interface_discovery,
  .periodic = true,
};
struct olsr_timer_entry _tentry_interface_discovery = {
  .info = &_tinfo_interface_discovery,
};

struct olsr_timer_info _tinfo_metric_update = {
  .name = "dlep metric update",
  .callback = _cb_metric_update,
};
struct olsr_timer_entry _tentry_metric_update = {
  .info = &_tinfo_metric_update,
};

bool _triggered_metric_update;

/* DLEP writer data */
static uint8_t _msg_buffer[1200];
static uint8_t _msg_addrtlvs[5000];

static enum dlep_orders _msg_order;
static struct olsr_layer2_network *_msg_network;

static struct rfc5444_writer _dlep_writer = {
  .msg_buffer = _msg_buffer,
  .msg_size = sizeof(_msg_buffer),
  .addrtlv_buffer = _msg_addrtlvs,
  .addrtlv_size = sizeof(_msg_addrtlvs),
};

static struct rfc5444_writer_message *_dlep_message = NULL;

static struct rfc5444_writer_content_provider _dlep_msgcontent_provider = {
  .msg_type = DLEP_MESSAGE_ID,
  .addMessageTLVs = _cb_addMessageTLVs,
  .addAddresses = _cb_addAddresses,
};

static struct rfc5444_writer_addrtlv_block _dlep_addrtlvs[] = {
  [IDX_ADDRTLV_LINK_STATUS] = { .type = RFC5444_ADDRTLV_LINK_STATUS },
  [IDX_ADDRTLV_SIGNAL]      = { .type = DLEP_ADDRTLV_SIGNAL },
  [IDX_ADDRTLV_LAST_SEEN]   = { .type = DLEP_ADDRTLV_LAST_SEEN },
  [IDX_ADDRTLV_RX_BITRATE]  = { .type = DLEP_ADDRTLV_RX_BITRATE },
  [IDX_ADDRTLV_RX_BYTES]    = { .type = DLEP_ADDRTLV_RX_BYTES },
  [IDX_ADDRTLV_RX_PACKETS]  = { .type = DLEP_ADDRTLV_RX_PACKETS },
  [IDX_ADDRTLV_TX_BITRATE]  = { .type = DLEP_ADDRTLV_TX_BITRATE },
  [IDX_ADDRTLV_TX_BYTES]    = { .type = DLEP_ADDRTLV_TX_BYTES },
  [IDX_ADDRTLV_TX_PACKETS]  = { .type = DLEP_ADDRTLV_TX_PACKETS },
  [IDX_ADDRTLV_TX_RETRIES]  = { .type = DLEP_ADDRTLV_TX_RETRIES },
  [IDX_ADDRTLV_TX_FAILED]   = { .type = DLEP_ADDRTLV_TX_FAILED },
};

static uint8_t _packet_buffer[1500];
static struct rfc5444_writer_interface _dlep_multicast = {
  .packet_buffer = _packet_buffer,
  .packet_size = sizeof(_packet_buffer),
  .sendPacket =_cb_send_multicast,
};

/* outgoing subsystem */
OLSR_SUBSYSTEM_STATE(_dlep_service_outgoing);

/**
 * Initialize subsystem for rfc5444 generation
 * @return -1 if an error happened, 0 otherwise
 */
int
dlep_outgoing_init(void) {
  if (olsr_subsystem_init(&_dlep_service_outgoing))
    return 0;

  rfc5444_writer_init(&_dlep_writer);

  _dlep_message = rfc5444_writer_register_message(&_dlep_writer, DLEP_MESSAGE_ID, true, 6);
  if (_dlep_message == NULL) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Could not register DLEP message");
    rfc5444_writer_cleanup(&_dlep_writer);
    return -1;
  }
  _dlep_message->addMessageHeader = _cb_addMessageHeader;

  if (rfc5444_writer_register_msgcontentprovider(&_dlep_writer,
      &_dlep_msgcontent_provider, _dlep_addrtlvs, ARRAYSIZE(_dlep_addrtlvs))) {
    OLSR_WARN(LOG_DLEP_SERVICE, "Count not register DLEP msg contentprovider");
    rfc5444_writer_unregister_message(&_dlep_writer, _dlep_message);
    rfc5444_writer_cleanup(&_dlep_writer);
    return -1;
  }

  rfc5444_writer_register_interface(&_dlep_writer, &_dlep_multicast);

  olsr_timer_add(&_tinfo_interface_discovery);
  olsr_timer_add(&_tinfo_metric_update);

  return 0;
}

/**
 * Cleanup all allocated data for rfc 5444 generation
 */
void
dlep_outgoing_cleanup(void) {
  if (olsr_subsystem_cleanup(&_dlep_service_outgoing))
    return;

  olsr_timer_remove(&_tinfo_interface_discovery);
  olsr_timer_remove(&_tinfo_metric_update);

  rfc5444_writer_unregister_content_provider(&_dlep_writer, &_dlep_msgcontent_provider,
      _dlep_addrtlvs, ARRAYSIZE(_dlep_addrtlvs));
  rfc5444_writer_unregister_message(&_dlep_writer, _dlep_message);
  rfc5444_writer_cleanup(&_dlep_writer);
}

/**
 * Add a packetbb interface to the writer instance
 * @param pbbif pointer to packetbb interface
 */
void
dlep_service_registerif(struct rfc5444_writer_interface *pbbif) {
  rfc5444_writer_register_interface(&_dlep_writer, pbbif);
}

/**
 * Remove a packetbb interface to the writer instance
 * @param pbbif pointer to packetbb interface
 */
void
dlep_service_unregisterif(struct rfc5444_writer_interface *pbbif) {
  rfc5444_writer_unregister_interface(&_dlep_writer, pbbif);
}

/**
 * Trigger an 'out of order' metric update
 */
void
dlep_trigger_metric_update(void) {
  if (!_triggered_metric_update) {
    _triggered_metric_update = true;
    olsr_timer_start(&_tentry_metric_update, 1);
  }
}

/**
 * Reset timer settings according to configuration
 */
void
dlep_reconfigure_timers(void) {
  olsr_timer_set(&_tentry_interface_discovery, _config.discovery_interval);
  olsr_timer_set(&_tentry_metric_update, _config.metric_interval);
}

/**
 * Add Message-TLVs for DLEP IF-Discovery message
 */
static void
_add_ifdiscovery_msgtlvs(void) {
  uint8_t encoded_vtime;

  /* encode vtime according to RFC 5497 */
  encoded_vtime = rfc5444_timetlv_encode(_config.discovery_validity);

  rfc5444_writer_add_messagetlv(&_dlep_writer,
      RFC5444_MSGTLV_VALIDITY_TIME, 0, &encoded_vtime, sizeof(encoded_vtime));

  if (_config.peer_type[0]) {
    rfc5444_writer_add_messagetlv(&_dlep_writer, DLEP_TLV_PEER_TYPE, 0,
        _config.peer_type, strlen(_config.peer_type));
  }
}

/**
 * Add Message-TLVs for DLEP Neighbor-Update message
 */
static void
_add_neighborupdate_msgtlvs(void) {
  uint8_t encoded_vtime;

  /* encode vtime according to RFC 5497 */
  encoded_vtime = rfc5444_timetlv_encode(_config.metric_validity);

  rfc5444_writer_add_messagetlv(&_dlep_writer,
      RFC5444_MSGTLV_VALIDITY_TIME, 0, &encoded_vtime, sizeof(encoded_vtime));

  if (olsr_layer2_network_has_ssid(_msg_network)) {
    rfc5444_writer_add_messagetlv(&_dlep_writer,
        DLEP_TLV_SSID, 0,
        _msg_network->ssid, strlen(_msg_network->ssid));
  }
  if (olsr_layer2_network_has_last_seen(_msg_network)) {
    uint32_t interval;
    int64_t t;

    t = -olsr_clock_get_relative(_msg_network->last_seen);
    if (t < 0) {
      interval = 0;
    }
    else if (t > UINT32_MAX) {
      interval = UINT32_MAX;
    }
    else {
      interval = (uint32_t)t;
    }

    interval = htonl(interval);
    rfc5444_writer_add_messagetlv(&_dlep_writer,
        DLEP_TLV_LAST_SEEN, 0, &interval, sizeof(interval));
  }
  if (olsr_layer2_network_has_frequency(_msg_network)) {
    uint64_t freq;

    freq = htobe64(_msg_network->frequency);
    rfc5444_writer_add_messagetlv(&_dlep_writer,
        DLEP_TLV_FREQUENCY, 0, &freq, sizeof(freq));
  }

  // TODO: add supported data rates
}

/**
 * Initialize message header for DLEP messages
 * @param writer
 * @param msg
 */
static void
_cb_addMessageHeader(struct rfc5444_writer *writer, struct rfc5444_writer_message *msg) {
  static uint16_t seqno = 0;
  rfc5444_writer_set_msg_header(writer, msg, true, false, false, true);
  rfc5444_writer_set_msg_originator(writer, msg, netaddr_get_binptr(&_msg_network->radio_id));
  rfc5444_writer_set_msg_seqno(writer, msg, seqno++);
}

static void
_cb_addMessageTLVs(struct rfc5444_writer *writer,
    struct rfc5444_writer_content_provider *prv __attribute__((unused))) {

  rfc5444_writer_add_messagetlv(writer,
      DLEP_TLV_ORDER, _msg_order, NULL, 0);

  switch (_msg_order) {
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      _add_ifdiscovery_msgtlvs();
      break;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      _add_neighborupdate_msgtlvs();
      break;
    default:
      OLSR_WARN(LOG_DLEP_SERVICE, "DLEP Message order %d not implemented yet", _msg_order);
      break;
  }
}

/**
 * Add addresses for DLEP Neighbor-Update message
 */
static void
_add_neighborupdate_addresses(void) {
  struct olsr_layer2_neighbor *neigh, *neigh_it;
  struct rfc5444_writer_address *addr;
  struct netaddr_str buf1, buf2;
  char link_status;

  OLSR_FOR_ALL_LAYER2_NEIGHBORS(neigh, neigh_it) {
    if (netaddr_cmp(&_msg_network->radio_id, &neigh->key.radio_mac) != 0) {
      continue;
    }

    addr = rfc5444_writer_add_address(&_dlep_writer, _dlep_message,
          netaddr_get_binptr(&neigh->key.neighbor_mac), 48);
    if (addr == NULL) {
      OLSR_WARN(LOG_DLEP_SERVICE, "Could not allocate address for neighbor update");
      break;
    }

    /* LINK_HEARD */
    link_status = neigh->active ? RFC5444_LINKSTATUS_HEARD : RFC5444_LINKSTATUS_LOST;

    rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
        _dlep_addrtlvs[IDX_ADDRTLV_LINK_STATUS]._tlvtype,
        &link_status, sizeof(link_status), false);

    OLSR_DEBUG(LOG_DLEP_SERVICE, "Added neighbor %s (seen by %s) to neigh-up",
        netaddr_to_string(&buf1, &neigh->key.neighbor_mac),
        netaddr_to_string(&buf2, &neigh->key.radio_mac));

    if (!neigh->active)
      continue;

    if (olsr_layer2_neighbor_has_signal(neigh)) {
      uint16_t sig_encoded;

      sig_encoded = htons(neigh->signal_dbm);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
                _dlep_addrtlvs[IDX_ADDRTLV_SIGNAL]._tlvtype,
                &sig_encoded, sizeof(sig_encoded), false);
    }
    if (olsr_layer2_neighbor_has_last_seen(neigh)) {
      uint32_t interval;
      int64_t t;

      t = -olsr_clock_get_relative(_msg_network->last_seen);
      if (t < 0) {
        interval = 0;
      }
      else if (t > UINT32_MAX) {
        interval = UINT32_MAX;
      }
      else {
        interval = (uint32_t)t;
      }

      interval = htonl(interval);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
                _dlep_addrtlvs[IDX_ADDRTLV_LAST_SEEN]._tlvtype,
                &interval, sizeof(interval), false);
    }
    if (olsr_layer2_neighbor_has_rx_bitrate(neigh)) {
      uint64_t rate = htobe64(neigh->rx_bitrate);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
          _dlep_addrtlvs[IDX_ADDRTLV_RX_BITRATE]._tlvtype, &rate, sizeof(rate), false);
    }
    if (olsr_layer2_neighbor_has_rx_bytes(neigh)) {
      uint32_t bytes = htonl(neigh->rx_bytes);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
          _dlep_addrtlvs[IDX_ADDRTLV_RX_BYTES]._tlvtype, &bytes, sizeof(bytes), false);
    }
    if (olsr_layer2_neighbor_has_rx_packets(neigh)) {
      uint32_t packets = htonl(neigh->rx_packets);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
          _dlep_addrtlvs[IDX_ADDRTLV_RX_PACKETS]._tlvtype, &packets, sizeof(packets), false);
    }
    if (olsr_layer2_neighbor_has_tx_bitrate(neigh)) {
      uint64_t rate = htobe64(neigh->tx_bitrate);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
          _dlep_addrtlvs[IDX_ADDRTLV_TX_BITRATE]._tlvtype, &rate, sizeof(rate), false);

    }
    if (olsr_layer2_neighbor_has_tx_bytes(neigh)) {
      uint32_t bytes = htonl(neigh->tx_bytes);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
          _dlep_addrtlvs[IDX_ADDRTLV_TX_BYTES]._tlvtype, &bytes, sizeof(bytes), false);
    }
    if (olsr_layer2_neighbor_has_tx_packets(neigh)) {
      uint32_t packets = htonl(neigh->tx_packets);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
          _dlep_addrtlvs[IDX_ADDRTLV_TX_PACKETS]._tlvtype, &packets, sizeof(packets), false);
    }
    if (olsr_layer2_neighbor_has_tx_retries(neigh)) {
      uint32_t packets = htonl(neigh->tx_retries);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
          _dlep_addrtlvs[IDX_ADDRTLV_TX_RETRIES]._tlvtype, &packets, sizeof(packets), false);
    }
    if (olsr_layer2_neighbor_has_tx_failed(neigh)) {
      uint32_t packets = htonl(neigh->tx_failed);
      rfc5444_writer_add_addrtlv(&_dlep_writer, addr,
          _dlep_addrtlvs[IDX_ADDRTLV_TX_FAILED]._tlvtype, &packets, sizeof(packets), false);
    }
  }
}

/**
 * Callback for adding addresses to DLEP messages
 * @param writer
 * @param cpr
 */
static void
_cb_addAddresses(struct rfc5444_writer *writer __attribute__((unused)),
    struct rfc5444_writer_content_provider *cpr __attribute__((unused))) {
  switch (_msg_order) {
    case DLEP_ORDER_INTERFACE_DISCOVERY:
      /* no addresses in interface discovery */
      break;
    case DLEP_ORDER_NEIGHBOR_UPDATE:
      _add_neighborupdate_addresses();
      break;
    default:
      OLSR_WARN(LOG_DLEP_SERVICE, "DLEP Message order %d not implemented yet", _msg_order);
      break;
  }
}

/**
 * Callback for periodic generation of Interface Discovery messages
 * @param ptr
 */
static void
_cb_interface_discovery(void *ptr __attribute__((unused))) {
  struct olsr_layer2_network *net_it;
  struct _router_session *session;
  struct netaddr_str buf;

  _msg_order = DLEP_ORDER_INTERFACE_DISCOVERY;
  OLSR_FOR_ALL_LAYER2_ACTIVE_NETWORKS(_msg_network, net_it) {
    OLSR_DEBUG(LOG_DLEP_SERVICE, "Send interface discovery for radio %s",
        netaddr_to_string(&buf, &_msg_network->radio_id));

    rfc5444_writer_create_message_singleif(&_dlep_writer, DLEP_MESSAGE_ID, &_dlep_multicast);
    rfc5444_writer_flush(&_dlep_writer, &_dlep_multicast, false);

    avl_for_each_element(&_router_tree, session, _node) {
      if (session->unicast) {
        rfc5444_writer_create_message_singleif(&_dlep_writer, DLEP_MESSAGE_ID, &session->out_if);
        rfc5444_writer_flush(&_dlep_writer, &session->out_if, false);
      }
    }
  }
}

/**
 * Callback for periodic generation of Neighbor Update messages
 * @param ptr
 */
static void
_cb_metric_update(void *ptr __attribute__((unused))) {
  struct olsr_layer2_network *net_it;
  struct _router_session *session;
  struct netaddr_str buf1, buf2;
  bool multicast;

  _triggered_metric_update = false;
  _msg_order = DLEP_ORDER_NEIGHBOR_UPDATE;

  multicast = _config.always_send;
  avl_for_each_element(&_router_tree, session, _node) {
    if (!session->unicast) {
      multicast = true;
      continue;
    }

    OLSR_FOR_ALL_LAYER2_ACTIVE_NETWORKS(_msg_network, net_it) {
      OLSR_DEBUG(LOG_DLEP_SERVICE, "Send metric updates for radio %s to router %s",
          netaddr_to_string(&buf1, &_msg_network->radio_id),
          netaddr_socket_to_string(&buf2, &session->router_socket));

      rfc5444_writer_create_message_singleif(&_dlep_writer, DLEP_MESSAGE_ID, &session->out_if);
      rfc5444_writer_flush(&_dlep_writer, &session->out_if, false);
    }
  }

  if (multicast) {
    OLSR_FOR_ALL_LAYER2_ACTIVE_NETWORKS(_msg_network, net_it) {
      OLSR_DEBUG(LOG_DLEP_SERVICE, "Send metric updates for radio %s (via multicast)",
          netaddr_to_string(&buf1, &_msg_network->radio_id));

      rfc5444_writer_create_message_singleif(&_dlep_writer, DLEP_MESSAGE_ID, &_dlep_multicast);
      rfc5444_writer_flush(&_dlep_writer, &_dlep_multicast, false);
    }
  }

  olsr_timer_start(&_tentry_metric_update, _config.metric_interval);
}
