
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

#include "olsr_callbacks.h"
#include "olsr_logging.h"
#include "olsr_memcookie.h"
#include "olsr_timer.h"
#include "olsr.h"
#include "olsr_layer2.h"

static void _remove_neighbor(struct olsr_layer2_neighbor *);
static void _remove_network(struct olsr_layer2_network *);
static void _cb_neighbor_timeout(void *ptr);
static void _cb_network_timeout(void *ptr);
static int _avl_comp_l2neigh(const void *k1, const void *k2, void *);
static const char *_cb_get_neighbor_name(struct olsr_callback_str *buf,void *);
static const char *_cb_get_network_name(struct olsr_callback_str *buf,void *);

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

static struct olsr_timer_info _network_vtime_info = {
  .name = "layer2 network vtime",
  .callback = _cb_network_timeout,
};

static struct olsr_timer_info _neighbor_vtime_info = {
  .name = "layer2 neighbor vtime",
  .callback = _cb_neighbor_timeout,
};

static struct olsr_callback_provider _network_callback = {
  .name = CALLBACK_ID_LAYER2_NETWORK,
  .cb_getkey = _cb_get_network_name,
};

static struct olsr_callback_provider _neighbor_callback = {
  .name = CALLBACK_ID_LAYER2_NEIGHBOR,
  .cb_getkey = _cb_get_neighbor_name,
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

  olsr_callback_add(&_network_callback);
  olsr_callback_add(&_neighbor_callback);

  olsr_timer_add(&_network_vtime_info);
  olsr_timer_add(&_neighbor_vtime_info);

  avl_init(&olsr_layer2_network_tree, avl_comp_netaddr, false, NULL);
  avl_init(&olsr_layer2_neighbor_tree, _avl_comp_l2neigh, false, NULL);
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
    net->active = false;
    olsr_layer2_remove_network(net);
  }

  OLSR_FOR_ALL_LAYER2_NEIGHBORS(neigh, neigh_it) {
    neigh->active = false;
    olsr_layer2_remove_neighbor(neigh);
  }

  olsr_timer_remove(&_network_vtime_info);
  olsr_timer_remove(&_neighbor_vtime_info);
  olsr_callback_remove(&_network_callback);
  olsr_callback_remove(&_neighbor_callback);
  olsr_memcookie_remove(&_network_cookie);
  olsr_memcookie_remove(&_neighbor_cookie);
}

/**
 * Add an active network to the database. If an entry for the
 * interface does already exists, it will be returned by this
 * function and no new entry will be created.
 * @param radio_id ID of the radio
 * @param if_index local interface index of network
 * @param vtime validity time of data
 * @return pointer to layer2 network data, NULL if OOM
 */
struct olsr_layer2_network *
olsr_layer2_add_network(struct netaddr *radio_id, uint32_t if_index,
    uint64_t vtime) {
  struct olsr_layer2_network *net;

  assert (vtime > 0);

  net = olsr_layer2_get_network(radio_id);
  if (!net) {
    net = olsr_memcookie_malloc(&_network_cookie);
    if (!net) {
      return NULL;
    }

    net->_node.key = &net->radio_id;
    memcpy (&net->radio_id, radio_id, sizeof(*radio_id));
    net->if_index = if_index;

    net->_valitity_timer.info = &_network_vtime_info;
    net->_valitity_timer.cb_context = net;

    avl_insert(&olsr_layer2_network_tree, &net->_node);

    olsr_callback_event(&_network_callback, net, CALLBACK_EVENT_ADD);
  }

  OLSR_DEBUG(LOG_MAIN, "Reset validity of network timer: %"PRIu64,
      vtime);
  net->active = true;
  olsr_timer_set(&net->_valitity_timer, vtime);
  return net;
}

/**
 * Remove a layer2 network from the database
 * @param net pointer to layer2 network data
 */
void
olsr_layer2_remove_network(struct olsr_layer2_network *net) {
  if (net->active) {
    /* restart validity timer */
    olsr_timer_set(&net->_valitity_timer,
      olsr_timer_get_period(&net->_valitity_timer));
  }
  _remove_network(net);
}

/**
 * Retrieve a layer2 neighbor from the database
 * @param radio_id pointer to radio_id of network
 * @param neigh_mac pointer to layer2 address of neighbor
 * @return pointer to layer2 neighbor data, NULL if not found
 */
struct olsr_layer2_neighbor *
olsr_layer2_get_neighbor(struct netaddr *radio_id, struct netaddr *neigh_mac) {
  struct olsr_layer2_neighbor_key key;
  struct olsr_layer2_neighbor *neigh;

  key.radio_mac = *radio_id;
  key.neighbor_mac = *neigh_mac;

  return avl_find_element(&olsr_layer2_neighbor_tree, &key, neigh, _node);
}

/**
 * Add a layer2 neighbor to the database. If an entry for the
 * neighbor on the interface does already exists, it will be
 * returned by this function and no new entry will be created.
 * @param radio_id pointer to radio_id of network
 * @param neigh_mac layer2 address of neighbor
 * @param if_index local interface index of the neighbor
 * @param vtime validity time of data
 * @return pointer to layer2 neighbor data, NULL if OOM
 */
struct olsr_layer2_neighbor *
olsr_layer2_add_neighbor(struct netaddr *radio_id, struct netaddr *neigh_mac,
    uint32_t if_index, uint64_t vtime) {
  struct olsr_layer2_neighbor *neigh;

  assert (vtime > 0);

  neigh = olsr_layer2_get_neighbor(radio_id, neigh_mac);
  if (!neigh) {
    neigh = olsr_memcookie_malloc(&_neighbor_cookie);
    if (!neigh) {
      return NULL;
    }

    neigh->if_index = if_index;
    memcpy(&neigh->key.radio_mac, radio_id, sizeof(*radio_id));
    memcpy(&neigh->key.neighbor_mac, neigh_mac, sizeof(*neigh_mac));

    neigh->_node.key = &neigh->key;
    neigh->_valitity_timer.info = &_neighbor_vtime_info;
    neigh->_valitity_timer.cb_context = neigh;

    avl_insert(&olsr_layer2_neighbor_tree, &neigh->_node);
    olsr_callback_event(&_neighbor_callback, neigh, CALLBACK_EVENT_ADD);
  }

  neigh->active = true;
  olsr_timer_set(&neigh->_valitity_timer, vtime);
  return neigh;
}

/**
 * Remove a layer2 neighbor from the database
 * @param neigh pointer to layer2 neighbor
 */
void
olsr_layer2_remove_neighbor(struct olsr_layer2_neighbor *neigh) {
  if (neigh->active) {
    /* restart validity timer */
    olsr_timer_set(&neigh->_valitity_timer,
        olsr_timer_get_period(&neigh->_valitity_timer));
  }
  _remove_neighbor(neigh);
}

/**
 * Set a new list of supported rates. Data will not be changed if an
 * error happens.
 * @param net pointer to layer2 network
 * @param rate_array pointer to array of supported rates
 * @param rate_count number of supported rates
 * @return -1 if an out of memory error happened, 0 otherwise.
 */
int
olsr_layer2_network_set_supported_rates(struct olsr_layer2_network *net,
    uint64_t *rate_array, size_t rate_count) {
  uint64_t *rates;

  rates = realloc(net->supported_rates, rate_count * sizeof(uint64_t));
  if (rates == NULL) {
    return -1;
  }

  net->_available_data |= OLSR_L2NET_SUPPORTED_RATES;
  net->supported_rates = rates;
  net->rate_count = rate_count;
  memcpy(rates, rate_array, sizeof(uint64_t) * rate_count);

  return 0;
}

/**
 * Remove a layer2 neighbor from the database
 * @param neigh pointer to layer2 neighbor
 */
static void
_remove_neighbor(struct olsr_layer2_neighbor *neigh) {
  if (neigh->active) {
    olsr_callback_event(&_neighbor_callback, neigh, CALLBACK_EVENT_REMOVE);
    neigh->active = false;
    return;
  }
  avl_remove(&olsr_layer2_neighbor_tree, &neigh->_node);
  olsr_timer_stop(&neigh->_valitity_timer);
  olsr_memcookie_free(&_neighbor_cookie, neigh);
}

/**
 * Remove a layer2 network from the database
 * @param net pointer to layer2 network data
 */
static void
_remove_network(struct olsr_layer2_network *net) {
  if (net->active) {
    olsr_callback_event(&_network_callback, net, CALLBACK_EVENT_REMOVE);
    net->active = false;
    return;
  }

  avl_remove(&olsr_layer2_network_tree, &net->_node);
  olsr_timer_stop(&net->_valitity_timer);
  free (net->supported_rates);
  olsr_memcookie_free(&_network_cookie, net);
}

/**
 * Validity time callback for neighbor entries
 * @param ptr pointer to neighbor entry
 */
static void
_cb_neighbor_timeout(void *ptr) {
  _remove_neighbor(ptr);
}

/**
 * Validity time callback for network entries
 * @param ptr pointer to network entry
 */
static void
_cb_network_timeout(void *ptr) {
  _remove_network(ptr);
}

/**
 * AVL comparator for layer2 neighbor nodes
 * @param k1 pointer to first layer2 neighbor
 * @param k2 pointer to second layer2 neighbor
 * @param ptr unused
 * @return +1 if k1>k2, -1 if k1<k2, 0 if k1==k2
 */
static int
_avl_comp_l2neigh(const void *k1, const void *k2,
    void *ptr __attribute__((unused))) {
  const struct olsr_layer2_neighbor_key *key1, *key2;
  int result;

  key1 = k1;
  key2 = k2;

  result = netaddr_cmp(&key1->radio_mac, &key2->radio_mac);
  if (!result) {
    result = netaddr_cmp(&key1->neighbor_mac, &key2->neighbor_mac);
  }
  return result;
}

/**
 * Construct human readable object id of neighbor for callbacks
 * @param buf pointer to callback id output buffer
 * @param ptr pointer to l2 neighbor
 * @return pointer to id
 */
static const char *
_cb_get_neighbor_name(struct olsr_callback_str *buf, void *ptr) {
  struct netaddr_str buf1, buf2;
  struct olsr_layer2_neighbor *nbr;

  nbr = ptr;

  snprintf(buf->buf, sizeof(*buf), "neigh=%s/radio=%s",
      netaddr_to_string(&buf1, &nbr->key.neighbor_mac),
      netaddr_to_string(&buf2, &nbr->key.radio_mac));
  return buf->buf;
}

/**
 * Construct human readable object id of network for callbacks
 * @param buf pointer to callback id output buffer
 * @param ptr pointer to l2 neighbor
 * @return pointer to id
 */
static const char *
_cb_get_network_name(struct olsr_callback_str *buf, void *ptr) {
  struct netaddr_str buf1;
  struct olsr_layer2_network *net;

  net = ptr;

  snprintf(buf->buf, sizeof(*buf), "radio=%s",
      netaddr_to_string(&buf1, &net->radio_id));
  return buf->buf;
}
