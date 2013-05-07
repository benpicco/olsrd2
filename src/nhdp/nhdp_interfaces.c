
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

#include <assert.h>

#include "common/common_types.h"
#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/container_of.h"
#include "common/netaddr.h"
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_interface.h"
#include "core/olsr_logging.h"
#include "core/olsr_class.h"
#include "common/netaddr_acl.h"
#include "core/olsr_timer.h"
#include "tools/olsr_cfg.h"
#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_writer.h"

/* Prototypes of local functions */
static struct nhdp_interface_addr *_addr_add(
    struct nhdp_interface *, struct netaddr *addr);
static void _addr_remove(struct nhdp_interface_addr *addr, uint64_t vtime);
static void _cb_remove_addr(void *ptr);

static int avl_comp_ifaddr(const void *k1, const void *k2);

static void _cb_generate_hello(void *ptr);
static void _cb_interface_event(struct olsr_rfc5444_interface_listener *, bool);

/* global tree of nhdp interfaces, filters and addresses */
struct avl_tree nhdp_interface_tree;
struct avl_tree nhdp_ifaddr_tree;

/* memory and timers for nhdp interface objects */
static struct olsr_class _interface_info = {
  .name = NHDP_INTERFACE,
  .size = sizeof(struct nhdp_interface),
};

static struct olsr_timer_info _interface_hello_timer = {
  .name = "NHDP hello timer",
  .periodic = true,
  .callback = _cb_generate_hello,
};

static struct olsr_class _addr_info = {
  .name = NHDP_INTERFACE_ADDRESS,
  .size = sizeof(struct nhdp_interface_addr),
};

static struct olsr_timer_info _removed_address_hold_timer = {
  .name = "NHDP interface removed address hold timer",
  .callback = _cb_remove_addr,
};

/* other global variables */
static struct olsr_rfc5444_protocol *_protocol;

/**
 * Initialize NHDP interface subsystem
 */
void
nhdp_interfaces_init(struct olsr_rfc5444_protocol *p) {
  avl_init(&nhdp_interface_tree, avl_comp_strcasecmp, false);
  avl_init(&nhdp_ifaddr_tree, avl_comp_ifaddr, true);
  olsr_class_add(&_interface_info);
  olsr_class_add(&_addr_info);
  olsr_timer_add(&_interface_hello_timer);
  olsr_timer_add(&_removed_address_hold_timer);

  /* default protocol should be always available */
  _protocol = p;
}

/**
 * Cleanup all allocated resources for nhdp interfaces
 */
void
nhdp_interfaces_cleanup(void) {
  struct nhdp_interface *interf, *if_it;

  avl_for_each_element_safe(&nhdp_interface_tree, interf, _node, if_it) {
    nhdp_interface_remove(interf);
  }

  olsr_timer_remove(&_interface_hello_timer);
  olsr_timer_remove(&_removed_address_hold_timer);
  olsr_class_remove(&_interface_info);
  olsr_class_remove(&_addr_info);
}

/**
 * Recalculates if IPv4 or IPv6 should be used on an interface
 * for flooding messages.
 * @param interf pointer to nhdp interface
 */
void
nhdp_interface_update_status(struct nhdp_interface *interf) {
  struct nhdp_link *lnk;

  interf->use_ipv4_for_flooding = false;
  interf->use_ipv6_for_flooding = false;

  list_for_each_element(&interf->_links, lnk, _if_node) {
    if (lnk->status != NHDP_LINK_SYMMETRIC) {
      /* link is not symmetric */
      continue;
    }

    /* originator can be AF_UNSPEC, so we cannot use "else" */
    if (netaddr_get_address_family(&lnk->neigh->originator) == AF_INET
        && lnk->dualstack_partner == NULL) {
      /* ipv4 neighbor without dualstack */
      interf->use_ipv4_for_flooding = true;
    }
    if (netaddr_get_address_family(&lnk->neigh->originator) == AF_INET6
        || lnk->dualstack_partner != NULL) {
      /* ipv6 neighbor or dualstack neighbor */
      interf->use_ipv6_for_flooding = true;
    }
  }
}

/**
 * Add a nhdp interface
 * @param name name of interface
 * @return pointer to nhdp interface, NULL if out of memory
 */
struct nhdp_interface *
nhdp_interface_add(const char *name) {
  struct nhdp_interface *interf;

  OLSR_DEBUG(LOG_NHDP, "Add interface to NHDP_interface tree: %s", name);

  interf = avl_find_element(&nhdp_interface_tree, name, interf, _node);
  if (interf == NULL) {
    interf = olsr_class_malloc(&_interface_info);
    if (interf == NULL) {
      OLSR_WARN(LOG_NHDP, "No memory left for NHDP interface");
      return NULL;
    }

    interf->rfc5444_if.cb_interface_changed = _cb_interface_event;
    if (!olsr_rfc5444_add_interface(_protocol, &interf->rfc5444_if, name)) {
      olsr_class_free(&_interface_info, interf);
      OLSR_WARN(LOG_NHDP, "Cannot allocate rfc5444 interface for %s", name);
      return NULL;
    }

    /* initialize timers */
    interf->_hello_timer.info = &_interface_hello_timer;
    interf->_hello_timer.cb_context = interf;

    /* hook into global interface tree */
    interf->_node.key = interf->rfc5444_if.interface->name;
    avl_insert(&nhdp_interface_tree, &interf->_node);

    /* init address tree */
    avl_init(&interf->_if_addresses, avl_comp_netaddr, false);

    /* init link list */
    list_init_head(&interf->_links);

    /* init link address tree */
    avl_init(&interf->_link_addresses, avl_comp_netaddr, false);

    /* init originator tree */
    avl_init(&interf->_link_originators, avl_comp_netaddr, false);

    /* trigger event */
    olsr_class_event(&_interface_info, interf, OLSR_OBJECT_ADDED);
  }
  return interf;
}

/**
 * Mark a nhdp interface as removed and start cleanup timer
 * @param interf pointer to nhdp interface
 */
void
nhdp_interface_remove(struct nhdp_interface *interf) {
  struct nhdp_interface_addr *addr, *a_it;
  struct nhdp_link *lnk, *l_it;

  /* trigger event */
  olsr_class_event(&_interface_info, interf, OLSR_OBJECT_REMOVED);

  /* free filter */
  netaddr_acl_remove(&interf->ifaddr_filter);

  olsr_timer_stop(&interf->_hello_timer);

  avl_for_each_element_safe(&interf->_if_addresses, addr, _if_node, a_it) {
    _cb_remove_addr(addr);
  }

  list_for_each_element_safe(&interf->_links, lnk, _if_node, l_it) {
    nhdp_db_link_remove(lnk);
  }

  olsr_rfc5444_remove_interface(interf->rfc5444_if.interface, &interf->rfc5444_if);
  avl_remove(&nhdp_interface_tree, &interf->_node);
  olsr_class_free(&_interface_info, interf);
}

void
nhdp_interface_apply_settings(struct nhdp_interface *interf) {
  /* parse ip address list again and apply ACL */
  _cb_interface_event(&interf->rfc5444_if, false);

  /* reset hello generation frequency */
  olsr_timer_set(&interf->_hello_timer, interf->refresh_interval);

  /* just copy hold time for now */
  interf->l_hold_time = interf->h_hold_time;
  interf->n_hold_time = interf->l_hold_time;
  interf->i_hold_time = interf->n_hold_time;
}

/**
 * Add a nhdp interface address to an interface
 * @param interf pointer to nhdp interface
 * @param if_addr address to add to interface
 * @return pointer to ndhp interface address, NULL if out of memory
 */
static struct nhdp_interface_addr *
_addr_add(struct nhdp_interface *interf, struct netaddr *addr) {
  struct nhdp_interface_addr *if_addr;
#if OONF_LOGGING_LEVEL >= OONF_LOGGING_LEVEL_DEBUG
  struct netaddr_str buf;
#endif

  OLSR_DEBUG(LOG_NHDP, "Add address %s in NHDP_interface_address tree",
      netaddr_to_string(&buf, addr));

  if_addr = avl_find_element(&interf->_if_addresses, addr, if_addr, _if_node);
  if (if_addr == NULL) {
    if_addr = olsr_class_malloc(&_addr_info);
    if (if_addr == NULL) {
      OLSR_WARN(LOG_NHDP, "No memory left for NHDP interface address");
      return NULL;
    }

    memcpy(&if_addr->if_addr, addr, sizeof(*addr));

    if_addr->interf = interf;

    /* hook if-addr into interface and global tree */
    if_addr->_global_node.key = &if_addr->if_addr;
    avl_insert(&nhdp_ifaddr_tree, &if_addr->_global_node);

    if_addr->_if_node.key = &if_addr->if_addr;
    avl_insert(&interf->_if_addresses, &if_addr->_if_node);

    /* initialize validity timer for removed addresses */
    if_addr->_vtime.info = &_removed_address_hold_timer;
    if_addr->_vtime.cb_context = if_addr;

    /* trigger event */
    olsr_class_event(&_addr_info, if_addr, OLSR_OBJECT_ADDED);
  }
  else {
    if_addr->_to_be_removed = false;
  }
  return if_addr;
}

/**
 * Mark an interface address as removed
 * @param addr nhdp interface address
 * @param vtime time in milliseconds until address should be removed from db
 */
static void
_addr_remove(struct nhdp_interface_addr *addr, uint64_t vtime) {
#if OONF_LOGGING_LEVEL >= OONF_LOGGING_LEVEL_DEBUG
  struct netaddr_str buf;
#endif

  OLSR_DEBUG(LOG_NHDP, "Remove %s from NHDP interface %s",
      netaddr_to_string(&buf, &addr->if_addr), nhdp_interface_get_name(addr->interf));

  addr->removed = true;
  olsr_timer_set(&addr->_vtime, vtime);
}

/**
 * Callback triggered when an address from a nhdp interface
 *  should be removed from the db
 * @param ptr pointer to nhdp interface address
 */
static void
_cb_remove_addr(void *ptr) {
  struct nhdp_interface_addr *addr;

  addr = ptr;

  /* trigger event */
  olsr_class_event(&_addr_info, addr, OLSR_OBJECT_REMOVED);

  olsr_timer_stop(&addr->_vtime);
  avl_remove(&nhdp_ifaddr_tree, &addr->_global_node);
  avl_remove(&addr->interf->_if_addresses, &addr->_if_node);
  olsr_class_free(&_addr_info, addr);
}

/**
 * AVL tree comparator for netaddr objects.
 * @param k1 pointer to key 1
 * @param k2 pointer to key 2
 * @param ptr not used in this comparator
 * @return +1 if k1>k2, -1 if k1<k2, 0 if k1==k2
 */
static int
avl_comp_ifaddr(const void *k1, const void *k2) {
  const struct netaddr *n1 = k1;
  const struct netaddr *n2 = k2;

  if (netaddr_get_address_family(n1) > netaddr_get_address_family(n2)) {
    return 1;
  }
  if (netaddr_get_address_family(n1) < netaddr_get_address_family(n2)) {
    return -1;
  }

  return memcmp(n1, n2, 16);
}

/**
 * Callback triggered to generate a Hello on an interface
 * @param ptr pointer to nhdp interface
 */
static void
_cb_generate_hello(void *ptr) {
  nhdp_writer_send_hello(ptr);
}

/**
 * Configuration of an interface changed,
 *  fix the nhdp addresses if necessary
 * @param ifl olsr rfc5444 interface listener
 * @param changed true if socket address changed
 */
static void
_cb_interface_event(struct olsr_rfc5444_interface_listener *ifl,
    bool changed __attribute__((unused))) {
  struct nhdp_interface *interf;
  struct nhdp_interface_addr *addr, *addr_it;
  struct olsr_interface *olsr_interf;
  struct netaddr ip;
  bool ipv4, ipv6;
  size_t i;

  OLSR_DEBUG(LOG_NHDP, "NHDP Interface change event: %s", ifl->interface->name);

  interf = container_of(ifl, struct nhdp_interface, rfc5444_if);

  /* mark all old addresses */
  avl_for_each_element_safe(&interf->_if_addresses, addr, _if_node, addr_it) {
    addr->_to_be_removed = true;
  }

  olsr_interf = olsr_rfc5444_get_core_interface(ifl->interface);

  ipv4 = olsr_rfc5444_is_target_active(interf->rfc5444_if.interface->multicast4);
  ipv6 = olsr_rfc5444_is_target_active(interf->rfc5444_if.interface->multicast6);

  if (olsr_interf->data.up) {
    /* handle local socket main addresses */
    if (ipv4) {
      OLSR_DEBUG(LOG_NHDP, "NHDP Interface %s is ipv4", ifl->interface->name);
        netaddr_from_socket(&ip, &interf->rfc5444_if.interface->_socket.socket_v4.local_socket);
      _addr_add(interf, &ip);
    }
    if (ipv6) {
      OLSR_DEBUG(LOG_NHDP, "NHDP Interface %s is ipv6", ifl->interface->name);
      netaddr_from_socket(&ip, &interf->rfc5444_if.interface->_socket.socket_v6.local_socket);
      _addr_add(interf, &ip);
    }

    /* get all socket addresses that are matching the filter */
    for (i = 0; i<olsr_interf->data.addrcount; i++) {
      struct netaddr *ifaddr = &olsr_interf->data.addresses[i];

      if (netaddr_get_address_family(ifaddr) == AF_INET && !ipv4) {
        /* ignore IPv4 addresses if ipv4 socket is not up*/
        continue;
      }
      if (netaddr_get_address_family(ifaddr) == AF_INET6 && !ipv6) {
        /* ignore IPv6 addresses if ipv6 socket is not up*/
        continue;
      }

      /* check if IP address fits to ACL */
      if (netaddr_acl_check_accept(&interf->ifaddr_filter, ifaddr)) {
        _addr_add(interf, ifaddr);
      }
    }
  }

  /* remove outdated socket addresses */
  avl_for_each_element_safe(&interf->_if_addresses, addr, _if_node, addr_it) {
    if (addr->_to_be_removed && !addr->removed) {
      addr->_to_be_removed = false;
      _addr_remove(addr, interf->i_hold_time);
    }
  }
}
