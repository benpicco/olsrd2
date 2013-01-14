/*
 * nhdp_db.c
 *
 *  Created on: Sep 17, 2012
 *      Author: rogge
 */

#include "common/common_types.h"
#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/list.h"
#include "common/netaddr.h"

#include "core/olsr_logging.h"
#include "core/olsr_memcookie.h"
#include "core/olsr_timer.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_db.h"

/* Prototypes of local functions */
static void _addr_move(struct nhdp_addr *,
    struct nhdp_neighbor *, struct nhdp_link *);
static void _link_status_now_symmetric(struct nhdp_link *lnk);
static void _link_status_not_symmetric_anymore(struct nhdp_link *lnk);
int _nhdp_db_link_calculate_status(struct nhdp_link *lnk);

static void _cb_link_vtime(void *);
static void _cb_link_vtime_v6(void *);
static void _cb_link_heard(void *);
static void _cb_link_symtime(void *);
static void _cb_addr_vtime(void *);
static void _cb_2hop_vtime(void *);

/* memory and timer classes necessary for NHDP */
static struct olsr_memcookie_info _neigh_info = {
  .name = "NHDP neighbor",
  .size = sizeof(struct nhdp_neighbor),
};

static struct olsr_memcookie_info _link_info = {
  .name = "NHDP link",
  .size = sizeof(struct nhdp_link),
};

static struct olsr_memcookie_info _addr_info = {
  .name = "NHDP address",
  .size = sizeof(struct nhdp_addr),
};

static struct olsr_memcookie_info _2hop_info = {
  .name = "NHDP twohop neighbor",
  .size = sizeof(struct nhdp_2hop),
};

static struct olsr_timer_info _link_vtime_info = {
  .name = "NHDP link vtime",
  .callback = _cb_link_vtime,
};

static struct olsr_timer_info _link_vtimev6_info = {
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

static struct olsr_timer_info _addr_vtime_info = {
  .name = "NHDP address vtime",
  .callback = _cb_addr_vtime,
};

static struct olsr_timer_info _2hop_vtime_info = {
  .name = "NHDP 2hop vtime",
  .callback = _cb_2hop_vtime,
};

/* global tree of neighbor addresses */
struct avl_tree nhdp_addr_tree;

/* global tree of 2-hop neighbor addresses */
struct avl_tree nhdp_2hop_tree;

/* list of neighbors */
struct list_entity nhdp_neigh_list;

/* list of links (to neighbors) */
struct list_entity nhdp_link_list;

/**
 * Initialize NHDP databases
 */
void
nhdp_db_init(void) {
  avl_init(&nhdp_addr_tree, avl_comp_netaddr, false, NULL);
  avl_init(&nhdp_2hop_tree, avl_comp_netaddr, true, NULL);
  list_init_head(&nhdp_neigh_list);
  list_init_head(&nhdp_link_list);

  olsr_memcookie_add(&_neigh_info);
  olsr_memcookie_add(&_link_info);
  olsr_memcookie_add(&_addr_info);
  olsr_memcookie_add(&_2hop_info);

  olsr_timer_add(&_link_vtime_info);
  olsr_timer_add(&_link_vtimev6_info);
  olsr_timer_add(&_link_heard_info);
  olsr_timer_add(&_link_symtime_info);
  olsr_timer_add(&_addr_vtime_info);
  olsr_timer_add(&_2hop_vtime_info);
}

/**
 * Cleanup NHDP databases
 */
void
nhdp_db_cleanup(void) {
  struct nhdp_neighbor *neigh, *n_it;
  struct nhdp_addr *naddr, *na_it;

  /* remove all addresses left */
  avl_for_each_element_safe(&nhdp_addr_tree, naddr, _global_node, na_it) {
    nhdp_db_addr_remove(naddr);
  }

  /* remove all neighbors (and everything attached to them) */
  list_for_each_element_safe(&nhdp_neigh_list, neigh, _node, n_it) {
    nhdp_db_neighbor_remove(neigh);
  }

  /* cleanup all timers */
  olsr_timer_remove(&_2hop_vtime_info);
  olsr_timer_remove(&_addr_vtime_info);
  olsr_timer_remove(&_link_symtime_info);
  olsr_timer_remove(&_link_heard_info);
  olsr_timer_remove(&_link_vtimev6_info);
  olsr_timer_remove(&_link_vtime_info);

  /* cleanup all memory cookies */
  olsr_memcookie_remove(&_2hop_info);
  olsr_memcookie_remove(&_addr_info);
  olsr_memcookie_remove(&_link_info);
  olsr_memcookie_remove(&_neigh_info);
}

/**
 * @return new NHDP neighbor without links and addresses,
 *  NULL if out of memory
 */
struct nhdp_neighbor *
nhdp_db_neighbor_insert(void) {
  struct nhdp_neighbor *neigh;

  neigh = olsr_memcookie_malloc(&_neigh_info);
  if (neigh == NULL) {
    return NULL;
  }

  OLSR_DEBUG(LOG_NHDP, "New Neighbor: 0x%0zx", (size_t)neigh);

  avl_init(&neigh->_addresses, avl_comp_netaddr, false, NULL);
  list_init_head(&neigh->_links);

  list_add_tail(&nhdp_neigh_list, &neigh->_node);
  return neigh;
}

/**
 * Remove NHDP neighbor including links and addresses from db
 * @param neigh nhdp neighbor to be removed
 */
void
nhdp_db_neighbor_remove(struct nhdp_neighbor *neigh) {
  struct nhdp_addr *naddr, *na_it;
  struct nhdp_link *lnk, *l_it;

  OLSR_DEBUG(LOG_NHDP, "Remove Neighbor: 0x%0zx", (size_t)neigh);

  /* detach/remove all addresses */
  avl_for_each_element_safe(&neigh->_addresses, naddr, _neigh_node, na_it) {
    if (naddr->lost) {
      /* just detach lost addresses and keep them in global list */
      nhdp_db_addr_detach_neigh(naddr);
    }
    else {
      /* remove address from neighbor (and mark them lost this way */
      nhdp_db_addr_remove(naddr);
    }
  }

  /* remove all links (and with this all 2hops) */
  list_for_each_element_safe(&neigh->_links, lnk, _neigh_node, l_it) {
    nhdp_db_link_remove(lnk);
  }

  list_remove(&neigh->_node);
  olsr_memcookie_free(&_neigh_info, neigh);
}

/**
 * Join the links and addresses of two NHDP neighbors
 * @param dst target neighbor which gets all the links and addresses
 * @param src source neighbor which will be removed afterwards
 */
void
nhdp_db_neighbor_join(struct nhdp_neighbor *dst, struct nhdp_neighbor *src) {
  struct nhdp_addr *naddr, *na_it;
  struct nhdp_link *lnk, *l_it;

  if (dst == src) {
    return;
  }

  /* fix symmetric link count */
  dst->symmetric += src->symmetric;

  /* move links */
  list_for_each_element_safe(&src->_links, lnk, _neigh_node, l_it) {
    /* move link to new neighbor */
    list_remove(&lnk->_neigh_node);
    list_add_tail(&dst->_links, &lnk->_neigh_node);

    lnk->neigh = dst;
  }

  /* move all addresses  */
  avl_for_each_element_safe(&src->_addresses, naddr, _neigh_node, na_it) {
    /* move address to new neighbor */
    avl_remove(&src->_addresses, &naddr->_neigh_node);
    avl_insert(&dst->_addresses, &naddr->_neigh_node);
    naddr->neigh = dst;
  }

  nhdp_db_neighbor_remove(src);
}

/**
 * Insert a new link into a nhdp neighbors database
 * @param neigh neighbor which will get the new link
 * @param local_if local interface through which the link was heard
 * @return new nhdp link, NULL if out of memory
 */
struct nhdp_link *
nhdp_db_link_insert(struct nhdp_neighbor *neigh, struct nhdp_interface *local_if) {
  struct nhdp_link *lnk;

  lnk = olsr_memcookie_malloc(&_link_info);
  if (lnk == NULL) {
    return NULL;
  }

  /* hook into interface */
  nhdp_interfaces_add_link(local_if, lnk);

  /* hook into neighbor */
  list_add_tail(&neigh->_links, &lnk->_neigh_node);
  lnk->neigh = neigh;

  /* hook into global list */
  list_add_tail(&nhdp_link_list, &lnk->_global_node);

  /* init local trees */
  avl_init(&lnk->_addresses, avl_comp_netaddr, false, NULL);
  avl_init(&lnk->_2hop, avl_comp_netaddr, false, NULL);

  /* init timers */
  lnk->sym_time.info = &_link_symtime_info;
  lnk->sym_time.cb_context = lnk;
  lnk->heard_time.info = &_link_heard_info;
  lnk->heard_time.cb_context = lnk;
  lnk->vtime.info = &_link_vtime_info;
  lnk->vtime.cb_context = lnk;
  lnk->vtime_v6.info = &_link_vtimev6_info;
  lnk->vtime_v6.cb_context = lnk;

  return lnk;
}

/**
 * Remove a nhdp link from database
 * @param lnk nhdp link to be removed
 */
void
nhdp_db_link_remove(struct nhdp_link *lnk) {
  struct nhdp_addr *naddr, *na_it;
  struct nhdp_2hop *twohop, *th_it;

  if (lnk->status == NHDP_LINK_SYMMETRIC) {
    _link_status_not_symmetric_anymore(lnk);
  }

  /* stop link timers */
  olsr_timer_stop(&lnk->sym_time);
  olsr_timer_stop(&lnk->heard_time);
  olsr_timer_stop(&lnk->vtime);
  olsr_timer_stop(&lnk->vtime_v6);

  /* detach all addresses */
  avl_for_each_element_safe(&lnk->_addresses, naddr, _link_node, na_it) {
    nhdp_db_addr_detach_link(naddr);
  }

  /* remove all 2hop addresses */
  avl_for_each_element_safe(&lnk->_2hop, twohop, _link_node, th_it) {
    nhdp_db_2hop_remove(twohop);
  }

  /* unlink */
  nhdp_interfaces_remove_link(lnk);
  list_remove(&lnk->_neigh_node);

  /* remove from global list */
  list_remove(&lnk->_global_node);
  olsr_memcookie_free(&_link_info, lnk);
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
}

/**
 * Add a new nhdp (one-hop) address to database
 * @param addr address to be added
 * @return nhdp address, NULL if out of memory
 */
struct nhdp_addr *
nhdp_db_addr_insert(struct netaddr *addr) {
  struct nhdp_addr *naddr;

  naddr = olsr_memcookie_malloc(&_addr_info);
  if (naddr == NULL) {
    return NULL;
  }

  /* set key and insert into global tree */
  memcpy(&naddr->if_addr, addr, sizeof(*addr));
  naddr->_global_node.key = &naddr->if_addr;
  avl_insert(&nhdp_addr_tree, &naddr->_global_node);

  /* initialize key to insert address later */
  naddr->_neigh_node.key = &naddr->if_addr;
  naddr->_link_node.key = &naddr->if_addr;

  /* initialize timer */
  naddr->vtime.info = &_addr_vtime_info;
  naddr->vtime.cb_context = naddr;

  /* initially lost */
  naddr->lost = true;

  return naddr;
}

/**
 * Remove a nhdp address from database
 * @param naddr nhdp address
 */
void
nhdp_db_addr_remove(struct nhdp_addr *naddr) {
  /* stop timer */
  olsr_timer_stop(&naddr->vtime);

  /* unlink from link */
  if (naddr->link) {
    avl_remove(&naddr->link->_addresses, &naddr->_link_node);
  }

  /* unlink from neighbor */
  if (naddr->neigh) {
    avl_remove(&naddr->neigh->_addresses, &naddr->_neigh_node);
  }

  /* unlink from global tree */
  avl_remove(&nhdp_addr_tree, &naddr->_global_node);

  olsr_memcookie_free(&_addr_info, naddr);
}

/**
 * Attach a neighbor to a nhdp address
 * @param naddr nhdp address
 * @param neigh nhdp neighbor
 */
void
nhdp_db_addr_attach_neigh(
    struct nhdp_addr *naddr, struct nhdp_neighbor *neigh) {
  if (naddr->lost) {
    olsr_timer_stop(&naddr->vtime);
    naddr->lost = false;
  }

  _addr_move(naddr, neigh, NULL);
}

/**
 * Attach a link to a nhdp address
 * @param naddr nhdp address
 * @param lnk nhdp link
 */
void
nhdp_db_addr_attach_link(struct nhdp_addr *naddr, struct nhdp_link *lnk) {
  if (naddr->lost) {
    olsr_timer_stop(&naddr->vtime);
    naddr->lost = false;
  }

  _addr_move(naddr, lnk->neigh, lnk);
}

/**
 * Detach the neighbor (and the link) from a nhdp address
 * @param naddr nhdp address
 */
void
nhdp_db_addr_detach_neigh(struct nhdp_addr *naddr) {
  _addr_move(naddr, NULL, NULL);
}

/**
 * Detach the link from a nhdp address, but keep the neighbor
 * @param naddr nhdp address
 */
void
nhdp_db_addr_detach_link(struct nhdp_addr *naddr) {
  _addr_move(naddr, naddr->neigh, NULL);
}

/**
 * Mark a nhdp address as lost
 * @param naddr nhdp address
 * @param vtime time until address should be removed from database
 */
void
nhdp_db_addr_set_lost(struct nhdp_addr *naddr, uint64_t vtime) {
  naddr->lost = true;
  olsr_timer_set(&naddr->vtime, vtime);
}

/**
 * Remove 'lost' mark from an nhdp address, but do not remove it
 * from database.
 * @param naddr nhdp address
 */
void
nhdp_db_addr_remove_lost(struct nhdp_addr *naddr) {
  naddr->lost = false;
  olsr_timer_stop(&naddr->vtime);
}

/**
 * Add a new 2-hop address to a nhdp link
 * @param lnk nhpd link
 * @param addr network address
 * @return 2hop address, NULL if out of memory
 */
struct nhdp_2hop *
nhdp_db_2hop_insert(struct nhdp_link *lnk, struct netaddr *addr) {
  struct nhdp_2hop *twohop;

  twohop = olsr_memcookie_malloc(&_2hop_info);
  if (twohop == NULL) {
    return NULL;
  }

  /* set key and insert into global tree */
  memcpy(&twohop->neigh_addr, addr, sizeof(*addr));
  twohop->_global_node.key = &twohop->neigh_addr;
  avl_insert(&nhdp_2hop_tree, &twohop->_global_node);

  /* hook into link */
  twohop->_link_node.key = &twohop->neigh_addr;
  avl_insert(&lnk->_2hop, &twohop->_link_node);
  twohop->link = lnk;

  /* initialize timer */
  twohop->_vtime.info = &_2hop_vtime_info;
  twohop->_vtime.cb_context = twohop;

  return twohop;
}

/**
 * Remove a 2-hop address from database
 * @param twohop two-hop address of a nhdp link
 */
void
nhdp_db_2hop_remove(struct nhdp_2hop *twohop) {
  avl_remove(&twohop->link->_2hop, &twohop->_link_node);
  avl_remove(&nhdp_2hop_tree, &twohop->_global_node);

  olsr_timer_stop(&twohop->_vtime);

  olsr_memcookie_free(&_2hop_info, twohop);
}

/**
 * Helper function to attach/detach nhdp addresses from links/neighbors
 * @param naddr nhdp address
 * @param neigh nhdp neighbor, NULL to detach from link and neighbor
 * @param lnk nhdp link, NULL to detach from link
 */
static void
_addr_move(struct nhdp_addr *naddr,
    struct nhdp_neighbor *neigh, struct nhdp_link *lnk) {
  struct netaddr_str buf;

  assert (lnk == NULL || (neigh != NULL && neigh == lnk->neigh));

  OLSR_DEBUG(LOG_NHDP, "Move address %s from neigh=0x%zx, link=0x%zx to neigh=0x%zx, link=0x%zx",
      netaddr_to_string(&buf, &naddr->if_addr),
      (size_t)naddr->neigh, (size_t)naddr->link,
      (size_t)neigh, (size_t)lnk);

  if (naddr->neigh != neigh) {
    /* fix neighbor hook */
    if (naddr->neigh) {
      avl_remove(&naddr->neigh->_addresses, &naddr->_neigh_node);
    }
    if (neigh) {
      avl_insert(&neigh->_addresses, &naddr->_neigh_node);
    }
    naddr->neigh = neigh;
  }

  if (naddr->link != lnk) {
    /* fix link hook */
    if (naddr->link) {
      avl_remove(&naddr->link->_addresses, &naddr->_link_node);
    }
    if (lnk) {
      avl_insert(&lnk->_addresses, &naddr->_link_node);
    }
    naddr->link = lnk;
  }
}

/**
 * Helper function to calculate NHDP link status
 * @param lnk nhdp link
 * @return link status
 */
int
_nhdp_db_link_calculate_status(struct nhdp_link *lnk) {
  if (lnk->hyst_pending)
    return NHDP_LINK_PENDING;
  else if (lnk->hyst_lost)
    return RFC5444_LINKSTATUS_LOST;
  else if (olsr_timer_is_active(&lnk->sym_time))
    return RFC5444_LINKSTATUS_SYMMETRIC;
  else if (olsr_timer_is_active(&lnk->heard_time))
    return RFC5444_LINKSTATUS_HEARD;
  return RFC5444_LINKSTATUS_LOST;
}

/**
 * Helper function that handles the case of a link becoming symmetric
 * @param lnk nhdp link
 */
static void
_link_status_now_symmetric(struct nhdp_link *lnk) {
  struct nhdp_addr *naddr;

  lnk->neigh->symmetric++;

  if (lnk->neigh->symmetric == 1) {
    avl_for_each_element(&lnk->neigh->_addresses, naddr, _neigh_node) {
      nhdp_db_addr_remove_lost(naddr);
    }
  }
}

/**
 * Helper function that handles the case of a link becoming asymmetric
 * @param lnk nhdp link
 */
static void
_link_status_not_symmetric_anymore(struct nhdp_link *lnk) {
  struct nhdp_2hop *twohop, *twohop_it;
  struct nhdp_addr *naddr, *na_it;

  /* remove all 2hop neighbors */
  avl_for_each_element_safe(&lnk->_2hop, twohop, _link_node, twohop_it) {
    nhdp_db_2hop_remove(twohop);
  }

  lnk->neigh->symmetric--;
  if (lnk->neigh->symmetric == 0) {
    /* mark all neighbor addresses as lost */
    avl_for_each_element_safe(&lnk->neigh->_addresses, naddr, _neigh_node, na_it) {
      nhdp_db_addr_set_lost(naddr, lnk->local_if->n_hold_time);
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
 * Callback triggered when link validity timer for ipv6 addresses fires
 * @param ptr nhdp link
 */
static void
_cb_link_vtime_v6(void *ptr) {
  struct nhdp_link *lnk = ptr;
  struct nhdp_addr *naddr, *na_it;

  OLSR_DEBUG(LOG_NHDP, "Link vtime_v6 fired: 0x%0zx", (size_t)ptr);

  /* remove all IPv6 addresses from link */
  avl_for_each_element_safe(&lnk->_addresses, naddr, _link_node, na_it) {
    if (netaddr_get_address_family(&naddr->if_addr) == AF_INET6) {
      nhdp_db_addr_remove(naddr);
    }
  }

  if (lnk->_addresses.count == 0) {
    /* no address left, remove link */
    _cb_link_vtime(lnk);
  }
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
_cb_addr_vtime(void *ptr) {
  struct nhdp_addr *naddr = ptr;

  OLSR_DEBUG(LOG_NHDP, "Neighbor Address Lost fired: 0x%0zx", (size_t)ptr);

  /* lost neighbor vtime triggered */
  if (naddr->neigh == NULL) {
    nhdp_db_addr_remove(naddr);
  }
  else {
    /* address is still used as non-symmetric */
    naddr->lost = false;
  }
}

/**
 * Callback triggered when 2hop valitidy timer fires
 * @param ptr nhdp 2hop address
 */
static void
_cb_2hop_vtime(void *ptr) {
  /* 2hop address vtime triggered */

  OLSR_DEBUG(LOG_NHDP, "2Hop vtime fired: 0x%0zx", (size_t)ptr);
  nhdp_db_2hop_remove(ptr);
}