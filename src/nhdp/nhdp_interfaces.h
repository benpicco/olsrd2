
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

#define NHDP_INTERFACE         "nhdp_interf"
#define NHDP_INTERFACE_ADDRESS "nhdp_iaddr"

/**
 * nhdp_interface represents a local interface participating in the mesh network
 */
struct nhdp_interface {
  /* listener for interface events */
  struct olsr_rfc5444_interface_listener rfc5444_if;

  /* interval between two hellos sent through this interface */
  uint64_t refresh_interval;

  /* See RFC 6130, 5.3.2 and 5.4.1 */
  uint64_t h_hold_time;
  uint64_t l_hold_time;
  uint64_t n_hold_time;
  uint64_t i_hold_time;

  /* ACL for incoming HELLO messages through this interface */
  struct olsr_netaddr_acl ifaddr_filter;

  /*
   * true if this interface has a neighbor that should be reached through
   * IPv4/IPv6 for flooding.
   */
  bool use_ipv4_for_flooding;
  bool use_ipv6_for_flooding;

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

  /* tree of originator addresses of links (nhdp_link objects */
  struct avl_tree _link_originators;
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

EXPORT void nhdp_interface_update_status(struct nhdp_interface *);

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
nhdp_interface_get_name(const struct nhdp_interface *interf) {
  return interf->_node.key;
}

/**
 * @param interf nhdp interface
 * @param addr network address
 * @return nhdp interface address
 */
static INLINE struct nhdp_interface_addr *
nhdp_interface_addr_if_get(const struct nhdp_interface *interf, const struct netaddr *addr) {
  struct nhdp_interface_addr *iaddr;

  return avl_find_element(&interf->_if_addresses, addr, iaddr, _if_node);
}

/**
 * @param network address
 * @return nhdp interface address
 */
static INLINE struct nhdp_interface_addr *
nhdp_interface_addr_global_get(const struct netaddr *addr) {
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
nhdp_interface_get_link_addr(const struct nhdp_interface *interf, const struct netaddr *addr) {
  struct nhdp_laddr *laddr;

  return avl_find_element(&interf->_link_addresses, addr, laddr, _if_node);
}

/**
 * @param interf local nhdp interface
 * @param originator originator address
 * @return corresponding nhdp link, NULL if not found
 */
static INLINE struct nhdp_link *
nhdp_interface_link_get_by_originator(
    const struct nhdp_interface *interf, const struct netaddr *originator) {
  struct nhdp_link *lnk;
  return avl_find_element(&interf->_link_originators, originator, lnk, _originator_node);
}

/**
 * @param nhdp_if pointer to nhdp interface
 * @return pointer to corresponding olsr_interface
 */
static inline struct olsr_interface *
nhdp_interface_get_coreif(struct nhdp_interface *nhdp_if) {
  return olsr_rfc5444_get_core_interface(nhdp_if->rfc5444_if.interface);
}

#endif /* NHDP_INTERFACES_H_ */
