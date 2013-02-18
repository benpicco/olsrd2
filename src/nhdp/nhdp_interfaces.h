
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

#ifndef NHDP_INTERFACES_H_
#define NHDP_INTERFACES_H_

struct nhdp_interface;
struct nhdp_interface_addr;

#include "common/common_types.h"
#include "common/avl.h"
#include "common/list.h"
#include "common/netaddr.h"
#include "core/olsr_interface.h"
#include "core/olsr_netaddr_acl.h"
#include "core/olsr_timer.h"
#include "rfc5444/rfc5444_iana.h"
#include "tools/olsr_rfc5444.h"

#include "nhdp/nhdp_db.h"

enum nhdp_interface_mode {
  NHDP_IPV4,
  NHDP_IPV6,
  NHDP_DUAL,
};

extern const char *NHDP_INTERFACE_MODES[3];

/**
 * nhdp_interface represents a local interface participating in the mesh network
 */
struct nhdp_interface {
  /* listener for interface events */
  struct olsr_rfc5444_interface_listener rfc5444_if;

  /* does this interface only work with IPv4/IPv6 or does it dualstack? */
  enum nhdp_interface_mode mode;

  /* true if this interface has a neighbors that support only ipv4 */
  bool neigh_onlyv4;

  /* Willingness for MPR */
  enum rfc5444_willingness_values mpr_willingness;

  /* Willingness as configured */
  int mpr_willingness_default;

  /* interval between two hellos sent through this interface */
  uint64_t refresh_interval;

  /* See RFC 6130, 5.3.2 and 5.4.1 */
  uint64_t h_hold_time;
  uint64_t l_hold_time;
  uint64_t n_hold_time;
  uint64_t i_hold_time;

  /* ACL for incoming HELLO messages through this interface */
  struct olsr_netaddr_acl ifaddr_filter;

  /* timer for hello generation */
  struct olsr_timer_entry _hello_timer;

  /* member entry for global interface tree */
  struct avl_node _node;

  /* tree of interface addresses */
  struct avl_tree _if_addresses;

  /* list of interface nhdp links (nhdp_link objects) */
  struct list_entity _links;

  /* tree of addresses of links (nhdp_laddr objects) */
  struct avl_tree _link_addresses;
};

/**
 * nhdp_interface_addr represents a single address of a nhdp interface
 */
struct nhdp_interface_addr {
  /* interface address */
  struct netaddr if_addr;

  /* pointer to nhdp interface */
  struct nhdp_interface *interf;

  /* true if address was removed */
  bool removed;

  /* internal variable */
  bool _to_be_removed;

  /* validity time until entry should be removed from database */
  struct olsr_timer_entry _vtime;

  /* member entry for interfaces tree of addresses */
  struct avl_node _if_node;

  /* member entry for global address tree */
  struct avl_node _global_node;
};

EXPORT extern struct avl_tree nhdp_interface_tree;
EXPORT extern struct avl_tree nhdp_ifaddr_tree;

void nhdp_interfaces_init(struct olsr_rfc5444_protocol *);
void nhdp_interfaces_cleanup(void);

void nhdp_interfaces_update_neigh_addresstype(struct nhdp_interface *interf);
void nhdp_interface_update_addresses(void);

/**
 * @param interface name
 * @return nhdp interface, NULL if not found
 */
static INLINE struct nhdp_interface *
nhdp_interface_get(const char *name) {
  struct nhdp_interface *interf;

  return avl_find_element(&nhdp_interface_tree, name, interf, _node);
}

/**
 * @param interf nhdp interface
 * @return name of interface (e.g. wlan0)
 */
static INLINE const char *
nhdp_interface_get_name(struct nhdp_interface *interf) {
  return interf->_node.key;
}

/**
 * @param interf nhdp interface
 * @param addr network address
 * @return nhdp interface address
 */
static INLINE struct nhdp_interface_addr *
nhdp_interface_addr_if_get(struct nhdp_interface *interf, struct netaddr *addr) {
  struct nhdp_interface_addr *iaddr;

  return avl_find_element(&interf->_if_addresses, addr, iaddr, _if_node);
}

/**
 * @param network address
 * @return nhdp interface address
 */
static INLINE struct nhdp_interface_addr *
nhdp_interface_addr_global_get(struct netaddr *addr) {
  struct nhdp_interface_addr *iaddr;

  return avl_find_element(&nhdp_ifaddr_tree, addr, iaddr, _global_node);
}

/**
 * Add a link to a nhdp interface
 * @param interf nhdp interface
 * @param lnk nhdp link
 */
static INLINE void
nhdp_interface_add_link(struct nhdp_interface *interf,
    struct nhdp_link *lnk) {
  lnk->local_if = interf;

  list_add_tail(&interf->_links, &lnk->_if_node);
}

/**
 * Remove a nhdp link from a nhdp interface
 * @param lnk nhdp link
 */
static INLINE void
nhdp_interface_remove_link(struct nhdp_link *lnk) {
  list_remove(&lnk->_if_node);
  lnk->local_if = NULL;
}

/**
 * Attach a link address to the local nhdp interface
 * @param laddr
 */
static INLINE void
nhdp_interface_add_laddr(struct nhdp_laddr *laddr) {
  avl_insert(&laddr->link->local_if->_link_addresses, &laddr->_if_node);
}

/**
 * Detach a link address from the local nhdp interface
 * @param laddr
 */
static INLINE void
nhdp_interface_remove_laddr(struct nhdp_laddr *laddr) {
  avl_remove(&laddr->link->local_if->_link_addresses, &laddr->_if_node);
}

/**
 * @param interf local nhdp interface
 * @param addr network address
 * @return link address object fitting the network address, NULL if not found
 */
static INLINE struct nhdp_laddr *
nhdp_interface_get_link_addr(struct nhdp_interface *interf, struct netaddr *addr) {
  struct nhdp_laddr *laddr;

  return avl_find_element(&interf->_link_addresses, addr, laddr, _if_node);
}

/**
 * Set custom MPR willingness for local nhdp interface
 * @param interf local nhdp interface
 * @param will new willingness
 */
static INLINE void
nhdp_interface_set_mpr_willingness(struct nhdp_interface *interf, enum rfc5444_willingness_values will) {
  interf->mpr_willingness = will;
}

/**
 * Resets the MPR willingness for the local nhdp interface
 * @param interf local nhdp interface
 */
static INLINE void
nhdp_interface_reset_mpr_willingness(struct nhdp_interface *interf) {
  interf->mpr_willingness = RFC5444_WILLINGNESS_UNDEFINED;
}

/**
 * @param interf local nhdp interface
 * @return current mpr willingness
 */
static INLINE enum rfc5444_willingness_values
nhdp_interface_get_mpr_willingness(struct nhdp_interface *interf) {
  if (interf->mpr_willingness == RFC5444_WILLINGNESS_UNDEFINED) {
    return interf->mpr_willingness_default;
  }
  return interf->mpr_willingness;
}

#endif /* NHDP_INTERFACES_H_ */
