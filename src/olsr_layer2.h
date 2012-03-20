
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

#ifndef OLSR_LAYER2_H_
#define OLSR_LAYER2_H_

#include "common/avl.h"
#include "common/common_types.h"
#include "common/netaddr.h"

#include "olsr_clock.h"

enum olsr_layer2_neighbor_data {
  OLSR_L2NEIGH_SIGNAL = 1<<0,
  OLSR_L2NEIGH_LAST_SEEN = 1<<1,
  OLSR_L2NEIGH_RX_BITRATE = 1<<2,
  OLSR_L2NEIGH_RX_BYTES = 1<<3,
  OLSR_L2NEIGH_RX_PACKETS = 1<<4,
  OLSR_L2NEIGH_TX_BITRATE = 1<<5,
  OLSR_L2NEIGH_TX_BYTES = 1<<6,
  OLSR_L2NEIGH_TX_PACKETS = 1<<7,
  OLSR_L2NEIGH_TX_RETRIES = 1<<8,
  OLSR_L2NEIGH_TX_FAILED = 1<<9,
};

struct olsr_layer2_neighbor_key {
  struct netaddr neighbor_mac;
  struct netaddr radio_mac;
};

struct olsr_layer2_neighbor {
  struct avl_node _node;
  struct olsr_layer2_neighbor_key key;
  uint32_t if_index;

  enum olsr_layer2_neighbor_data _available_data;

  uint16_t signal;
  uint64_t last_seen;

  uint64_t tx_bitrate, rx_bitrate;
  uint32_t tx_bytes, tx_packets;
  uint32_t rx_bytes, rx_packets;

  uint32_t tx_retries, tx_failed;
};

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains signal strength, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_signal(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_SIGNAL) != 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains timestamp
 *    when station has been seen last, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_last_seen(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_LAST_SEEN) != 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains rx bitrate, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_rx_bitrate(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_RX_BITRATE) != 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains rx bytes, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_rx_bytes(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_RX_BYTES) != 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains rx packets, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_rx_packets(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_RX_PACKETS) != 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains tx bitrate, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_tx_bitrate(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_TX_BITRATE) != 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains tx bytes, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_tx_bytes(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_TX_BYTES) != 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains tx packets, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_tx_packets(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_TX_PACKETS) != 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains tx retries, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_tx_retries(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_TX_RETRIES) != 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @return true if data contains tx failed, false otherwise
 */
static INLINE bool
olsr_layer2_neighbor_has_tx_failed(struct olsr_layer2_neighbor *neigh) {
  return (neigh->_available_data & OLSR_L2NEIGH_TX_FAILED) != 0;
}

/**
 * Clear all optional data from a layer2 neighbor
 * @param neigh pointer to layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_clear(struct olsr_layer2_neighbor *neigh) {
  neigh->_available_data = 0;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param signal signal strength to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_signal(struct olsr_layer2_neighbor *neigh, uint16_t signal_dbm) {
  neigh->_available_data |= OLSR_L2NEIGH_SIGNAL;
  neigh->signal = signal_dbm;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param relative relative time since station was last seen
 *   to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_last_seen(struct olsr_layer2_neighbor *neigh, int64_t relative) {
  neigh->_available_data |= OLSR_L2NEIGH_LAST_SEEN;
  neigh->last_seen = olsr_clock_get_absolute(-relative);
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param bitrate incoming bitrate of station to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_rx_bitrate(struct olsr_layer2_neighbor *neigh, uint64_t bitrate) {
  neigh->_available_data |= OLSR_L2NEIGH_RX_BITRATE;
  neigh->rx_bitrate = bitrate;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param bytes total number of bytes received by station
 *    to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_rx_bytes(struct olsr_layer2_neighbor *neigh, uint32_t bytes) {
  neigh->_available_data |= OLSR_L2NEIGH_RX_BYTES;
  neigh->rx_bytes = bytes;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param packets total number of packets received by station
 *    to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_rx_packets(struct olsr_layer2_neighbor *neigh, uint32_t packets) {
  neigh->_available_data |= OLSR_L2NEIGH_RX_PACKETS;
  neigh->rx_packets = packets;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param bytes outgoing bitrate of station
 *    to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_tx_bitrate(struct olsr_layer2_neighbor *neigh, uint64_t bitrate) {
  neigh->_available_data |= OLSR_L2NEIGH_TX_BITRATE;
  neigh->tx_bitrate = bitrate;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param bytes total number of bytes sent to station
 *    to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_tx_bytes(struct olsr_layer2_neighbor *neigh, uint32_t bytes) {
  neigh->_available_data |= OLSR_L2NEIGH_TX_BYTES;
  neigh->tx_bytes = bytes;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param packets total number of packwets sent to station
 *    to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_tx_packets(struct olsr_layer2_neighbor *neigh, uint32_t packets) {
  neigh->_available_data |= OLSR_L2NEIGH_TX_PACKETS;
  neigh->tx_packets = packets;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param retries total number of transmission retries to station
 *   to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_tx_retries(struct olsr_layer2_neighbor *neigh, uint32_t retries) {
  neigh->_available_data |= OLSR_L2NEIGH_TX_PACKETS;
  neigh->tx_retries = retries;
}

/**
 * @param neigh pointer to layer2 neighbor data
 * @param signal signal strength to store in layer2 neighbor data
 */
static INLINE void
olsr_layer2_neighbor_set_tx_fails(struct olsr_layer2_neighbor *neigh, uint32_t failed) {
  neigh->_available_data |= OLSR_L2NEIGH_TX_PACKETS;
  neigh->tx_failed = failed;
}

enum olsr_layer2_network_data {
  OLSR_L2NET_SSID = 1<<0,
  OLSR_L2NET_LAST_SEEN = 1<<1,
  OLSR_L2NET_FREQUENCY = 1<<2,
  OLSR_L2NET_SUPPORTED_RATES = 1<<3,
};

struct olsr_layer2_network {
  struct avl_node _node;

  struct netaddr radio_id;
  uint32_t if_index;

  enum olsr_layer2_network_data _available_data;

  struct netaddr ssid;

  uint64_t last_seen;

  uint64_t frequency;

  uint64_t *supported_rates;
  size_t rate_count;
};

/**
 * @param neigh pointer to layer2 network data
 * @return true if data contains timestamp when network
 *    has been seen last, false otherwise
 */
static INLINE bool
olsr_layer2_network_has_ssid(struct olsr_layer2_network *net) {
  return (net->_available_data & OLSR_L2NET_SSID) != 0;
}

/**
 * @param neigh pointer to layer2 network data
 * @return true if data contains timestamp when network
 *    has been seen last, false otherwise
 */
static INLINE bool
olsr_layer2_network_has_last_seen(struct olsr_layer2_network *net) {
  return (net->_available_data & OLSR_L2NET_LAST_SEEN) != 0;
}

/**
 * @param neigh pointer to layer2 network data
 * @return true if data contains signal strength, false otherwise
 */
static INLINE bool
olsr_layer2_network_has_frequency(struct olsr_layer2_network *net) {
  return (net->_available_data & OLSR_L2NET_FREQUENCY) != 0;
}

/**
 * @param neigh pointer to layer2 network data
 * @return true if data contains supported data-rates, false otherwise
 */
static INLINE bool
olsr_layer2_network_has_supported_rates(struct olsr_layer2_network *net) {
  return (net->_available_data & OLSR_L2NET_SUPPORTED_RATES) != 0;
}

/**
 * Clear all optional data from a layer2 neighbor
 * @param neigh pointer to layer2 neighbor data
 */
static INLINE void
olsr_layer2_network_clear(struct olsr_layer2_network *net) {
  net->_available_data = 0;
}

/**
 * @param neigh pointer to layer2 network data
 * @param relative relative timestamp when network has been seen last
 *    to store in layer2 network data
 */
static INLINE void
olsr_layer2_network_set_ssid(struct olsr_layer2_network *net, struct netaddr *ssid) {
  net->_available_data |= OLSR_L2NET_SSID;
  memcpy(&net->ssid, ssid, sizeof(*ssid));
}

/**
 * @param neigh pointer to layer2 network data
 * @param relative relative timestamp when network has been seen last
 *    to store in layer2 network data
 */
static INLINE void
olsr_layer2_network_set_last_seen(struct olsr_layer2_network *net, uint64_t relative) {
  net->_available_data |= OLSR_L2NET_LAST_SEEN;
  net->last_seen = olsr_clock_get_absolute(-relative);
}

/**
 * @param neigh pointer to layer2 network data
 * @param frequency frequency of network to store in layer2 network data
 */
static INLINE void
olsr_layer2_network_set_frequency(struct olsr_layer2_network *net, uint64_t frequency) {
  net->_available_data |= OLSR_L2NET_FREQUENCY;
  net->frequency = frequency;
}

EXPORT extern struct avl_tree olsr_layer2_network_tree;
EXPORT extern struct avl_tree olsr_layer2_neighbor_tree;

#define OLSR_FOR_ALL_LAYER2_NETWORKS(network, iterator) avl_for_each_element_safe(&olsr_layer2_network_tree, network, _node, iterator)
#define OLSR_FOR_ALL_LAYER2_NEIGHBORS(neighbor, iterator) avl_for_each_element_safe(&olsr_layer2_neighbor_tree, neighbor, _node, iterator)

EXPORT void olsr_layer2_init(void);
EXPORT void olsr_layer2_cleanup(void);

EXPORT struct olsr_layer2_network *olsr_layer2_add_network(
    struct netaddr *ssid, uint32_t if_index);
EXPORT void olsr_layer2_remove_network(struct olsr_layer2_network *);

EXPORT struct olsr_layer2_neighbor *olsr_layer2_get_neighbor(
    struct netaddr *radio_id, struct netaddr *neigh_mac);
EXPORT struct olsr_layer2_neighbor *olsr_layer2_add_neighbor(
    struct netaddr *radio_id, struct netaddr *neigh_mac, uint32_t if_index);
EXPORT void olsr_layer2_remove_neighbor(struct olsr_layer2_neighbor *);

EXPORT int olsr_layer2_network_set_supported_rates(
    struct olsr_layer2_network *net,
    uint64_t *rate_array, size_t rate_count);

/**
 * Retrieve a layer2 network entry from the database
 * @param if_index local interface index of network
 * @return pointer to layer2 network, NULL if not found
 */
static INLINE struct olsr_layer2_network *
olsr_layer2_get_network(uint32_t if_index) {
  struct olsr_layer2_network *net;
  return avl_find_element(&olsr_layer2_network_tree, &if_index, net, _node);
}

#endif /* OLSR_LAYER2_H_ */
