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

static void _addr_move(struct nhdp_addr *,
    struct nhdp_neighbor *, struct nhdp_link *);
static void _link_status_now_symmetric(struct nhdp_link *lnk);
static void _link_status_not_symmetric_anymore(struct nhdp_link *lnk);
int _nhdp_db_link_calculate_status(struct nhdp_link *lnk);

static void _cb_link_vtime(void *);
static void _cb_link_heard(void *);
static void _cb_link_symtime(void *);
static void _cb_addr_vtime(void *);
static void _cb_2hop_vtime(void *);

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

struct avl_tree nhdp_addr_tree;
struct avl_tree nhdp_2hop_tree;
struct list_entity nhdp_neigh_list;
struct list_entity nhdp_link_list;

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
  olsr_timer_add(&_link_heard_info);
  olsr_timer_add(&_link_symtime_info);
  olsr_timer_add(&_addr_vtime_info);
  olsr_timer_add(&_2hop_vtime_info);
}

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
  olsr_timer_remove(&_link_vtime_info);

  /* cleanup all memory cookies */
  olsr_memcookie_remove(&_2hop_info);
  olsr_memcookie_remove(&_addr_info);
  olsr_memcookie_remove(&_link_info);
  olsr_memcookie_remove(&_neigh_info);
}

struct nhdp_neighbor *
nhdp_db_neighbor_insert(void) {
  struct nhdp_neighbor *neigh;

  neigh = olsr_memcookie_malloc(&_neigh_info);
  if (neigh == NULL) {
    return NULL;
  }

  avl_init(&neigh->_addresses, avl_comp_netaddr, false, NULL);
  list_init_head(&neigh->_links);

  list_add_tail(&nhdp_neigh_list, &neigh->_node);
  return neigh;
}

void
nhdp_db_neighbor_remove(struct nhdp_neighbor *neigh) {
  struct nhdp_addr *naddr, *na_it;
  struct nhdp_link *lnk, *l_it;

  /* detach/remove all addresses */
  avl_for_each_element_safe(&neigh->_addresses, naddr, _neigh_node, na_it) {
    if (naddr->lost) {
      nhdp_db_addr_detach_neigh(naddr);
    }
    else {
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

void
nhdp_db_neighbor_join(struct nhdp_neighbor *dst, struct nhdp_neighbor *src) {
  struct nhdp_addr *naddr, *na_it;

  if (dst == src) {
    return;
  }

  /* fix symmetric link count */
  dst->symmetric += src->symmetric;

  /* move all addresses  */
  avl_for_each_element_safe(&src->_addresses, naddr, _neigh_node, na_it) {
    /* move address to new neighbor */
    avl_remove(&src->_addresses, &naddr->_neigh_node);
    avl_insert(&dst->_addresses, &naddr->_neigh_node);
    naddr->neigh = dst;

    /* move link to new neighbor */
    list_remove(&naddr->link->_neigh_node);
    list_add_tail(&dst->_links, &naddr->link->_neigh_node);

    naddr->link->neigh = dst;
  }

  nhdp_db_neighbor_remove(src);
}

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

  return lnk;
}

void
nhdp_db_link_remove(struct nhdp_link *lnk) {
  struct nhdp_addr *naddr, *na_it;
  struct nhdp_2hop *twohop, *th_it;

  if (lnk->status == RFC5444_LINKSTATUS_SYMMETRIC) {
    _link_status_not_symmetric_anymore(lnk);
  }

  /* stop timers */
  olsr_timer_stop(&lnk->sym_time);
  olsr_timer_stop(&lnk->heard_time);
  olsr_timer_stop(&lnk->vtime);

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

void
nhdp_db_link_update_status(struct nhdp_link *lnk) {
  bool was_symmetric;

  was_symmetric = lnk->status == RFC5444_LINKSTATUS_SYMMETRIC;

  /* update link status */
  lnk->status = _nhdp_db_link_calculate_status(lnk);

  /* handle database changes */
  if (was_symmetric && lnk->status != RFC5444_LINKSTATUS_SYMMETRIC) {
    _link_status_not_symmetric_anymore(lnk);
  }
  if (!was_symmetric && lnk->status == RFC5444_LINKSTATUS_SYMMETRIC) {
    _link_status_now_symmetric(lnk);
  }
}

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

void
nhdp_db_addr_attach_neigh(
    struct nhdp_addr *naddr, struct nhdp_neighbor *neigh) {
  if (naddr->lost) {
    olsr_timer_stop(&naddr->vtime);
    naddr->lost = false;
  }

  _addr_move(naddr, neigh, NULL);
}

void
nhdp_db_addr_attach_link(struct nhdp_addr *naddr, struct nhdp_link *lnk) {
  if (naddr->lost) {
    olsr_timer_stop(&naddr->vtime);
    naddr->lost = false;
  }

  _addr_move(naddr, lnk->neigh, lnk);
}

void
nhdp_db_addr_detach_neigh(struct nhdp_addr *naddr) {
  _addr_move(naddr, NULL, NULL);
}

void
nhdp_db_addr_detach_link(struct nhdp_addr *naddr) {
  _addr_move(naddr, naddr->neigh, NULL);
}

void
nhdp_db_addr_set_lost(struct nhdp_addr *naddr, uint64_t vtime) {
  naddr->lost = true;
  olsr_timer_set(&naddr->vtime, vtime);
}

void
nhdp_db_addr_remove_lost(struct nhdp_addr *naddr) {
  naddr->lost = false;
  olsr_timer_stop(&naddr->vtime);
}

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

void
nhdp_db_2hop_remove(struct nhdp_2hop *twohop) {
  avl_remove(&twohop->link->_2hop, &twohop->_link_node);
  avl_remove(&nhdp_2hop_tree, &twohop->_global_node);

  olsr_memcookie_free(&_2hop_info, twohop);
}

static void
_addr_move(struct nhdp_addr *naddr,
    struct nhdp_neighbor *neigh, struct nhdp_link *lnk) {
  struct netaddr_str buf;

  assert (lnk == NULL || (neigh != NULL && neigh == lnk->neigh));

  OLSR_DEBUG(LOG_NHDP, "Move address %s to neigh=0x%zx, link=0x%zx",
      netaddr_to_string(&buf, &naddr->if_addr), (size_t)neigh, (size_t)lnk);

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

static void
_cb_link_vtime(void *ptr) {
  struct nhdp_link *lnk = ptr;
  struct nhdp_neighbor *neigh;

  neigh = lnk->neigh;

  if (lnk->status == RFC5444_LINKSTATUS_SYMMETRIC) {
    _link_status_not_symmetric_anymore(lnk);
  }

  /* remove link from database */
  nhdp_db_link_remove(lnk);

  /* check if neighbor still has links */
  if (list_is_empty(&neigh->_links)) {
    nhdp_db_neighbor_remove(neigh);
  }
}

static void
_cb_link_heard(void *ptr) {
  nhdp_db_link_update_status(ptr);
}

static void
_cb_link_symtime(void *ptr __attribute__((unused))) {
  nhdp_db_link_update_status(ptr);
}

static void
_cb_addr_vtime(void *ptr) {
  struct nhdp_addr *naddr = ptr;

  /* lost neighbor vtime triggered */
  if (naddr->neigh == NULL) {
    nhdp_db_addr_remove(naddr);
  }
  else {
    /* address is still used as non-symmetric */
    naddr->lost = false;
  }
}

static void
_cb_2hop_vtime(void *ptr) {
  /* 2hop address vtime triggered */
  nhdp_db_2hop_remove(ptr);
}
