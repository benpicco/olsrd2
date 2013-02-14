/*
 * nhdp_db.h
 *
 *  Created on: Sep 17, 2012
 *      Author: rogge
 */

#ifndef NHDP_DB_H_
#define NHDP_DB_H_

struct nhdp_link;
struct nhdp_laddr;
struct nhdp_l2hop;
struct nhdp_neighbor;
struct nhdp_naddr;

#include "common/common_types.h"
#include "common/avl.h"
#include "common/list.h"
#include "common/netaddr.h"

#include "core/olsr_timer.h"
#include "rfc5444/rfc5444_iana.h"

enum nhdp_link_status {
  NHDP_LINK_PENDING   = -1,                          //!< NHDP_LINK_PENDING
  NHDP_LINK_LOST      = RFC5444_LINKSTATUS_LOST,     //!< NHDP_LINK_LOST
  NHDP_LINK_SYMMETRIC = RFC5444_LINKSTATUS_SYMMETRIC,//!< NHDP_LINK_SYMMETRIC
  NHDP_LINK_HEARD     = RFC5444_LINKSTATUS_HEARD,    //!< NHDP_LINK_HEARD
};

/**
 * nhdl_link represents a link by a specific local interface to one interface
 * of a one-hop neighbor.
 */
struct nhdp_link {
  /* timer that fires if this link is not symmetric anymore */
  struct olsr_timer_entry sym_time;

  /* timer that fires if the last received neighbor HELLO timed out */
  struct olsr_timer_entry heard_time;

  /* timer that fires when the link has to be removed from the database */
  struct olsr_timer_entry vtime;

  /* cached status of the linked */
  enum nhdp_link_status status;

  /* quality value for link hysteresis */
  uint32_t hyst_quality;

  /* true if link is still pending (but might be considered online heard later) */
  bool hyst_pending;

  /* true if link is considered lost at the moment */
  bool hyst_lost;

  /* true if link is used as flooding MPR */
  bool mpr_flooding;

  /* true if link is used as routing MPR */
  bool mpr_routing;

  /* pointer to local interface for this link */
  struct nhdp_interface *local_if;

  /* pointer to neighbor entry of the other side of the link */
  struct nhdp_neighbor *neigh;

  /* internal field for NHDP processing */
  int _process_count;

  /* tree of local addresses of the other side of the link */
  struct avl_tree _addresses;

  /* tree of two-hop addresses reachable through the other side of the link */
  struct avl_tree _2hop;

  /* member entry for global list of nhdp links */
  struct list_entity _global_node;

  /* member entry for nhdp links of local interface */
  struct list_entity _if_node;

  /* member entry for nhdp links of neighbor node */
  struct list_entity _neigh_node;
};

/**
 * nhdp_laddr represents an interface address of a link
 */
struct nhdp_laddr {
  /* links interface address */
  struct netaddr link_addr;

  /* link entry for address */
  struct nhdp_link *link;

  /* internal variable for NHDP processing */
  bool _might_be_removed;

  /* member entry for addresses of neighbor link */
  struct avl_node _link_node;

  /* member entry for addresss of neighbor */
  struct avl_node _neigh_node;

  /* member entry for interface tree of link addresses */
  struct avl_node _if_node;
};

/**
 * nhdp_l2hop represents an address of a two-hop neighbor
 */
struct nhdp_l2hop {
  /* address of two-hop neighbor */
  struct netaddr twohop_addr;

  /*
   * link entry for two-hop address, might be NULL if only learned as
   * an "other neighbor" through a HELLO
   */
  struct nhdp_link *link;

  /* validity time for this address */
  struct olsr_timer_entry _vtime;

  /* member entry for two-hop addresses of neighbor link */
  struct avl_node _link_node;
};

/**
 * nhdp_neighbor represents a neighbor node (with one or multiple interfaces
 */
struct nhdp_neighbor {
  /* number of links to this neighbor which are symmetric */
  int symmetric;

  /* internal field for NHDP processing */
  int _process_count;

  /*
   * timer that fires when the ipv6 addresses
   * of this neighbor have to be removed
   */
  struct olsr_timer_entry vtime_v4;

  /*
   * timer that fires when the ipv6 addresses
   * of this neighbor have to be removed
   */
  struct olsr_timer_entry vtime_v6;

  /* list of links for this neighbor */
  struct list_entity _links;

  /* tree of addresses of this neighbor */
  struct avl_tree _neigh_addresses;

  /* tree of addresses of this neighbors links */
  struct avl_tree _link_addresses;

  /* member entry for global list of neighbors */
  struct list_entity _node;
};

/**
 * nhdp_naddr represents an address of a known 1-hop neighbor
 * or a former (lost) address which will be removed soon
 */
struct nhdp_naddr {
    /* neighbor interface address */
    struct netaddr neigh_addr;

    /* backlink to neighbor */
    struct nhdp_neighbor *neigh;

    /* link address usage counter */
    int laddr_count;

    /* validity time for this address when its lost */
    struct olsr_timer_entry _lost_vtime;

    /* member entry for neighbor address tree */
    struct avl_node _neigh_node;

    /* member entry for global neighbor address tree */
    struct avl_node _global_node;

    /* temporary variables for NHDP Hello processing */
    bool _this_if, _might_be_removed;
};

/* handler for generating MPR information of a link */
struct nhdp_mpr_handler {
  /* name of handler */
  const char *name;

  /* update mprs of link, update all mprs if link is NULL */
  void (* update_mprs)(struct nhdp_link *);
};

EXPORT extern struct list_entity nhdp_neigh_list;
EXPORT extern struct list_entity nhdp_link_list;
EXPORT extern struct avl_tree nhdp_naddr_tree;

void nhdp_db_init(void);
void nhdp_db_cleanup(void);

EXPORT struct nhdp_neighbor *nhdp_db_neighbor_add(void);
EXPORT void nhdp_db_neighbor_remove(struct nhdp_neighbor *);
EXPORT void nhdp_db_neighbor_join(struct nhdp_neighbor *, struct nhdp_neighbor *);
EXPORT struct nhdp_naddr *nhdp_db_neighbor_addr_add(struct nhdp_neighbor *, struct netaddr *);
EXPORT void nhdp_db_neighbor_addr_remove(struct nhdp_naddr *);
EXPORT void nhdp_db_neighbor_addr_move(struct nhdp_neighbor *, struct nhdp_naddr *);

EXPORT struct nhdp_link *nhdp_db_link_add(struct nhdp_neighbor *, struct nhdp_interface *);
EXPORT void nhdp_db_link_remove(struct nhdp_link *);
EXPORT struct nhdp_laddr *nhdp_db_link_addr_add(struct nhdp_link *, struct netaddr*);
EXPORT void nhdp_db_link_addr_remove(struct nhdp_laddr *);
EXPORT void nhdp_db_link_addr_move(struct nhdp_link *, struct nhdp_laddr *);
EXPORT struct nhdp_l2hop *nhdp_db_link_2hop_add(
    struct nhdp_link *, struct netaddr *);
EXPORT void nhdp_db_link_2hop_remove(struct nhdp_l2hop *);

EXPORT int _nhdp_db_link_calculate_status(struct nhdp_link *lnk);
EXPORT void nhdp_db_link_update_status(struct nhdp_link *);

static INLINE struct nhdp_naddr *
nhdp_db_neighbor_addr_get(struct netaddr *addr) {
  struct nhdp_naddr *naddr;

  return avl_find_element(&nhdp_naddr_tree, addr, naddr, _global_node);
}

static INLINE struct nhdp_laddr *
nhdp_db_link_addr_get(struct nhdp_link *lnk, struct netaddr *addr) {
  struct nhdp_laddr *laddr;
  return avl_find_element(&lnk->_addresses, addr, laddr, _link_node);
}

static INLINE struct nhdp_l2hop *
ndhp_db_link_2hop_get(struct nhdp_link *lnk, struct netaddr *addr) {
  struct nhdp_l2hop *l2hop;
  return avl_find_element(&lnk->_2hop, addr, l2hop, _link_node);
}

/**
 * Sets the validity time of a nhdp link
 * @param lnk pointer to nhdp link
 * @param vtime validity time in milliseconds
 */
static INLINE void
nhdp_db_link_set_vtime(
    struct nhdp_link *lnk, uint64_t vtime) {
  olsr_timer_set(&lnk->vtime, vtime);
}

/**
 * Sets the time until a NHDP link is not considered heard anymore
 * @param lnk pointer to nhdp link
 * @param htime heard time in milliseconds
 */
static INLINE void
nhdp_db_link_set_heardtime(
    struct nhdp_link *lnk, uint64_t htime) {
  olsr_timer_set(&lnk->heard_time, htime);
}

/**
 * Sets the time until a NHDP link is not considered symmetric anymore
 * @param lnk pointer to nhdp link
 * @param stime symmetric time in milliseconds
 */
static INLINE void
nhdp_db_link_set_symtime(
    struct nhdp_link *lnk, uint64_t stime) {
  olsr_timer_set(&lnk->sym_time, stime);
  nhdp_db_link_update_status(lnk);
}

static INLINE void
nhdp_db_link_2hop_set_vtime(
    struct nhdp_l2hop *l2hop, uint64_t vtime) {
  olsr_timer_set(&l2hop->_vtime, vtime);
}

static INLINE void
nhdp_db_neighbor_addr_set_lost(struct nhdp_naddr *naddr, uint64_t vtime) {
  olsr_timer_set(&naddr->_lost_vtime, vtime);
}

static INLINE void
nhdp_db_neighbor_addr_not_lost(struct nhdp_naddr *naddr) {
  olsr_timer_stop(&naddr->_lost_vtime);
}

static INLINE bool
nhdp_db_neighbor_addr_is_lost(struct nhdp_naddr *naddr) {
  return olsr_timer_is_active(&naddr->_lost_vtime);
}
#endif /* NHDP_DB_H_ */
