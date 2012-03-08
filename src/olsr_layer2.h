
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

enum olsr_layer2_neighbor_data {
  OLSR_L2NEIGH_SIGNAL = 1<<0,
  OLSR_L2NEIGH_INACTIVE_TIME = 1<<1,
  OLSR_L2NEIGH_RX_BITRATE = 1<<2,
  OLSR_L2NEIGH_RX_BYTES = 1<<3,
  OLSR_L2NEIGH_RX_PACKETS = 1<<4,
  OLSR_L2NEIGH_TX_BITRATE = 1<<5,
  OLSR_L2NEIGH_TX_BYTES = 1<<6,
  OLSR_L2NEIGH_TX_PACKETS = 1<<7,
  OLSR_L2NEIGH_TX_RETRIES = 1<<8,
  OLSR_L2NEIGH_TX_FAILED = 1<<9,
};

struct olsr_layer2_neighbor {
  struct avl_node _node;

  struct netaddr mac_address;
  uint32_t if_index;

  enum olsr_layer2_neighbor_data available_data;

  uint16_t signal;
  uint64_t last_seen;

  uint64_t tx_bitrate, rx_bitrate;
  uint32_t tx_bytes, tx_packets;
  uint32_t rx_bytes, rx_packets;

  uint32_t tx_retries, tx_failed;
};

enum olsr_layer2_network_data {
  OLSR_L2NET_INACTIVE_TIME = 1<<0,
  OLSR_L2NET_FREQUENCY = 1<<1,
  OLSR_L2NET_SUPPORTED_RATES = 1<<2,
};

struct olsr_layer2_network {
  struct avl_node _node;

  struct netaddr id;
  uint32_t if_index;

  enum olsr_layer2_network_data available_data;

  uint64_t last_seen;

  uint64_t frequency;

  uint64_t *supported_rates;
  size_t rate_count;
};

EXPORT extern struct avl_tree olsr_layer2_network_tree;
EXPORT extern struct avl_tree olsr_layer2_neighbor_tree;

EXPORT void olsr_layer2_init(void);
EXPORT void olsr_layer2_cleanup(void);

EXPORT struct olsr_layer2_network *olsr_layer2_add_network(uint32_t if_index);
EXPORT void olsr_layer2_remove_network(struct olsr_layer2_network *);

EXPORT struct olsr_layer2_neighbor *olsr_layer2_get_neighbor(
    struct netaddr *, uint32_t if_index);
EXPORT struct olsr_layer2_neighbor *olsr_layer2_add_neighbor(
    struct netaddr *, uint32_t if_index);
EXPORT void olsr_layer2_remove_neighbor(struct olsr_layer2_neighbor *);

static INLINE struct olsr_layer2_network *
olsr_layer2_get_network(uint32_t if_index) {
  struct olsr_layer2_network *net;
  return avl_find_element(&olsr_layer2_network_tree, &if_index, net, _node);
}

#endif /* OLSR_LAYER2_H_ */
