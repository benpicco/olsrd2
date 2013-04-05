
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

#include "common/common_types.h"
#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/list.h"
#include "common/netaddr.h"

#include "rfc5444/rfc5444.h"

#include "core/olsr_logging.h"
#include "core/olsr_class.h"
#include "core/olsr_timer.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_hysteresis.h"
#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp_db.h"

/* Prototypes of local functions */
static void _link_status_now_symmetric(struct nhdp_link *lnk);
static void _link_status_not_symmetric_anymore(struct nhdp_link *lnk);
int _nhdp_db_link_calculate_status(struct nhdp_link *lnk);

static void _cb_link_vtime(void *);
static void _cb_link_vtime_v4(void *);
static void _cb_link_vtime_v6(void *);
static void _cb_link_heard(void *);
static void _cb_link_symtime(void *);
static void _cb_l2hop_vtime(void *);
static void _cb_naddr_vtime(void *);

/* memory and timer classes necessary for NHDP */
static struct olsr_class _neigh_info = {
  .name = NHDP_CLASS_NEIGHBOR,
  .size = sizeof(struct nhdp_neighbor),
};

static struct olsr_class _link_info = {
  .name = NHDP_CLASS_LINK,
  .size = sizeof(struct nhdp_link),
};

static struct olsr_class _laddr_info = {
  .name = NHDP_CLASS_LINK_ADDRESS,
  .size = sizeof(struct nhdp_laddr),
};

static struct olsr_class _l2hop_info = {
  .name = NHDP_CLASS_LINK_2HOP,
  .size = sizeof(struct nhdp_l2hop),
};

static struct olsr_class _naddr_info = {
  .name = NHDP_CLASS_NEIGHBOR_ADDRESS,
  .size = sizeof(struct nhdp_naddr),
};

static struct olsr_timer_info _link_vtime_info = {
  .name = "NHDP link vtime",
  .callback = _cb_link_vtime,
};

static struct olsr_timer_info _neigh_vtimev4_info = {
  .name = "NHDP link vtime v4",
  .callback = _cb_link_vtime_v4,
};

static struct olsr_timer_info _neigh_vtimev6_info = {
  .name = "NHDP link vtime v6",
  .callback = _cb_link_vtime_v6,
};

static struct olsr_timer_info _link_heard_info = {
  .name = "NHDP link heard-time",
  .callback = _cb_link_heard,
};

static struct olsr_timer_info _link_symtime_info = {
  .name = "NHDP link symtime",
  .callback = _cb_link_symtime,
};

static struct olsr_timer_info _naddr_vtime_info = {
  .name = "NHDP neighbor address vtime",
  .callback = _cb_naddr_vtime,
};

static struct olsr_timer_info _l2hop_vtime_info = {
  .name = "NHDP 2hop vtime",
  .callback = _cb_l2hop_vtime,
};

/* global tree of neighbor addresses */
struct avl_tree nhdp_naddr_tree;

/* list of neighbors */
struct list_entity nhdp_neigh_list;

/* tree of neighbors with originator addresses */
struct avl_tree nhdp_neigh_originator_tree;

/* list of links (to neighbors) */
struct list_entity nhdp_link_list;

/**
 * Initialize NHDP databases
 */
void
nhdp_db_init(void) {
  avl_init(&nhdp_naddr_tree, avl_comp_netaddr, false);
  list_init_head(&nhdp_neigh_list);
  avl_init(&nhdp_neigh_originator_tree, avl_comp_netaddr, false);
  list_init_head(&nhdp_link_list);

  olsr_class_add(&_neigh_info);
  olsr_class_add(&_naddr_info);
  olsr_class_add(&_link_info);
  olsr_class_add(&_laddr_info);
  olsr_class_add(&_l2hop_info);

  olsr_timer_add(&_naddr_vtime_info);
  olsr_timer_add(&_link_vtime_info);
  olsr_timer_add(&_neigh_vtimev4_info);
  olsr_timer_add(&_neigh_vtimev6_info);
  olsr_timer_add(&_link_heard_info);
  olsr_timer_add(&_link_symtime_info);
  olsr_timer_add(&_l2hop_vtime_info);
}

/**
 * Cleanup NHDP databases
 */
void
nhdp_db_cleanup(void) {
  struct nhdp_neighbor *neigh, *n_it;

  /* remove all neighbors */
  list_for_each_element_safe(&nhdp_neigh_list, neigh, _global_node, n_it) {
    nhdp_db_neighbor_remove(neigh);
  }

  /* cleanup all timers */
  olsr_timer_remove(&_l2hop_vtime_info);
  olsr_timer_remove(&_link_symtime_info);
  olsr_timer_remove(&_link_heard_info);
  olsr_timer_remove(&_neigh_vtimev6_info);
  olsr_timer_remove(&_neigh_vtimev4_info);
  olsr_timer_remove(&_link_vtime_info);
  olsr_timer_remove(&_naddr_vtime_info);

  /* cleanup all memory cookies */
  olsr_class_remove(&_l2hop_info);
  olsr_class_remove(&_laddr_info);
  olsr_class_remove(&_link_info);
  olsr_class_remove(&_naddr_info);
  olsr_class_remove(&_neigh_info);
}

/**
 * @return new NHDP neighbor without links and addresses,
 *  NULL if out of memory
 */
struct nhdp_neighbor *
nhdp_db_neighbor_add(void) {
  struct nhdp_neighbor *neigh;

  neigh = olsr_class_malloc(&_neigh_info);
  if (neigh == NULL) {
    return NULL;
  }

  OLSR_DEBUG(LOG_NHDP, "New Neighbor: 0x%0zx", (size_t)neigh);

  /* initialize timers */
  neigh->_vtime_v4.cb_context = neigh;
  neigh->_vtime_v4.info = &_neigh_vtimev4_info;

  neigh->_vtime_v6.cb_context = neigh;
  neigh->_vtime_v6.info = &_neigh_vtimev6_info;

  /* initialize trees and lists */
  avl_init(&neigh->_neigh_addresses, avl_comp_netaddr, false);
  avl_init(&neigh->_link_addresses, avl_comp_netaddr, true);
  list_init_head(&neigh->_links);

  /* hook into global neighbor list */
  list_add_tail(&nhdp_neigh_list, &neigh->_global_node);

  /* initialize originator node */
  neigh->_originator_node.key = &neigh->originator;

  /* initialize domain data */
  nhdp_domain_init_neighbor(neigh);

  /* trigger event */
  olsr_class_event(&_neigh_info, neigh, OLSR_OBJECT_ADDED);
  return neigh;
}

/**
 * Remove NHDP neighbor including links and addresses from db
 * @param neigh nhdp neighbor to be removed
 */
void
nhdp_db_neighbor_remove(struct nhdp_neighbor *neigh) {
  struct nhdp_naddr *naddr, *na_it;
  struct nhdp_link *lnk, *l_it;

  OLSR_DEBUG(LOG_NHDP, "Remove Neighbor: 0x%0zx", (size_t)neigh);

  /* trigger event */
  olsr_class_event(&_neigh_info, neigh, OLSR_OBJECT_REMOVED);

  /* stop timers */
  olsr_timer_stop(&neigh->_vtime_v4);
  olsr_timer_stop(&neigh->_vtime_v6);

  /* remove all links */
  list_for_each_element_safe(&neigh->_links, lnk, _neigh_node, l_it) {
    nhdp_db_link_remove(lnk);
  }

  /* remove all neighbor addresses */
  avl_for_each_element_safe(&neigh->_neigh_addresses, naddr, _neigh_node, na_it) {
    nhdp_db_neighbor_addr_remove(naddr);
  }

  /* remove from originator tree if necessary */
  if (netaddr_get_address_family(&neigh->originator) != AF_UNSPEC) {
    avl_remove(&nhdp_neigh_originator_tree, &neigh->_originator_node);
  }

  /* remove from global list and free memory */
  list_remove(&neigh->_global_node);
  olsr_class_free(&_neigh_info, neigh);
}

/**
 * Join the links and addresses of two NHDP neighbors
 * @param dst target neighbor which gets all the links and addresses
 * @param src source neighbor which will be removed afterwards
 */
void
nhdp_db_neighbor_join(struct nhdp_neighbor *dst, struct nhdp_neighbor *src) {
  struct nhdp_naddr *naddr, *na_it;
  struct nhdp_link *lnk, *l_it;
  struct nhdp_laddr *laddr, *la_it;

  if (dst == src) {
    return;
  }

  /* fix symmetric link count */
  dst->symmetric += src->symmetric;

  /* move links */
  list_for_each_element_safe(&src->_links, lnk, _neigh_node, l_it) {
    /* more addresses to new neighbor */
    avl_for_each_element_safe(&lnk->_addresses, laddr, _neigh_node, la_it) {
      avl_remove(&src->_link_addresses, &laddr->_neigh_node);
      avl_insert(&dst->_link_addresses, &laddr->_neigh_node);
    }

    /* move link to new neighbor */
    list_remove(&lnk->_neigh_node);
    list_add_tail(&dst->_links, &lnk->_neigh_node);

    lnk->neigh = dst;
  }

  /* move neighbor addresses to target */
  avl_for_each_element_safe(&src->_neigh_addresses, naddr, _neigh_node, na_it) {
    /* move address to new neighbor */
    avl_remove(&src->_neigh_addresses, &naddr->_neigh_node);
    avl_insert(&dst->_neigh_addresses, &naddr->_neigh_node);
    naddr->neigh = dst;
  }

  nhdp_db_neighbor_remove(src);
}

/**
 * Adds an address to a nhdp neighbor
 * @param neigh nhdp neighbor
 * @param addr network address
 * @return pointer to neighbor address, NULL if out of memory
 */
struct nhdp_naddr *
nhdp_db_neighbor_addr_add(struct nhdp_neighbor *neigh,
    const struct netaddr *addr) {
  struct nhdp_naddr *naddr;

  naddr = olsr_class_malloc(&_naddr_info);
  if (naddr == NULL) {
    return NULL;
  }

  /* initialize key */
  memcpy(&naddr->neigh_addr, addr, sizeof(naddr->neigh_addr));
  naddr->_neigh_node.key = &naddr->neigh_addr;
  naddr->_global_node.key = &naddr->neigh_addr;

  /* initialize backward link */
  naddr->neigh = neigh;

  /* initialize timer for lost addresses */
  naddr->_lost_vtime.info = &_naddr_vtime_info;
  naddr->_lost_vtime.cb_context = naddr;

  /* add to trees */
  avl_insert(&nhdp_naddr_tree, &naddr->_global_node);
  avl_insert(&neigh->_neigh_addresses, &naddr->_neigh_node);

  /* trigger event */
  olsr_class_event(&_naddr_info, naddr, OLSR_OBJECT_ADDED);

  return naddr;
}

/**
 * Removes a nhdp neighbor address from its neighbor
 * @param naddr neighbor address
 */
void
nhdp_db_neighbor_addr_remove(struct nhdp_naddr *naddr) {
  /* trigger event */
  olsr_class_event(&_naddr_info, naddr, OLSR_OBJECT_REMOVED);

  /* remove from trees */
  avl_remove(&nhdp_naddr_tree, &naddr->_global_node);
  avl_remove(&naddr->neigh->_neigh_addresses, &naddr->_neigh_node);

  /* stop timer */
  olsr_timer_stop(&naddr->_lost_vtime);

  /* free memory */
  olsr_class_free(&_naddr_info, naddr);
}

/**
 * Moves a nhdp neighbor address to a different neighbor
 * @param neigh
 * @param naddr
 */
void
nhdp_db_neighbor_addr_move(struct nhdp_neighbor *neigh, struct nhdp_naddr *naddr) {
  /* remove from old neighbor */
  avl_remove(&naddr->neigh->_neigh_addresses, &naddr->_neigh_node);

  /* add to new neighbor */
  avl_insert(&neigh->_neigh_addresses, &naddr->_neigh_node);

  /* set new backlink */
  naddr->neigh = neigh;
}

/**
 * Sets a new originator address for an NHDP neighbor
 * @param nhdp neighbor
 * @param originator originator address, might be type AF_UNSPEC
 */
void
nhdp_db_neighbor_set_originator(struct nhdp_neighbor *neigh, const struct netaddr *originator) {
  struct nhdp_neighbor *neigh2;
  struct nhdp_link *lnk;

  if (memcmp(&neigh->originator, originator, sizeof(*originator)) == 0) {
    /* same originator, nothing to do */
    return;
  }

  if (netaddr_get_address_family(&neigh->originator) != AF_UNSPEC) {
    /* different originator, remove from tree */
    avl_remove(&nhdp_neigh_originator_tree, &neigh->_originator_node);

    list_for_each_element(&neigh->_links, lnk, _neigh_node) {
      /* remove links from interface specific tree */
      avl_remove(&lnk->local_if->_link_originators, &lnk->_originator_node);
    }
  }

  neigh2 = nhdp_db_neighbor_get_by_originator(originator);
  if (neigh2) {
    /* different neighbor has this originator, invalidate it */
    avl_remove(&nhdp_neigh_originator_tree, &neigh2->_originator_node);

    list_for_each_element(&neigh2->_links, lnk, _neigh_node) {
      /* remove links from interface specific tree */
      avl_remove(&lnk->local_if->_link_originators, &lnk->_originator_node);
    }

    netaddr_invalidate(&neigh2->originator);
  }

  /* copy originator address into neighbor */
  memcpy(&neigh->originator, originator, sizeof(*originator));

  if (netaddr_get_address_family(originator) != AF_UNSPEC) {
    /* add to tree if new originator is valid */
    avl_insert(&nhdp_neigh_originator_tree, &neigh->_originator_node);

    list_for_each_element(&neigh->_links, lnk, _neigh_node) {
      /* remove links from interface specific tree */
      avl_insert(&lnk->local_if->_link_originators, &lnk->_originator_node);
    }
  }
}

/**
 * Connect two neighbors as representations of the same node,
 * @param n_ipv4 ipv4 neighbor
 * @param n_ipv6 ipv6 neighbor
 */
void
nhdp_db_neighbor_connect_dualstack(
    struct nhdp_neighbor *n_ipv4, struct nhdp_neighbor *n_ipv6) {

  if (n_ipv4->dualstack_partner != n_ipv6) {
    nhdp_db_neigbor_disconnect_dualstack(n_ipv4);
    n_ipv4->dualstack_partner = n_ipv6;
  }
  n_ipv4->dualstack_is_ipv4 = true;

  if (n_ipv6->dualstack_partner != n_ipv4) {
    nhdp_db_neigbor_disconnect_dualstack(n_ipv6);
    n_ipv6->dualstack_partner = n_ipv4;
  }
  n_ipv6->dualstack_is_ipv4 = false;
}

/**
 * Disconnects the pointers of a dualstack pair of neighbors
 * @param neigh one of the connected neighbors
 */
void
nhdp_db_neigbor_disconnect_dualstack(struct nhdp_neighbor *neigh) {
  if (neigh->dualstack_partner) {
    neigh->dualstack_partner->dualstack_partner = NULL;
    neigh->dualstack_partner = NULL;
  }
}

/**
 * Insert a new link into a nhdp neighbors database
 * @param neigh neighbor which will get the new link
 * @param local_if local interface through which the link was heard
 * @return new nhdp link, NULL if out of memory
 */
struct nhdp_link *
nhdp_db_link_add(struct nhdp_neighbor *neigh, struct nhdp_interface *local_if) {
  struct nhdp_link *lnk;

  lnk = olsr_class_malloc(&_link_info);
  if (lnk == NULL) {
    return NULL;
  }

  /* hook into interface */
  nhdp_interface_add_link(local_if, lnk);

  /* hook into neighbor */
  list_add_tail(&neigh->_links, &lnk->_neigh_node);
  lnk->neigh = neigh;

  /* hook into global list */
  list_add_tail(&nhdp_link_list, &lnk->_global_node);

  /* init local trees */
  avl_init(&lnk->_addresses, avl_comp_netaddr, false);
  avl_init(&lnk->_2hop, avl_comp_netaddr, false);

  /* init timers */
  lnk->sym_time.info = &_link_symtime_info;
  lnk->sym_time.cb_context = lnk;
  lnk->heard_time.info = &_link_heard_info;
  lnk->heard_time.cb_context = lnk;
  lnk->vtime.info = &_link_vtime_info;
  lnk->vtime.cb_context = lnk;

  /* add to originator tree if set */
  lnk->_originator_node.key = &neigh->originator;
  if (netaddr_get_address_family(&neigh->originator) != AF_UNSPEC) {
    avl_insert(&local_if->_link_originators, &lnk->_originator_node);
  }

  /* initialize link domain data */
  nhdp_domain_init_link(lnk);

  /* trigger event */
  olsr_class_event(&_link_info, lnk, OLSR_OBJECT_ADDED);

  return lnk;
}

/**
 * Remove a nhdp link from database
 * @param lnk nhdp link to be removed
 */
void
nhdp_db_link_remove(struct nhdp_link *lnk) {
  struct nhdp_laddr *laddr, *la_it;
  struct nhdp_l2hop *twohop, *th_it;

  /* trigger event */
  olsr_class_event(&_link_info, lnk, OLSR_OBJECT_REMOVED);

  if (lnk->status == NHDP_LINK_SYMMETRIC) {
    _link_status_not_symmetric_anymore(lnk);
  }

  /* stop link timers */
  olsr_timer_stop(&lnk->sym_time);
  olsr_timer_stop(&lnk->heard_time);
  olsr_timer_stop(&lnk->vtime);

  if (netaddr_get_address_family(&lnk->neigh->originator) != AF_UNSPEC) {
    avl_remove(&lnk->local_if->_link_originators, &lnk->_originator_node);
  }

  /* detach all addresses */
  avl_for_each_element_safe(&lnk->_addresses, laddr, _link_node, la_it) {
    nhdp_db_link_addr_remove(laddr);
  }

  /* remove all 2hop addresses */
  avl_for_each_element_safe(&lnk->_2hop, twohop, _link_node, th_it) {
    nhdp_db_link_2hop_remove(twohop);
  }

  /* unlink */
  nhdp_interface_remove_link(lnk);
  list_remove(&lnk->_neigh_node);

  /* remove from global list */
  list_remove(&lnk->_global_node);

  /* free memory */
  olsr_class_free(&_link_info, lnk);
}

/**
 * Add a network address as a link address to a nhdp link
 * @param lnk nhpd link
 * @param addr network address
 * @return nhdp link address, NULL if out of memory
 */
struct nhdp_laddr *
nhdp_db_link_addr_add(struct nhdp_link *lnk, const struct netaddr *addr) {
  struct nhdp_laddr *laddr;

  laddr = olsr_class_malloc(&_laddr_info);
  if (laddr == NULL) {
    return NULL;
  }

  /* initialize key */
  memcpy(&laddr->link_addr, addr, sizeof(laddr->link_addr));
  laddr->_link_node.key = &laddr->link_addr;
  laddr->_neigh_node.key = &laddr->link_addr;
  laddr->_if_node.key = &laddr->link_addr;

  /* initialize back link */
  laddr->link = lnk;

  /* add to trees */
  avl_insert(&lnk->_addresses, &laddr->_link_node);
  avl_insert(&lnk->neigh->_link_addresses, &laddr->_neigh_node);
  nhdp_interface_add_laddr(laddr);

  /* trigger event */
  olsr_class_event(&_laddr_info, laddr, OLSR_OBJECT_ADDED);

  return laddr;
}

/**
 * Removes a nhdp link address from its link
 * @param laddr nhdp link address
 */
void
nhdp_db_link_addr_remove(struct nhdp_laddr *laddr) {
  /* trigger event */
  olsr_class_event(&_laddr_info, laddr, OLSR_OBJECT_REMOVED);

  /* remove from trees */
  nhdp_interface_remove_laddr(laddr);
  avl_remove(&laddr->link->_addresses, &laddr->_link_node);
  avl_remove(&laddr->link->neigh->_link_addresses, &laddr->_neigh_node);

  /* free memory */
  olsr_class_free(&_laddr_info, laddr);
}

/**
 * Moves a nhdp link address to a different link
 * @param lnk
 * @param laddr
 */
void
nhdp_db_link_addr_move(struct nhdp_link *lnk, struct nhdp_laddr *laddr) {
  /* remove from old link */
  avl_remove(&laddr->link->_addresses, &laddr->_link_node);

  /* add to new neighbor */
  avl_insert(&lnk->_addresses, &laddr->_link_node);

  if (laddr->link->neigh != lnk->neigh) {
    /* remove from old neighbor */
    avl_remove(&laddr->link->neigh->_link_addresses, &laddr->_neigh_node);

    /* add to new neighbor */
    avl_insert(&lnk->neigh->_link_addresses, &laddr->_neigh_node);
  }
  /* set new backlink */
  laddr->link = lnk;
}

/**
 * Adds a network address as a 2-hop neighbor to a nhdp link
 * @param lnk nhdp link
 * @param addr network address
 * @return nhdp link two-hop neighbor
 */
struct nhdp_l2hop *
nhdp_db_link_2hop_add(struct nhdp_link *lnk, const struct netaddr *addr) {
  struct nhdp_l2hop *l2hop;

  l2hop = olsr_class_malloc(&_l2hop_info);
  if (l2hop == NULL) {
    return NULL;
  }

  /* initialize key */
  memcpy(&l2hop->twohop_addr, addr, sizeof(l2hop->twohop_addr));
  l2hop->_link_node.key = &l2hop->twohop_addr;

  /* initialize back link */
  l2hop->link = lnk;

  /* initialize validity timer */
  l2hop->_vtime.info = &_l2hop_vtime_info;
  l2hop->_vtime.cb_context = l2hop;

  /* add to trees */
  avl_insert(&lnk->_2hop, &l2hop->_link_node);

  /* initialize metrics */
  nhdp_domain_init_l2hop(l2hop);

  /* trigger event */
  olsr_class_event(&_l2hop_info, l2hop, OLSR_OBJECT_ADDED);

  return l2hop;
}

/**
 * Removes a two-hop address from a nhdp link
 * @param l2hop nhdp two-hop link address
 */
void
nhdp_db_link_2hop_remove(struct nhdp_l2hop *l2hop) {
  /* trigger event */
  olsr_class_event(&_l2hop_info, l2hop, OLSR_OBJECT_REMOVED);

  /* remove from tree */
  avl_remove(&l2hop->link->_2hop, &l2hop->_link_node);

  /* stop validity timer */
  olsr_timer_stop(&l2hop->_vtime);

  /* free memory */
  olsr_class_free(&_l2hop_info, l2hop);
}

/**
 * Connect two links as representations of the same node,
 * @param l_ipv4 ipv4 link
 * @param l_ipv6 ipv6 link
 */
void
nhdp_db_link_connect_dualstack(
    struct nhdp_link *l_ipv4, struct nhdp_link *l_ipv6) {

  if (l_ipv4->dualstack_partner != l_ipv6) {
    nhdp_db_link_disconnect_dualstack(l_ipv4);
    l_ipv4->dualstack_partner = l_ipv6;
  }
  l_ipv4->dualstack_is_ipv4 = true;

  if (l_ipv6->dualstack_partner != l_ipv4) {
    nhdp_db_link_disconnect_dualstack(l_ipv6);
    l_ipv6->dualstack_partner = l_ipv4;
  }
  l_ipv6->dualstack_is_ipv4 = false;
}

/**
 * Disconnects the pointers of a dualstack pair of neighbors
 * @param neigh one of the connected neighbors
 */
void
nhdp_db_link_disconnect_dualstack(struct nhdp_link *lnk) {
  if (lnk->dualstack_partner) {
    lnk->dualstack_partner->dualstack_partner = NULL;
    lnk->dualstack_partner = NULL;
  }
}

/**
 * Recalculate the status of a nhdp link and update database
 * if link changed between symmetric and non-symmetric
 * @param lnk nhdp link with (potential) new status
 */
void
nhdp_db_link_update_status(struct nhdp_link *lnk) {
  bool was_symmetric;

  was_symmetric = lnk->status == NHDP_LINK_SYMMETRIC;

  /* update link status */
  lnk->status = _nhdp_db_link_calculate_status(lnk);

  /* handle database changes */
  if (was_symmetric && lnk->status != NHDP_LINK_SYMMETRIC) {
    _link_status_not_symmetric_anymore(lnk);
  }
  if (!was_symmetric && lnk->status == NHDP_LINK_SYMMETRIC) {
    _link_status_now_symmetric(lnk);
  }

  olsr_class_event(&_link_info, lnk, OLSR_OBJECT_CHANGED);
}

/**
 * Helper function to calculate NHDP link status
 * @param lnk nhdp link
 * @return link status
 */
int
_nhdp_db_link_calculate_status(struct nhdp_link *lnk) {
  if (nhdp_hysteresis_is_pending(lnk))
    return NHDP_LINK_PENDING;
  if (nhdp_hysteresis_is_lost(lnk))
    return RFC5444_LINKSTATUS_LOST;
  if (olsr_timer_is_active(&lnk->sym_time))
    return RFC5444_LINKSTATUS_SYMMETRIC;
  if (olsr_timer_is_active(&lnk->heard_time))
    return RFC5444_LINKSTATUS_HEARD;
  return RFC5444_LINKSTATUS_LOST;
}

/**
 * Helper function that handles the case of a link becoming symmetric
 * @param lnk nhdp link
 */
static void
_link_status_now_symmetric(struct nhdp_link *lnk) {
  struct nhdp_naddr *naddr;

  lnk->neigh->symmetric++;

  if (lnk->neigh->symmetric == 1) {
    avl_for_each_element(&lnk->neigh->_neigh_addresses, naddr, _neigh_node) {
      nhdp_db_neighbor_addr_not_lost(naddr);
    }
  }
}

/**
 * Helper function that handles the case of a link becoming asymmetric
 * @param lnk nhdp link
 */
static void
_link_status_not_symmetric_anymore(struct nhdp_link *lnk) {
  struct nhdp_l2hop *twohop, *twohop_it;
  struct nhdp_naddr *naddr, *na_it;

  /* remove all 2hop neighbors */
  avl_for_each_element_safe(&lnk->_2hop, twohop, _link_node, twohop_it) {
    nhdp_db_link_2hop_remove(twohop);
  }

  lnk->neigh->symmetric--;
  if (lnk->neigh->symmetric == 0) {
    /* mark all neighbor addresses as lost */
    avl_for_each_element_safe(&lnk->neigh->_neigh_addresses, naddr, _neigh_node, na_it) {
      nhdp_db_neighbor_addr_set_lost(naddr, lnk->local_if->n_hold_time);
    }
  }
}

/**
 * Callback triggered when link validity timer fires
 * @param ptr nhdp link
 */
static void
_cb_link_vtime(void *ptr) {
  struct nhdp_link *lnk = ptr;
  struct nhdp_neighbor *neigh;

  OLSR_DEBUG(LOG_NHDP, "Link vtime fired: 0x%0zx", (size_t)ptr);

  neigh = lnk->neigh;

  if (lnk->status == NHDP_LINK_SYMMETRIC) {
    _link_status_not_symmetric_anymore(lnk);
  }

  /* remove link from database */
  nhdp_db_link_remove(lnk);

  /* check if neighbor still has links */
  if (list_is_empty(&neigh->_links)) {
    nhdp_db_neighbor_remove(neigh);
  }
}

/**
 * Clean up a all elements of a neighbor that fit a certain address family
 * @param neigh nhdp neighbor
 * @param af_type address family
 */
static void
_cleanup_neighbor(struct nhdp_neighbor *neigh, int af_type) {
  struct nhdp_naddr *naddr, *na_it;
  struct nhdp_link *lnk, *l_it;
  struct nhdp_laddr *laddr, *la_it;
  struct nhdp_l2hop *l2hop, *l2_it;

  /* remove all IPv4 addresses from neighbor */
  avl_for_each_element_safe(&neigh->_neigh_addresses, naddr, _neigh_node, na_it) {
    if (netaddr_get_address_family(&naddr->neigh_addr) == af_type) {
      nhdp_db_neighbor_addr_remove(naddr);
    }
  }

  if (neigh->_neigh_addresses.count == 0) {
    /* no address left, remove link */
    nhdp_db_neighbor_remove(neigh);
    return;
  }

  list_for_each_element_safe(&neigh->_links, lnk, _neigh_node, l_it) {
    avl_for_each_element_safe(&lnk->_addresses, laddr, _link_node, la_it) {
      if (netaddr_get_address_family(&laddr->link_addr) == af_type) {
        nhdp_db_link_addr_remove(laddr);
      }
    }

    if (lnk->_addresses.count == 0) {
      nhdp_db_link_remove(lnk);
      return;
    }

    avl_for_each_element_safe(&lnk->_2hop, l2hop, _link_node, l2_it) {
      if (netaddr_get_address_family(&l2hop->twohop_addr) == af_type) {
        nhdp_db_link_2hop_remove(l2hop);
      }
    }
  }
}

/**
 * Callback triggered when neighbor validity timer for ipv6 addresses fires
 * @param ptr nhdp link
 */
static void
_cb_link_vtime_v4(void *ptr) {
  struct nhdp_neighbor *neigh = ptr;

  OLSR_DEBUG(LOG_NHDP, "Neighbor vtime_v4 fired: 0x%0zx", (size_t)ptr);
  _cleanup_neighbor(neigh, AF_INET);
}

/**
 * Callback triggered when neighbor validity timer for ipv6 addresses fires
 * @param ptr nhdp link
 */
static void
_cb_link_vtime_v6(void *ptr) {
  struct nhdp_neighbor *neigh = ptr;

  OLSR_DEBUG(LOG_NHDP, "Neighbor vtime_v6 fired: 0x%0zx", (size_t)ptr);

  _cleanup_neighbor(neigh, AF_INET6);
}

/**
 * Callback triggered when link heard timer fires
 * @param ptr nhdp link
 */
static void
_cb_link_heard(void *ptr) {
  OLSR_DEBUG(LOG_NHDP, "Link heard fired: 0x%0zx", (size_t)ptr);
  nhdp_db_link_update_status(ptr);
}

/**
 * Callback triggered when link symmetric timer fires
 * @param ptr nhdp link
 */
static void
_cb_link_symtime(void *ptr __attribute__((unused))) {
  OLSR_DEBUG(LOG_NHDP, "Link Symtime fired: 0x%0zx", (size_t)ptr);
  nhdp_db_link_update_status(ptr);
}

/**
 * Callback triggered when nhdp address validity timer fires
 * @param ptr nhdp address
 */
static void
_cb_naddr_vtime(void *ptr) {
  struct nhdp_naddr *naddr = ptr;

  OLSR_DEBUG(LOG_NHDP, "Neighbor Address Lost fired: 0x%0zx", (size_t)ptr);

  nhdp_db_neighbor_addr_remove(naddr);
}

/**
 * Callback triggered when 2hop valitidy timer fires
 * @param ptr nhdp 2hop address
 */
static void
_cb_l2hop_vtime(void *ptr) {
  struct nhdp_l2hop *l2hop = ptr;

  OLSR_DEBUG(LOG_NHDP, "2Hop vtime fired: 0x%0zx", (size_t)ptr);
  nhdp_db_link_2hop_remove(l2hop);
}
