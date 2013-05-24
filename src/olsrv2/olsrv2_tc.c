
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
#include "rfc5444/rfc5444.h"
#include "subsystems/oonf_class.h"
#include "subsystems/oonf_timer.h"

#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp.h"

#include "olsrv2/olsrv2_routing.h"
#include "olsrv2/olsrv2_tc.h"

/* prototypes */
static void _cb_tc_node_timeout(void *);
static bool _remove_edge(struct olsrv2_tc_edge *edge, bool cleanup);

/* classes for topology data */
static struct oonf_class _tc_node_class = {
  .name = "olsrv2 tc node",
  .size = sizeof(struct olsrv2_tc_node),
};

static struct oonf_class _tc_edge_class = {
  .name = "olsrv2 tc edge",
  .size = sizeof(struct olsrv2_tc_edge),
};

static struct oonf_class _tc_attached_class = {
  .name = "olsrv2 tc attached network",
  .size = sizeof(struct olsrv2_tc_attachment),
};

static struct oonf_class _tc_endpoint_class = {
  .name = "olsrv2 tc attached network endpoint",
  .size = sizeof(struct olsrv2_tc_endpoint),
};

/* validity timer for tc nodes */
static struct oonf_timer_info _validity_info = {
  .name = "olsrv2 tc node validity",
  .callback = _cb_tc_node_timeout,
};

/* global trees for tc nodes and endpoints */
struct avl_tree olsrv2_tc_tree;
struct avl_tree olsrv2_tc_endpoint_tree;

/**
 * Initialize tc database
 */
void
olsrv2_tc_init(void) {
  oonf_class_add(&_tc_node_class);
  oonf_class_add(&_tc_edge_class);
  oonf_class_add(&_tc_attached_class);
  oonf_class_add(&_tc_endpoint_class);

  avl_init(&olsrv2_tc_tree, avl_comp_netaddr, false);
  avl_init(&olsrv2_tc_endpoint_tree, avl_comp_netaddr, true);
}

/**
 * Cleanup tc database
 */
void
olsrv2_tc_cleanup(void) {
  struct olsrv2_tc_node *node, *n_it;
  struct olsrv2_tc_edge *edge, *e_it;
  struct olsrv2_tc_attachment *a_end, *ae_it;

  avl_for_each_element(&olsrv2_tc_tree, node, _originator_node) {
    avl_for_each_element_safe(&node->_edges, edge, _node, e_it) {
      /* remove edge without cleaning up the node */
      _remove_edge(edge, false);
    }

    avl_for_each_element_safe(&node->_endpoints, a_end, _src_node, ae_it) {
      olsrv2_tc_endpoint_remove(a_end);
    }
  }

  avl_for_each_element_safe(&olsrv2_tc_tree, node, _originator_node, n_it) {
    olsrv2_tc_node_remove(node);
  }

  oonf_class_remove(&_tc_endpoint_class);
  oonf_class_remove(&_tc_attached_class);
  oonf_class_remove(&_tc_edge_class);
  oonf_class_remove(&_tc_node_class);
}

/**
 * Add a new tc node to the database
 * @param originator originator address of node
 * @param vtime validity time for node entry
 * @param ansn answer set number of node
 * @return pointer to node, NULL if out of memory
 */
struct olsrv2_tc_node *
olsrv2_tc_node_add(struct netaddr *originator,
    uint64_t vtime, uint16_t ansn) {
  struct olsrv2_tc_node *node;

  node = avl_find_element(
      &olsrv2_tc_tree, originator, node, _originator_node);
  if (!node) {
    node = oonf_class_malloc(&_tc_node_class);
    if (node == NULL) {
      return NULL;
    }

    /* copy key and attach it to node */
    memcpy(&node->target.addr, originator, sizeof(*originator));
    node->_originator_node.key = &node->target.addr;

    /* initialize node */
    avl_init(&node->_edges, avl_comp_netaddr, false);
    avl_init(&node->_endpoints, avl_comp_netaddr, false);

    node->_validity_time.info = &_validity_info;
    node->_validity_time.cb_context = node;

    node->ansn = ansn;

    /* initialize dijkstra data */
    olsrv2_routing_dijkstra_node_init(&node->target._dijkstra);

    /* hook into global tree */
    avl_insert(&olsrv2_tc_tree, &node->_originator_node);

    /* fire event */
    oonf_class_event(&_tc_node_class, node, OONF_OBJECT_ADDED);
  }
  else if (!oonf_timer_is_active(&node->_validity_time)) {
    /* node was virtual */
    node->ansn = ansn;

    /* fire event */
    oonf_class_event(&_tc_node_class, node, OONF_OBJECT_ADDED);
  }
  oonf_timer_set(&node->_validity_time, vtime);
  return node;
}

/**
 * Remove a tc node from the database
 * @param node pointer to node
 */
void
olsrv2_tc_node_remove(struct olsrv2_tc_node *node) {
  struct olsrv2_tc_edge *edge, *edge_it;
  struct olsrv2_tc_attachment *net, *net_it;

  oonf_class_event(&_tc_node_class, node, OONF_OBJECT_REMOVED);

  /* remove tc_edges */
  avl_for_each_element_safe(&node->_edges, edge, _node, edge_it) {
    /* some edges might just become virtual */
    olsrv2_tc_edge_remove(edge);
  }

  /* remove attached networks */
  avl_for_each_element_safe(
      &node->_endpoints, net, _src_node, net_it) {
    olsrv2_tc_endpoint_remove(net);
  }

  /* stop validity timer */
  oonf_timer_stop(&node->_validity_time);

  /* remove from global tree and free memory if node is not needed anymore*/
  if (node->_edges.count == 0) {
    avl_remove(&olsrv2_tc_tree, &node->_originator_node);
    oonf_class_free(&_tc_node_class, node);
  }
}

/**
 * Add a tc edge to the database
 * @param src pointer to source node
 * @param addr pointer to destination address
 * @return pointer to TC edge, NULL if out of memory
 */
struct olsrv2_tc_edge *
olsrv2_tc_edge_add(struct olsrv2_tc_node *src, struct netaddr *addr) {
  struct olsrv2_tc_edge *edge = NULL, *inverse = NULL;
  struct olsrv2_tc_node *dst = NULL;
  int i;

  edge = avl_find_element(&src->_edges, addr, edge, _node);
  if (edge != NULL) {
    edge->virtual = false;

    /* fire event */
    oonf_class_event(&_tc_edge_class, edge, OONF_OBJECT_ADDED);
    return edge;
  }

  /* allocate edge */
  edge = oonf_class_malloc(&_tc_edge_class);
  if (edge == NULL) {
    return NULL;
  }

  /* allocate inverse edge */
  inverse = oonf_class_malloc(&_tc_edge_class);
  if (inverse == NULL) {
    oonf_class_free(&_tc_edge_class, edge);
    return NULL;
  }

  /* find or allocate destination node */
  dst = avl_find_element(&olsrv2_tc_tree, addr, dst, _originator_node);
  if (dst == NULL) {
    /* create virtual node */
    dst = olsrv2_tc_node_add(addr, 0, 0);
    if (dst == NULL) {
      oonf_class_free(&_tc_edge_class, edge);
      oonf_class_free(&_tc_edge_class, inverse);
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

  /* fire event */
  oonf_class_event(&_tc_edge_class, edge, OONF_OBJECT_ADDED);
  return edge;
}

/**
 * Remove a tc edge from the database
 * @param edge pointer to tc edge
 * @return true if destination of edge was removed too
 */
bool
olsrv2_tc_edge_remove(struct olsrv2_tc_edge *edge) {
  return _remove_edge(edge, true);
}

/**
 * Add an endpoint to a tc node
 * @param node pointer to tc node
 * @param prefix address prefix of endpoint
 * @param mesh true if an interface of a mesh node, #
 *   false if a local attached network.
 * @return pointer to tc attachment, NULL if out of memory
 */
struct olsrv2_tc_attachment *
olsrv2_tc_endpoint_add(struct olsrv2_tc_node *node,
    struct netaddr *prefix, bool mesh) {
  struct olsrv2_tc_attachment *net;
  struct olsrv2_tc_endpoint *end;
  int i;

  net = avl_find_element(&node->_endpoints, prefix, net, _src_node);
  if (net != NULL) {
    return net;
  }

  net = oonf_class_malloc(&_tc_attached_class);
  if (net == NULL) {
    return NULL;
  }

  end = avl_find_element(&olsrv2_tc_endpoint_tree, prefix, end, _node);
  if (end == NULL) {
    /* create new endpoint */
    end = oonf_class_malloc(&_tc_endpoint_class);
    if (end == NULL) {
      oonf_class_free(&_tc_attached_class, net);
      return NULL;
    }

    /* initialize endpoint */
    end->target.type = mesh ? OONFV2_ADDRESS_TARGET : OONFV2_NETWORK_TARGET;
    avl_init(&end->_attached_networks, avl_comp_netaddr, false);

    /* attach to global tree */
    memcpy(&end->target.addr, prefix, sizeof(*prefix));
    end->_node.key = &end->target.addr;
    avl_insert(&olsrv2_tc_endpoint_tree, &end->_node);

    oonf_class_event(&_tc_endpoint_class, end, OONF_OBJECT_ADDED);
  }

  /* initialize attached network */
  net->src = node;
  net->dst = end;
  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    net->cost[i] = RFC5444_METRIC_INFINITE;
  }

  /* hook into src node */
  net->_src_node.key = &end->target;
  avl_insert(&node->_endpoints, &net->_src_node);

  /* hook into endpoint */
  net->_endpoint_node.key = &node->target.addr;
  avl_insert(&end->_attached_networks, &net->_endpoint_node);

  /* initialize dijkstra data */
  olsrv2_routing_dijkstra_node_init(&end->target._dijkstra);

  oonf_class_event(&_tc_attached_class, net, OONF_OBJECT_ADDED);
  return net;
}

/**
 * Remove a tc attachment from the database
 * @param net pointer to tc attachment
 */
void
olsrv2_tc_endpoint_remove(
    struct olsrv2_tc_attachment *net) {
  oonf_class_event(&_tc_attached_class, net, OONF_OBJECT_REMOVED);

  /* remove from node */
  avl_remove(&net->src->_endpoints, &net->_src_node);

  /* remove from endpoint */
  avl_remove(&net->dst->_attached_networks, &net->_endpoint_node);

  if (net->dst->_attached_networks.count == 0) {
    oonf_class_event(&_tc_endpoint_class, net->dst, OONF_OBJECT_REMOVED);

    /* remove endpoint */
    avl_remove(&olsrv2_tc_endpoint_tree, &net->dst->_node);
    oonf_class_free(&_tc_endpoint_class, net->dst);
  }

  /* free attached network */
  oonf_class_free(&_tc_attached_class, net);
}

/**
 * Callback triggered when a tc node times out
 * @param ptr pointer to tc node
 */
void
_cb_tc_node_timeout(void *ptr) {
  struct olsrv2_tc_node *node = ptr;

  olsrv2_tc_node_remove(node);
  olsrv2_routing_trigger_update();
}

/**
 * Remove a tc edge from the database
 * @param edge pointer to tc edge
 * @param cleanup true to remove the destination of the edge too
 *   if its not needed anymore
 * @return true if destination was removed, false otherwise
 */
static bool
_remove_edge(struct olsrv2_tc_edge *edge, bool cleanup) {
  bool removed_node = false;

  if (edge->virtual) {
    /* nothing to do */
    return false;
  }

  /* fire event */
  oonf_class_event(&_tc_edge_class, edge, OONF_OBJECT_REMOVED);

  if (!edge->inverse->virtual) {
    /* make this edge virtual */
    edge->virtual = true;

    return false;
  }

  /* unhook edge from both sides */
  avl_remove(&edge->src->_edges, &edge->_node);
  avl_remove(&edge->dst->_edges, &edge->inverse->_node);

  if (edge->dst->_edges.count == 0 && cleanup
      && !oonf_timer_is_active(&edge->dst->_validity_time)) {
    /*
     * node is already virtual and has no
     * incoming links anymore.
     */

    olsrv2_tc_node_remove(edge->dst);
    removed_node = true;
  }

  oonf_class_free(&_tc_edge_class, edge->inverse);
  oonf_class_free(&_tc_edge_class, edge);

  return removed_node;
}
