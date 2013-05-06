
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
#include "common/list.h"
#include "common/netaddr.h"
#include "config/cfg_schema.h"
#include "core/olsr_class.h"
#include "core/olsr_logging.h"
#include "core/olsr_timer.h"
#include "rfc5444/rfc5444.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_domain.h"

#include "olsrv2_originator.h"
#include "olsrv2/olsrv2_tc.h"
#include "olsrv2/olsrv2_routing.h"
#include "olsrv2/olsrv2.h"

static struct olsrv2_routing_entry *_add_entry(
    struct nhdp_domain *, struct netaddr *prefix);
static void _remove_entry(struct olsrv2_routing_entry *);
static void _insert_into_working_tree(struct olsrv2_tc_target *target,
    struct nhdp_neighbor *neigh, uint32_t linkcost,
    uint32_t pathcost, uint8_t distance, bool single_hop);
static void _prepare_routes(struct nhdp_domain *);
static void _handle_working_queue(struct nhdp_domain *);
static void _handle_nhdp_routes(struct nhdp_domain *);
static void _process_dijkstra_result(struct nhdp_domain *);
static void _cb_trigger_dijkstra(void *);
static void _cb_nhdp_update(struct nhdp_neighbor *);
static void _cb_route_finished(struct os_route *route, int error);
static void _cb_cfg_domain_changed(void);

/* configuration for domain specific routing parameters */
static struct cfg_schema_entry _rt_domain_entries[] = {
  CFG_MAP_BOOL(olsrv2_routing_domain, use_srcip_in_routes, "srcip_routes", "no",
      "Set the source IP of IPv4-routes to a fixed value."),
  CFG_MAP_INT_MINMAX(olsrv2_routing_domain, protocol, "protocol", "100",
      "Protocol number to be used in routing table", 1, 254),
  CFG_MAP_INT_MINMAX(olsrv2_routing_domain, table, "table", "254",
      "Routing table number for routes", 1, 254),
  CFG_MAP_INT_MINMAX(olsrv2_routing_domain, distance, "distance", "2",
      "Metric Distance to be used in routing table", 1, 255),
};

static struct cfg_schema_section _rt_domain_section = {
  .type = CFG_NHDP_DOMAIN_SECTION,
  .mode = CFG_SSMODE_NAMED_WITH_DEFAULT,
  .def_name = CFG_NHDP_DEFAULT_DOMAIN,
  .cb_delta_handler = _cb_cfg_domain_changed,
  .entries = _rt_domain_entries,
  .entry_count = ARRAYSIZE(_rt_domain_entries),
};

static struct olsrv2_routing_domain _domain_parameter[NHDP_MAXIMUM_DOMAINS];

/* memory class for routing entries */
static struct olsr_class _rtset_entry = {
  .name = "Olsrv2 Routing Set Entry",
  .size = sizeof(struct olsrv2_routing_entry),
};

/* rate limitation for dijkstra algorithm */
static struct olsr_timer_info _dijkstra_timer_info = {
  .name = "Dijkstra timer",
  .callback = _cb_trigger_dijkstra,
};

static struct olsr_timer_entry _rate_limit_timer = {
  .info = &_dijkstra_timer_info
};

/* callback for NHDP domain events */
static struct nhdp_domain_listener _nhdp_listener = {
  .update = _cb_nhdp_update,
};

static bool _trigger_dijkstra = false;

/* global datastructure for routing */
struct avl_tree olsrv2_routing_tree[NHDP_MAXIMUM_DOMAINS];
static struct avl_tree _dijkstra_working_tree;
static struct list_entity _kernel_queue;

static enum log_source LOG_OLSRV2_ROUTING = LOG_MAIN;
static bool _initiate_shutdown = false;

void
olsrv2_routing_init(void) {
  int i;

  LOG_OLSRV2_ROUTING = olsr_log_register_source("olsrv2_routing");

  olsr_class_add(&_rtset_entry);
  olsr_timer_add(&_dijkstra_timer_info);

  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    avl_init(&olsrv2_routing_tree[i], avl_comp_netaddr, false);
  }
  avl_init(&_dijkstra_working_tree, avl_comp_uint32, true);
  list_init_head(&_kernel_queue);

  nhdp_domain_listener_add(&_nhdp_listener);
  cfg_schema_add_section(olsr_cfg_get_schema(), &_rt_domain_section);
}

void
olsrv2_routing_initiate_shutdown(void) {
  struct olsrv2_routing_entry *entry, *e_it;
  int i;

  _initiate_shutdown = true;

  /* remove all routes */
  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    avl_for_each_element_safe(&olsrv2_routing_tree[i], entry, _node, e_it) {
      if (entry->set) {
        entry->set = false;
        os_routing_set(&entry->route, false, false);
      }
    }
  }
}

void
olsrv2_routing_cleanup(void) {
  struct olsrv2_routing_entry *entry, *e_it;
  int i;

  nhdp_domain_listener_remove(&_nhdp_listener);

  olsr_timer_stop(&_rate_limit_timer);

  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    avl_for_each_element_safe(&olsrv2_routing_tree[i], entry, _node, e_it) {
      /* make sure route processing has stopped */
      entry->route.cb_finished = NULL;
      os_routing_interrupt(&entry->route);

      /* remove entry from database */
      _remove_entry(entry);
    }
  }
  olsr_timer_remove(&_dijkstra_timer_info);
  olsr_class_remove(&_rtset_entry);

  cfg_schema_remove_section(olsr_cfg_get_schema(), &_rt_domain_section);
}

void
olsrv2_routing_trigger_update(void) {
  if (olsr_timer_is_active(&_rate_limit_timer)) {
    /* we are in the delay interval between two dijkstras */
    _trigger_dijkstra = true;
  }
  else {
    /* trigger as soon as we hit the next time slice */
    olsr_timer_set(&_rate_limit_timer, 1);
  }
}

void
olsrv2_routing_force_update(bool skip_wait) {
  struct olsrv2_routing_entry *rtentry, *rt_it;
  struct nhdp_domain *domain;
  struct os_route_str rbuf;

  if (_initiate_shutdown) {
    /* no dijkstra anymore when in shutdown */
    return;
  }

  /* handle dijkstra rate limitation timer */
  if (olsr_timer_is_active(&_rate_limit_timer)) {
    if (!skip_wait) {
      /* trigger dijkstra later */
      _trigger_dijkstra = true;

      OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Delay Dijkstra");
      return;
    }
    olsr_timer_stop(&_rate_limit_timer);
  }


  OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Run Dijkstra");

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    /* initialize dijkstra specific fields */
    _prepare_routes(domain);

    /* run dijkstra */
    while (!avl_is_empty(&_dijkstra_working_tree)) {
      _handle_working_queue(domain);
    }

    /* check if direct one-hop routes are quicker */
    _handle_nhdp_routes(domain);

    /* update kernel routes */
    _process_dijkstra_result(domain);
  }

  list_for_each_element_safe(&_kernel_queue, rtentry, _working_node, rt_it) {
    /* remove from routing queue */
    list_remove(&rtentry->_working_node);

    if (rtentry->set) {
      /* add to kernel */
      if (os_routing_set(&rtentry->route, true, true)) {
        OLSR_WARN(LOG_OLSRV2_ROUTING, "Could not set route %s",
            os_routing_to_string(&rbuf, &rtentry->route));
      }
    }
    else  {
      /* remove from kernel */
      if (os_routing_set(&rtentry->route, false, false)) {
        OLSR_WARN(LOG_OLSRV2_ROUTING, "Could not remove route %s",
            os_routing_to_string(&rbuf, &rtentry->route));
      }
    }
  }

  /* make sure dijkstra is not called too often */
  olsr_timer_set(&_rate_limit_timer, 250);
}

void
olsrv2_routing_dijkstra_node_init(struct olsrv2_dijkstra_node *dijkstra) {
  dijkstra->_node.key = &dijkstra->path_cost;
}

static struct olsrv2_routing_entry *
_add_entry(struct nhdp_domain *domain, struct netaddr *prefix) {
  struct olsrv2_routing_entry *rtentry;

  rtentry = avl_find_element(
      &olsrv2_routing_tree[domain->index], prefix, rtentry, _node);
  if (rtentry) {
    return rtentry;
  }

  rtentry = olsr_class_malloc(&_rtset_entry);
  if (rtentry == NULL) {
    return NULL;
  }

  /* set key */
  memcpy(&rtentry->route.dst, prefix, sizeof(struct netaddr));
  rtentry->_node.key = &rtentry->route.dst;

  /* set domain */
  rtentry->domain = domain;

  /* initialize path costs and os-route callback */
  rtentry->cost = RFC5444_METRIC_INFINITE_PATH;
  rtentry->route.cb_finished = _cb_route_finished;
  rtentry->route.family = netaddr_get_address_family(prefix);

  avl_insert(&olsrv2_routing_tree[domain->index], &rtentry->_node);
  return rtentry;
}

static void
_remove_entry(struct olsrv2_routing_entry *entry) {
  /* remove entry from database */
  avl_remove(&olsrv2_routing_tree[entry->domain->index], &entry->_node);
  olsr_class_free(&_rtset_entry, entry);
}

static void
_insert_into_working_tree(struct olsrv2_tc_target *target,
    struct nhdp_neighbor *neigh, uint32_t linkcost,
    uint32_t pathcost, uint8_t distance, bool single_hop) {
  struct olsrv2_dijkstra_node *node;
#if OONF_LOGGING_LEVEL >= OONF_LOGGING_LEVEL_DEBUG
  struct netaddr_str buf;
#endif
  if (linkcost >= RFC5444_METRIC_INFINITE) {
    return;
  }

  node = &target->_dijkstra;
  if (node->first_hop != NULL) {
    /* already hooked into dijkstra ! */
    avl_remove(&_dijkstra_working_tree, &node->_node);
  }

  /* calculate new total pathcost */
  pathcost += linkcost;

  if (node->path_cost <= pathcost || node->local) {
    /* current target is better or it is ourselves */
    return;
  }

  OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Add dst %s with pastcost %u to dijstra tree",
      netaddr_to_string(&buf, &target->addr), pathcost);

  node->path_cost = pathcost;
  node->first_hop = neigh;
  node->distance = distance;
  node->single_hop = single_hop;

  avl_insert(&_dijkstra_working_tree, &node->_node);
}

static void
_update_routing_entry(struct olsrv2_routing_entry *rtentry,
    struct nhdp_domain *domain,
    struct nhdp_neighbor *first_hop,
    uint8_t distance, uint32_t pathcost,
    bool single_hop) {
  struct nhdp_neighbor_domaindata *neighdata;
#if OONF_LOGGING_LEVEL >= OONF_LOGGING_LEVEL_DEBUG
  struct netaddr_str buf;
#endif

  neighdata = nhdp_domain_get_neighbordata(domain, first_hop);
  OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Add dst %s with pastcost %u to working queue",
      netaddr_to_string(&buf, &rtentry->route.dst), pathcost);

  /* copy route parameters into data structure */
  rtentry->route.if_index = neighdata->best_link_ifindex;
  rtentry->cost = pathcost;
  rtentry->route.metric = distance;

  /* mark route as set */
  rtentry->set = true;

  /* copy gateway if necessary */
  if (single_hop) {
    netaddr_invalidate(&rtentry->route.gw);
  }
  else {
    memcpy(&rtentry->route.gw, &neighdata->best_link->if_addr,
        sizeof(struct netaddr));
  }
}

static void
_prepare_routes(struct nhdp_domain *domain) {
  struct olsrv2_routing_entry *rtentry;
  struct olsrv2_tc_endpoint *end;
  struct olsrv2_tc_node *node;
  struct nhdp_neighbor *neigh;

  /* prepare all existing routing entries and put them into the working queue */
  avl_for_each_element(&olsrv2_routing_tree[domain->index], rtentry, _node) {
    rtentry->set = false;
    rtentry->_old_if_index = rtentry->route.if_index;
    rtentry->_old_distance = rtentry->route.metric;
    memcpy(&rtentry->_old_next_hop, &rtentry->route.gw, sizeof(struct netaddr));
  }

  /* initialize private dijkstra data on nodes */
  avl_for_each_element(&olsrv2_tc_tree, node, _originator_node) {
    node->target._dijkstra.first_hop = NULL;
    node->target._dijkstra.path_cost = RFC5444_METRIC_INFINITE_PATH;
    node->target._dijkstra.local =
        olsrv2_originator_is_local(&node->target.addr);
  }

  /* initialize private dijkstra data on endpoints */
  avl_for_each_element(&olsrv2_tc_endpoint_tree, end, _node) {
    end->target._dijkstra.first_hop = NULL;
    end->target._dijkstra.path_cost = RFC5444_METRIC_INFINITE_PATH;
  }

  /* initialize Dijkstra working queue with one-hop neighbors */
  list_for_each_element(&nhdp_neigh_list, neigh, _global_node) {
    if (neigh->symmetric > 0
        && netaddr_get_address_family(&neigh->originator) != AF_UNSPEC
        && (node = olsrv2_tc_node_get(&neigh->originator)) != NULL) {
      /* found node for neighbor, add to worker list */
      _insert_into_working_tree(&node->target, neigh,
          nhdp_domain_get_neighbordata(domain, neigh)->metric.out,
          0, 0, true);
    }
  }
}

static void
_handle_working_queue(struct nhdp_domain *domain) {
  struct olsrv2_routing_entry *rtentry;
  struct olsrv2_tc_target *target;
  struct nhdp_neighbor *first_hop;

  struct olsrv2_tc_node *tc_node;
  struct olsrv2_tc_edge *tc_edge;
  struct olsrv2_tc_attachment *tc_attached;

#if OONF_LOGGING_LEVEL >= OONF_LOGGING_LEVEL_DEBUG
  struct netaddr_str buf;
#endif

  /* get tc target */
  target = avl_first_element(&_dijkstra_working_tree, target, _dijkstra._node);

  /* remove current node from working tree */
  OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Remove node %s from dijkstra tree",
      netaddr_to_string(&buf, &target->addr));
  avl_remove(&_dijkstra_working_tree, &target->_dijkstra._node);

  /* add routing entry */
  rtentry = _add_entry(domain, &target->addr);
  if (rtentry == NULL) {
    return;
  }

  /* fill routing entry with dijkstra result */
  _update_routing_entry(rtentry, domain,
      target->_dijkstra.first_hop,
      target->_dijkstra.distance,
      target->_dijkstra.path_cost,
      target->_dijkstra.single_hop);

  if (target->type == OLSRV2_NODE_TARGET) {
    /* get neighbor and its domain specific data */
    first_hop = target->_dijkstra.first_hop;

    /* calculate pointer of olsrv2_tc_node */
    tc_node = container_of(target, struct olsrv2_tc_node, target);

    /* iterate over edges */
    avl_for_each_element(&tc_node->_edges, tc_edge, _node) {
      if (tc_edge->cost[domain->index] < RFC5444_METRIC_INFINITE) {
        /* add new tc_node to working tree */
        _insert_into_working_tree(&tc_edge->dst->target, first_hop,
            tc_edge->cost[domain->index], target->_dijkstra.path_cost,
            0, false);
      }
    }

    /* iterate over attached networks and addresses */
    avl_for_each_element(&tc_node->_endpoints, tc_attached, _src_node) {
      if (tc_attached->cost[domain->index] < RFC5444_METRIC_INFINITE) {
        /* add attached network or address to working tree */
        _insert_into_working_tree(&tc_attached->dst->target, first_hop,
            tc_attached->cost[domain->index], target->_dijkstra.path_cost,
            tc_attached->distance[domain->index], false);
      }
    }
  }

  /* cleanup temporary dijkstra data */
  target->_dijkstra.first_hop = NULL;
}

static void
_handle_nhdp_routes(struct nhdp_domain *domain) {
  struct olsrv2_routing_entry *rtentry;
  struct nhdp_neighbor_domaindata *neigh_data;
  struct nhdp_neighbor *neigh;
  struct nhdp_naddr *naddr;
  struct nhdp_l2hop *l2hop;
  struct nhdp_link *lnk;
  uint32_t neighcost;
  uint32_t l2hop_pathcost;

  list_for_each_element(&nhdp_neigh_list, neigh, _global_node) {

    /* get linkcost to neighbor */
    neigh_data = nhdp_domain_get_neighbordata(domain, neigh);
    neighcost = neigh_data->metric.out;

    if (neighcost >= RFC5444_METRIC_INFINITE) {
      continue;
    }

    /* make sure all addresses of the neighbor are better than our direct link */
    avl_for_each_element(&neigh->_neigh_addresses, naddr, _neigh_node) {
      if (!netaddr_acl_check_accept(olsrv2_get_routable(), &naddr->neigh_addr)) {
        /* not a routable address, check the next one */
        continue;
      }

      rtentry = _add_entry(domain, &naddr->neigh_addr);
      if (rtentry == NULL || (rtentry->set && rtentry->cost <= neighcost)) {
        /*
         * error in rtentry creation or existing entry is better than
         * the new one, check next neighbor address
         */
        continue;
      }

      /* the direct link is better than the dijkstra calculation */
      _update_routing_entry(rtentry, domain, neigh, 0, neighcost, true);
    }

    list_for_each_element(&neigh->_links, lnk, _neigh_node) {
      avl_for_each_element(&lnk->_2hop, l2hop, _link_node) {
        /* get new pathcost to 2hop neighbor */
        l2hop_pathcost = nhdp_domain_get_l2hopdata(domain, l2hop)->metric.out;
        if (l2hop_pathcost >= RFC5444_METRIC_INFINITE) {
          continue;
        }

        if (!netaddr_acl_check_accept(olsrv2_get_routable(), &l2hop->twohop_addr)) {
          /* not a routable address, check the next one */
          continue;
        }

        l2hop_pathcost += neighcost;

        rtentry = _add_entry(domain, &l2hop->twohop_addr);
        if (rtentry == NULL || (rtentry->set && rtentry->cost <= l2hop_pathcost)) {
          /* next 2hop address */
          continue;
        }

        /* the 2-hop route is better than the dijkstra calculation */
        _update_routing_entry(rtentry, domain, neigh, 0, l2hop_pathcost, false);
      }
    }
  }
}

static void
_process_dijkstra_result(struct nhdp_domain *domain) {
  struct olsrv2_routing_entry *rtentry;
#if OONF_LOGGING_LEVEL >= OONF_LOGGING_LEVEL_INFO
  struct os_route_str rbuf;
  struct netaddr_str nbuf;
#endif

  avl_for_each_element(&olsrv2_routing_tree[domain->index], rtentry, _node) {
    /* initialize rest of route parameters */
    rtentry->route.table = _domain_parameter[rtentry->domain->index].table;
    rtentry->route.protocol = _domain_parameter[rtentry->domain->index].protocol;
    rtentry->route.metric = _domain_parameter[rtentry->domain->index].distance;

    // TODO: handle source ip

    if (rtentry->set) {
      if (rtentry->_old_if_index == rtentry->route.if_index
        && rtentry->_old_distance == rtentry->route.metric
        && netaddr_cmp(&rtentry->_old_next_hop, &rtentry->route.gw) == 0) {
        /* no change, ignore this entry */
        continue;
      }

      OLSR_INFO(LOG_OLSRV2_ROUTING,
          "Dijkstra result: set route %s (%u %u %s)",
          os_routing_to_string(&rbuf, &rtentry->route),
          rtentry->_old_if_index, rtentry->_old_distance,
          netaddr_to_string(&nbuf, &rtentry->_old_next_hop));

      if (netaddr_get_address_family(&rtentry->route.gw) == AF_UNSPEC) {
        /* insert/update single-hop routes early */
        list_add_head(&_kernel_queue, &rtentry->_working_node);
      }
      else {
        /* insert/update multi-hop routes late */
        list_add_tail(&_kernel_queue, &rtentry->_working_node);
      }
    }
    else if (!rtentry->set) {
      OLSR_INFO(LOG_OLSRV2_ROUTING,
          "Dijkstra result: remove route %s",
          os_routing_to_string(&rbuf, &rtentry->route));

      if (netaddr_get_address_family(&rtentry->route.gw) == AF_UNSPEC) {
        /* remove single-hop routes late */
        list_add_tail(&_kernel_queue, &rtentry->_working_node);
      }
      else {
        /* remove multi-hop routes early */
        list_add_head(&_kernel_queue, &rtentry->_working_node);
      }
    }
  }
}

static void
_cb_trigger_dijkstra(void *unused __attribute__((unused))) {
  if (_trigger_dijkstra) {
    _trigger_dijkstra = false;
    olsrv2_routing_force_update(false);
  }
}

static void
_cb_nhdp_update(struct nhdp_neighbor *neigh __attribute__((unused))) {
  olsrv2_routing_trigger_update();
}

static void
_cb_route_finished(struct os_route *route, int error) {
  struct olsrv2_routing_entry *rtentry;
  struct os_route_str rbuf;

  rtentry = container_of(route, struct olsrv2_routing_entry, route);

  if (error) {
    /* an error happened, try again later */
    OLSR_WARN(LOG_OLSRV2_ROUTING, "Error in route %s %s: %s (%d)",
        rtentry->set ? "setting" : "removal",
        os_routing_to_string(&rbuf, &rtentry->route),
        strerror(error), error);

    /* revert attempted change */
    if (rtentry->set) {
      _remove_entry(rtentry);
    }
    else {
      rtentry->set = true;
    }
    return;
  }
  if (rtentry->set) {
    /* route was set/updated successfully */
    OLSR_INFO(LOG_OLSRV2_ROUTING, "Successfully set route %s",
        os_routing_to_string(&rbuf, &rtentry->route));
  }
  else {
    OLSR_INFO(LOG_OLSRV2_ROUTING, "Successfully removed route %s",
        os_routing_to_string(&rbuf, &rtentry->route));
    _remove_entry(rtentry);
  }
}

static void
_cb_cfg_domain_changed(void) {
  struct nhdp_domain *domain;
  char *error = NULL;
  int ext;

  ext = strtol(_rt_domain_section.section_name, &error, 10);
  if (error != NULL && *error != 0) {
    /* illegal domain name */
    return;
  }

  if (ext < 0 || ext > 255) {
    /* name out of range */
    return;
  }

  domain = nhdp_domain_add(ext);
  if (domain == NULL) {
    return;
  }

  if (cfg_schema_tobin(&_domain_parameter[domain->index], _rt_domain_section.post,
      _rt_domain_entries, ARRAYSIZE(_rt_domain_entries))) {
    OLSR_WARN(LOG_NHDP, "Cannot convert NHDP routing domain parameters.");
    return;
  }
}
