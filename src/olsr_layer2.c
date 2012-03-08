
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

void
olsr_layer2_init(void) {
  if (olsr_subsystem_init(&_layer2_state))
    return;

  olsr_memcookie_add(&_network_cookie);
  olsr_memcookie_add(&_neighbor_cookie);

  /* TODO: init avl trees */
}

void
olsr_layer2_cleanup(void) {
  /* TODO: free avl tree elements */

  olsr_memcookie_remove(&_network_cookie);
  olsr_memcookie_remove(&_neighbor_cookie);
}

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

void
olsr_layer2_remove_network(struct olsr_layer2_network *net) {
  avl_remove(&olsr_layer2_network_tree, &net->_node);
  olsr_memcookie_free(&_network_cookie, net);
}

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

void
olsr_layer2_remove_neighbor(struct olsr_layer2_neighbor *neigh) {
  avl_remove(&olsr_layer2_neighbor_tree, &neigh->_node);
  olsr_memcookie_free(&_neighbor_cookie, neigh);
}
