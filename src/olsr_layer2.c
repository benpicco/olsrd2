
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

#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/common_types.h"

#include "olsr_memcookie.h"
#include "olsr.h"
#include "olsr_layer2.h"

struct avl_tree olsr_layer2_network_tree;
struct avl_tree olsr_layer2_neighbor_tree;

static struct olsr_memcookie_info _network_cookie = {
  .name = "layer2 networks",
  .size = sizeof(struct olsr_layer2_network),
};

static struct olsr_memcookie_info _neighbor_cookie = {
  .name = "layer2 neighbors",
  .size = sizeof(struct olsr_layer2_neighbor),
};

OLSR_SUBSYSTEM_STATE(_layer2_state);

/**
 * Initialize layer2 subsystem
 */
void
olsr_layer2_init(void) {
  if (olsr_subsystem_init(&_layer2_state))
    return;

  olsr_memcookie_add(&_network_cookie);
  olsr_memcookie_add(&_neighbor_cookie);

  avl_init(&olsr_layer2_network_tree, avl_comp_uint32, false, NULL);
  avl_init(&olsr_layer2_neighbor_tree, avl_comp_netaddr, true, NULL);
}

/**
 * Cleanup all resources allocated by layer2 subsystem
 */
void
olsr_layer2_cleanup(void) {
  struct olsr_layer2_neighbor *neigh, *neigh_it;
  struct olsr_layer2_network *net, *net_it;

  if (olsr_subsystem_cleanup(&_layer2_state))
    return;

  OLSR_FOR_ALL_LAYER2_NETWORKS(net, net_it) {
    olsr_layer2_remove_network(net);
  }

  OLSR_FOR_ALL_LAYER2_NEIGHBORS(neigh, neigh_it) {
    olsr_layer2_remove_neighbor(neigh);
  }

  olsr_memcookie_remove(&_network_cookie);
  olsr_memcookie_remove(&_neighbor_cookie);
}

/**
 * Add an active network to the database. If an entry for the
 * interface does already exists, it will be returned by this
 * function and no new entry will be created.
 * @param if_index local interface index of network
 * @return pointer to layer2 network data, NULL if OOM
 */
struct olsr_layer2_network *
olsr_layer2_add_network(uint32_t if_index) {
  struct olsr_layer2_network *net;

  net = olsr_layer2_get_network(if_index);
  if (!net) {
    net = olsr_memcookie_malloc(&_network_cookie);
    if (net) {
      net->if_index = if_index;
      net->_node.key = &net->if_index;

      avl_insert(&olsr_layer2_network_tree, &net->_node);
    }
  }
  return net;
}

/**
 * Remove a layer2 network from the database
 * @param net pointer to layer2 network data
 */
void
olsr_layer2_remove_network(struct olsr_layer2_network *net) {
  avl_remove(&olsr_layer2_network_tree, &net->_node);
  olsr_memcookie_free(&_network_cookie, net);
}

/**
 * Retrieve a layer2 neighbor from the database
 * @param mac pointer to layer2 address of neighbor
 * @param if_index local interface index
 * @return pointer to layer2 neighbor data, NULL if not found
 */
struct olsr_layer2_neighbor *
olsr_layer2_get_neighbor(struct netaddr *mac, uint32_t if_index) {
  struct olsr_layer2_neighbor *neigh, *start;

  avl_for_each_elements_with_key(&olsr_layer2_neighbor_tree, neigh, _node, start, mac) {
    if (neigh->if_index == if_index) {
      return neigh;
    }
  }
  return NULL;
}

/**
 * Add a layer2 neighbor to the database. If an entry for the
 * neighbor on the interface does already exists, it will be
 * returned by this function and no new entry will be created.
 * @param mac layer2 address of neighbor
 * @param if_index local interface index of the neighbor
 * @return pointer to layer2 neighbor data, NULL if OOM
 */
struct olsr_layer2_neighbor *
olsr_layer2_add_neighbor(struct netaddr *mac, uint32_t if_index) {
  struct olsr_layer2_neighbor *neigh;

  neigh = olsr_layer2_get_neighbor(mac, if_index);
  if (!neigh) {
    neigh = olsr_memcookie_malloc(&_neighbor_cookie);
    if (neigh) {
      neigh->if_index = if_index;
      memcpy(&neigh->mac_address, mac, sizeof(*mac));

      neigh->_node.key = &neigh->mac_address;

      avl_insert(&olsr_layer2_neighbor_tree, &neigh->_node);
    }
  }
  return neigh;
}

/**
 * Remove a layer2 neighbor from the database
 * @param neigh pointer to layer2 neighbor
 */
void
olsr_layer2_remove_neighbor(struct olsr_layer2_neighbor *neigh) {
  avl_remove(&olsr_layer2_neighbor_tree, &neigh->_node);
  olsr_memcookie_free(&_neighbor_cookie, neigh);
}
