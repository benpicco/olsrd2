/*
 * nhdp_db.h
 *
 *  Created on: Sep 17, 2012
 *      Author: rogge
 */

#ifndef NHDP_DB_H_
#define NHDP_DB_H_

#include "common/common_types.h"
#include "common/avl.h"
#include "common/list.h"
#include "common/netaddr.h"

#include "core/olsr_timer.h"
#include "rfc5444/rfc5444_iana.h"

enum nhdp_link_status {
  NHDP_LINK_PENDING   = -1,
  NHDP_LINK_LOST      = RFC5444_LINKSTATUS_LOST,
  NHDP_LINK_SYMMETRIC = RFC5444_LINKSTATUS_SYMMETRIC,
  NHDP_LINK_HEARD     = RFC5444_LINKSTATUS_HEARD,
};

struct nhdp_link {
  struct olsr_timer_entry sym_time;
  struct olsr_timer_entry heard_time;
  struct olsr_timer_entry vtime;

  enum nhdp_link_status status;

  uint32_t hyst_quality;
  bool hyst_pending;
  bool hyst_lost;

  struct nhdp_interface *local_if;
  struct nhdp_neighbor *neigh;

  struct avl_tree _addresses;
  struct avl_tree _2hop;

  struct list_entity _global_node;
  struct list_entity _if_node;
  struct list_entity _neigh_node;
};

struct nhdp_addr {
  struct netaddr if_addr;

  struct nhdp_link *link;
  struct nhdp_neighbor *neigh;

  /* for neighbor address */
  struct olsr_timer_entry vtime;
  bool lost;

  bool _might_be_removed, _this_if;

  struct avl_node _link_node;
  struct avl_node _neigh_node;
  struct avl_node _global_node;
};

struct nhdp_2hop {
  struct netaddr neigh_addr;

  struct nhdp_link *link;

  struct olsr_timer_entry _vtime;

  struct avl_node _link_node;
  struct avl_node _global_node;
};

struct nhdp_neighbor {
  int symmetric;

  struct list_entity _links;
  struct avl_tree _addresses;

  struct list_entity _node;
};

EXPORT extern struct avl_tree nhdp_addr_tree;
EXPORT extern struct avl_tree nhdp_2hop_tree;
EXPORT extern struct list_entity nhdp_neigh_list;
EXPORT extern struct list_entity nhdp_link_list;

void nhdp_db_init(void);
void nhdp_db_cleanup(void);

EXPORT struct nhdp_neighbor *nhdp_db_neighbor_insert(void);
EXPORT void nhdp_db_neighbor_remove(struct nhdp_neighbor *);
EXPORT void nhdp_db_neighbor_join(struct nhdp_neighbor *, struct nhdp_neighbor *);

EXPORT struct nhdp_link *nhdp_db_link_insert(struct nhdp_neighbor *, struct nhdp_interface *);
EXPORT void nhdp_db_link_remove(struct nhdp_link *);
EXPORT int _nhdp_db_link_calculate_status(struct nhdp_link *lnk);
EXPORT void nhdp_db_link_update_status(struct nhdp_link *);

EXPORT struct nhdp_addr *nhdp_db_addr_insert(struct netaddr *);
EXPORT void nhdp_db_addr_remove(struct nhdp_addr *);
EXPORT void nhdp_db_addr_attach_neigh(struct nhdp_addr *, struct nhdp_neighbor *);
EXPORT void nhdp_db_addr_detach_neigh(struct nhdp_addr *);
EXPORT void nhdp_db_addr_attach_link(struct nhdp_addr *, struct nhdp_link *);
EXPORT void nhdp_db_addr_detach_link(struct nhdp_addr *);
EXPORT void nhdp_db_addr_set_lost(struct nhdp_addr *, uint64_t);
EXPORT void nhdp_db_addr_remove_lost(struct nhdp_addr *);

EXPORT struct nhdp_2hop *nhdp_db_2hop_insert(
    struct nhdp_link *, struct netaddr *);
EXPORT void nhdp_db_2hop_remove(struct nhdp_2hop *);

static INLINE void
nhdp_db_link_set_vtime(
    struct nhdp_link *lnk, uint64_t vtime) {
  olsr_timer_set(&lnk->vtime, vtime);
}

static INLINE void
nhdp_db_link_set_heardtime(
    struct nhdp_link *lnk, uint64_t htime) {
  olsr_timer_set(&lnk->heard_time, htime);
}

static INLINE void
nhdp_db_link_set_symtime(
    struct nhdp_link *lnk, uint64_t stime) {
  olsr_timer_set(&lnk->sym_time, stime);
  nhdp_db_link_update_status(lnk);
}

static INLINE struct nhdp_addr *
nhdp_db_addr_get(struct netaddr *addr) {
  struct nhdp_addr *naddr;

  return avl_find_element(&nhdp_addr_tree, addr, naddr, _global_node);
}

static INLINE void
nhdp_db_addr_set_vtime(
    struct nhdp_addr *naddr, uint64_t vtime) {
  olsr_timer_set(&naddr->vtime, vtime);
}

static INLINE struct nhdp_2hop *
nhdp_db_2hop_get(struct nhdp_link *lnk, struct netaddr *addr) {
  struct nhdp_2hop *n2;

  return avl_find_element(&lnk->_2hop, addr, n2, _link_node);
}


static INLINE void
nhdp_db_2hop_set_vtime(
    struct nhdp_2hop *n2, uint64_t vtime) {
  olsr_timer_set(&n2->_vtime, vtime);
}

#endif /* NHDP_DB_H_ */
