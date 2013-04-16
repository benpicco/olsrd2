
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2013, the olsr.org team - see HISTORY file
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
#include "common/netaddr.h"

#include "core/olsr_class.h"
#include "core/olsr_timer.h"

#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp.h"

#include "olsrv2/olsrv2_tc.h"

static void _cb_tc_node_timeout(void *);

/* classes for topology data */
static struct olsr_class _tc_node_class = {
  .name = "olsrv2 tc node",
  .size = sizeof(struct olsrv2_tc_node),
};

static struct olsr_class _tc_edge_class = {
  .name = "olsrv2 tc edge",
  .size = sizeof(struct olsrv2_tc_edge),
};

static struct olsr_class _tc_attached_class = {
  .name = "olsrv2 tc attached network",
  .size = sizeof(struct olsrv2_tc_attached_network),
};

static struct olsr_class _tc_endpoint_class = {
  .name = "olsrv2 tc attached network endpoint",
  .size = sizeof(struct olsrv2_tc_attached_endpoint),
};

/* validity timer for tc nodes */
static struct olsr_timer_info _validity_info = {
  .name = "olsrv2 tc node validity",
  .callback = _cb_tc_node_timeout,
};
struct avl_tree olsrv2_tc_tree;
struct avl_tree olsrv2_tc_endpoint_tree;

void
olsrv2_tc_init(void) {
  olsr_class_add(&_tc_node_class);
  olsr_class_add(&_tc_edge_class);
  olsr_class_add(&_tc_attached_class);
  olsr_class_add(&_tc_endpoint_class);

  avl_init(&olsrv2_tc_tree, avl_comp_netaddr, false);
  avl_init(&olsrv2_tc_endpoint_tree, avl_comp_netaddr, true);
}

void
olsrv2_tc_cleanup(void) {
  struct olsrv2_tc_node *node, *n_it;

  avl_for_each_element_safe(&olsrv2_tc_tree, node, _originator_node, n_it) {
    olsrv2_tc_node_remove(node);
  }

  olsr_class_remove(&_tc_endpoint_class);
  olsr_class_remove(&_tc_attached_class);
  olsr_class_remove(&_tc_edge_class);
  olsr_class_remove(&_tc_node_class);
}

struct olsrv2_tc_node *
olsrv2_tc_node_add(struct netaddr *originator,
    uint64_t vtime, uint16_t ansn) {
  struct olsrv2_tc_node *node;

  node = avl_find_element(
      &olsrv2_tc_tree, originator, node, _originator_node);
  if (!node) {
    node = olsr_class_malloc(&_tc_node_class);
    if (node == NULL) {
      return NULL;
    }

    /* copy key and attach it to node */
    memcpy(&node->target.addr, originator, sizeof(*originator));
    node->_originator_node.key = &node->target.addr;

    /* initialize node */
    avl_init(&node->_edges, avl_comp_netaddr, false);
    avl_init(&node->_attached_networks, avl_comp_netaddr, false);

    node->_validity_time.info = &_validity_info;
    node->_validity_time.cb_context = node;

    node->ansn = ansn;

    /* hook into global tree */
    avl_insert(&olsrv2_tc_tree, node->_originator_node);
  }

  olsr_timer_set(&node->_validity_time, vtime);
  return node;
}

void
olsrv2_tc_node_remove(struct olsrv2_tc_node *node) {
  struct olsrv2_tc_edge *edge, *edge_it;
  struct olsrv2_tc_attached_network *net, *net_it;

  /* remove tc_edges */
  avl_for_each_element_safe(&node->_edges, edge, _node, edge_it) {
    /* some edges might just become virtual */
    olsrv2_tc_edge_remove(edge);
  }

  /* remove attached networks */
  avl_for_each_element_safe(
      &node->_attached_networks, net, _src_node, net_it) {
    olsrv2_tc_attached_network_remove(net);
  }

  /* stop validity timer */
  olsr_timer_stop(&node->_validity_time);

  /* remove from global tree and free memory if node is not needed anymore*/
  if (node->_edges.count == 0) {
    avl_remove(&olsrv2_tc_tree, &node->_originator_node);
    olsr_class_free(&_tc_node_class, node);
  }
}

struct olsrv2_tc_edge *
olsrv2_tc_edge_add(struct olsrv2_tc_node *src, struct netaddr *addr) {
  struct olsrv2_tc_edge *edge = NULL, *inverse = NULL;
  struct olsrv2_tc_node *dst = NULL;
  int i;

  edge = avl_find_element(&src->_edges, addr, edge, _node);
  if (edge != NULL) {
    edge->virtual = false;
    return edge;
  }

  /* allocate edge */
  edge = olsr_class_malloc(&_tc_edge_class);
  if (edge == NULL) {
    return NULL;
  }

  /* allocate inverse edge */
  inverse = olsr_class_malloc(&_tc_edge_class);
  if (inverse == NULL) {
    olsr_class_free(&_tc_edge_class, edge);
    return NULL;
  }

  /* find or allocate destination node */
  dst = avl_find_element(&olsrv2_tc_tree, addr, dst, _originator_node);
  if (dst == NULL) {
    /* create virtual node */
    dst = olsrv2_tc_node_add(addr, 0);
    if (dst == NULL) {
      olsr_class_free(&_tc_edge_class, edge);
      olsr_class_free(&_tc_edge_class, inverse);
      return NULL;
    }
  }

  /* initialize edge */
  edge->src = src;
  edge->dst = dst;
  edge->inverse = inverse;
  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    edge->cost[i] = RFC5444_METRIC_INFINITE;
  }

  /* hook edge into src node */
  edge->_node.key = &dst->target.addr;
  avl_insert(&src->_edges, &edge->_node);

  /* initialize inverse (virtual) edge */
  inverse->src = dst;
  inverse->dst = src;
  inverse->inverse = edge;
  inverse->virtual = true;
  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    inverse->cost[i] = RFC5444_METRIC_INFINITE;
  }

  /* hook inverse edge into dst node */
  inverse->_node.key = &src->target.addr;
  avl_insert(&dst->_edges, &inverse->_node);

  return edge;
}

bool
olsrv2_tc_edge_remove(struct olsrv2_tc_edge *edge) {
  bool removed_node = false;

  if (edge->virtual) {
    /* nothing to do */
    return false;
  }

  if (!edge->inverse->virtual) {
    /* make this edge virtual */
    edge->virtual = true;
    return false;
  }

  /* unhook edge from both sides */
  avl_remove(&edge->src->_edges, &edge->_node);
  avl_remove(&edge->dst->_edges, &edge->inverse->_node);

  if (edge->dst->_edges.count == 0
      && olsr_timer_is_active(!edge->dst->_validity_time)) {
    /*
     * node is already virtual and has no
     * incoming links anymore.
     */

    olsrv2_tc_node_remove(edge->dst);
    removed_node = true;
  }

  olsr_class_free(&_tc_edge_class, edge->inverse);
  olsr_class_free(&_tc_edge_class, edge);

  return removed_node;
}

struct olsrv2_tc_attached_network *
olsrv2_tc_attached_network_add(
    struct olsrv2_tc_node *node, struct netaddr *prefix) {
  struct olsrv2_tc_attached_network *net;
  struct olsrv2_tc_attached_endpoint *end;
  int i;

  net = avl_find_element(&node->_attached_networks, prefix, net, _src_node);
  if (net != NULL) {
    return net;
  }

  net = olsr_class_malloc(&_tc_attached_class);
  if (net == NULL) {
    return NULL;
  }

  end = avl_find_element(&olsrv2_tc_endpoint_tree, prefix, end, _node);
  if (end == NULL) {
    /* create new endpoint */
    end = olsr_class_malloc(&_tc_endpoint_class);
    if (end == NULL) {
      olsr_class_free(&_tc_attached_class, net);
      return NULL;
    }

    /* initialize endpoint */
    end->target.attached_network = true;
    avl_init(&end->_attached_networks, avl_comp_netaddr, false);

    /* attach to global tree */
    memcpy(&end->target.addr, prefix, sizeof(*prefix));
    end->_node.key = &end->target.addr;
  }

  /* initialize attached network */
  net->src = node;
  net->dst = end;
  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    net->cost[i] = RFC5444_METRIC_INFINITE;
  }

  /* hook into src node */
  net->_src_node.key = end->target;
  avl_insert(&node->_attached_networks, &net->_src_node);

  /* hook into endpoint */
  net->_endpoint_node.key = &node->target.addr;
  avl_insert(&end->_attached_networks, &net->_endpoint_node);

  return net;
}

void
olsrv2_tc_attached_network_remove(
    struct olsrv2_tc_attached_network *net) {
  /* remove from node */
  avl_remove(&net->src->_attached_networks, &net->_src_node);

  /* remove from endpoint */
  avl_remove(&net->dst->_attached_networks, &net->_endpoint_node);

  if (net->dst->_attached_networks.count == 0) {
    /* remove endpoint */
    avl_remove(&olsrv2_tc_endpoint_tree, &net->dst->_node);
    olsr_class_free(&_tc_endpoint_class, net->dst);
  }

  /* free attached network */
  olsr_class_free(&_tc_attached_class, net);
}

void
_cb_tc_node_timeout(void *ptr) {
  struct olsrv2_tc_node *node = ptr;

  olsrv2_tc_node_remove(node);
}
