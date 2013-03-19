
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
#include "config/cfg_schema.h"
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_interface.h"
#include "core/olsr_logging.h"
#include "core/olsr_class.h"
#include "core/olsr_netaddr_acl.h"
#include "core/olsr_timer.h"
#include "tools/olsr_cfg.h"
#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_interfaces.h"

/* Prototypes of local functions */
static struct nhdp_interface *_interface_add(const char *name);
static void _interface_remove(struct nhdp_interface *interf);
static struct nhdp_interface_addr *_addr_add(
    struct nhdp_interface *, struct netaddr *addr);
static void _addr_remove(struct nhdp_interface_addr *addr, uint64_t vtime);
static void _cb_remove_addr(void *ptr);

static int avl_comp_ifaddr(const void *k1, const void *k2);

static void _cb_generate_hello(void *ptr);
static void _cb_cfg_interface_changed(void);
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


/* additional configuration options for interface section */
const char *NHDP_INTERFACE_MODES[] = {
  [NHDP_IPV4] = "ipv4",
  [NHDP_IPV6] = "ipv6",
  [NHDP_DUAL] = "dual",
};

static struct cfg_schema_section _interface_section = {
  .type = CFG_INTERFACE_SECTION,
  .mode = CFG_SSMODE_NAMED,
  .cb_delta_handler = _cb_cfg_interface_changed,
};

static struct cfg_schema_entry _interface_entries[] = {
  CFG_MAP_CHOICE(nhdp_interface, mode, "mode",
      "ipv4", "Mode of interface (ipv4/6/dual)", NHDP_INTERFACE_MODES),
  CFG_MAP_ACL_V46(nhdp_interface, ifaddr_filter, "ifaddr_filter", ACL_DEFAULT_REJECT,
      "Filter for ip interface addresses that should be included in HELLO messages"),
  CFG_MAP_CLOCK_MIN(nhdp_interface, h_hold_time, "hello-validity", "6.0",
    "Validity time for NHDP Hello Messages", 100),
  CFG_MAP_CLOCK_MIN(nhdp_interface, refresh_interval, "hello-interval", "2.0",
    "Time interval between two NHDP Hello Messages", 100),
};

/* other global variables */
static struct olsr_rfc5444_protocol *_protocol;

// TODO: add callback provider

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

  /* add additional configuration for interface section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_interface_section,
      _interface_entries, ARRAYSIZE(_interface_entries));
}

/**
 * Cleanup all allocated resources for nhdp interfaces
 */
void
nhdp_interfaces_cleanup(void) {
  struct nhdp_interface *interf, *if_it;

  avl_for_each_element_safe(&nhdp_interface_tree, interf, _node, if_it) {
    _interface_remove(interf);
  }

  cfg_schema_remove_section(olsr_cfg_get_schema(), &_interface_section);

  olsr_timer_remove(&_interface_hello_timer);
  olsr_timer_remove(&_removed_address_hold_timer);
  olsr_class_remove(&_interface_info);
  olsr_class_remove(&_addr_info);
}

/**
 * Updates the neigh_v4/6only variables of an interface
 * @param interf nhdp interface
 */
void
nhdp_interfaces_update_neigh_addresstype(struct nhdp_interface *interf) {
  struct nhdp_link *lnk;
  struct nhdp_laddr *addr;
  bool v4only;

  interf->neigh_onlyv4 = false;

  list_for_each_element(&interf->_links, lnk, _if_node) {
    v4only = true;

    avl_for_each_element(&lnk->_addresses, addr, _link_node) {
      if (netaddr_get_address_family(&addr->link_addr) == AF_INET6) {
        v4only = false;
        break;
      }
    }

    interf->neigh_onlyv4 |= v4only;
  }
}

/**
 * Add a nhdp interface
 * @param name name of interface
 * @return pointer to nhdp interface, NULL if out of memory
 */
static struct nhdp_interface *
_interface_add(const char *name) {
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

    /* trigger event */
    olsr_class_event(&_interface_info, interf, OLSR_OBJECT_ADDED);
  }
  return interf;
}

/**
 * Mark a nhdp interface as removed and start cleanup timer
 * @param interf pointer to nhdp interface
 */
static void
_interface_remove(struct nhdp_interface *interf) {
  struct nhdp_interface_addr *addr, *a_it;
  struct nhdp_link *lnk, *l_it;

  /* trigger event */
  olsr_class_event(&_interface_info, interf, OLSR_OBJECT_REMOVED);

  /* free filter */
  olsr_acl_remove(&interf->ifaddr_filter);

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

/**
 * Add a nhdp interface address to an interface
 * @param interf pointer to nhdp interface
 * @param if_addr address to add to interface
 * @return pointer to ndhp interface address, NULL if out of memory
 */
static struct nhdp_interface_addr *
_addr_add(struct nhdp_interface *interf, struct netaddr *addr) {
  struct nhdp_interface_addr *if_addr;
  struct netaddr_str buf;

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
  struct netaddr_str buf;

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
  struct nhdp_interface *interf;
  enum rfc5444_result result;
#if OONF_LOGGING_LEVEL >= OONF_LOGGING_LEVEL_WARN
  struct netaddr_str buf;
#endif

  interf = ptr;

  OLSR_DEBUG(LOG_NHDP, "Sending Hello to interface %s (mode=%d)",
      nhdp_interface_get_name(interf), interf->mode);

  /* send IPv4 if this interface is IPv4-only or if this interface has such neighbors */
  if (interf->mode == NHDP_IPV4 || interf->neigh_onlyv4) {
    result = olsr_rfc5444_send(interf->rfc5444_if.interface->multicast4, RFC5444_MSGTYPE_HELLO);
    if (result < 0) {
      OLSR_WARN(LOG_NHDP, "Could not send NHDP message to %s: %s (%d)",
          netaddr_to_string(&buf, &interf->rfc5444_if.interface->multicast4->dst), rfc5444_strerror(result), result);
    }
  }

  /* send IPV6 unless this interface is in IPv4-only mode */
  if (interf->mode != NHDP_IPV4) {
    result = olsr_rfc5444_send(interf->rfc5444_if.interface->multicast6, RFC5444_MSGTYPE_HELLO);
    if (result < 0) {
      OLSR_WARN(LOG_NHDP, "Could not send NHDP message to %s: %s (%d)",
          netaddr_to_string(&buf, &interf->rfc5444_if.interface->multicast6->dst), rfc5444_strerror(result), result);
    }
  }
}

/**
 * Configuration has changed, handle the changes
 */
static void
_cb_cfg_interface_changed(void) {
  struct nhdp_interface *interf;
  const char *name;

  /* get section name */
  if (_interface_section.pre) {
    name = _interface_section.pre->name;
  }
  else {
    name = _interface_section.post->name;
  }

  OLSR_DEBUG(LOG_NHDP, "Configuration of NHDP interface %s changed", name);

  /* get interface */
  interf = nhdp_interface_get(name);

  if (_interface_section.post == NULL) {
    /* section was removed */
    if (interf != NULL) {
      _interface_remove(interf);
    }
    return;
  }

  if (interf == NULL) {
    interf = _interface_add(name);
  }

  if (cfg_schema_tobin(interf, _interface_section.post,
      _interface_entries, ARRAYSIZE(_interface_entries))) {
    OLSR_WARN(LOG_NHDP, "Cannot convert NHDP configuration for interface.");
    return;
  }

  /* trigger interface address change handler */
  _cb_interface_event(&interf->rfc5444_if, false);

  /* reset hello generation frequency */
  olsr_timer_set(&interf->_hello_timer, interf->refresh_interval);

  /* just copy hold time for now */
  interf->l_hold_time = interf->h_hold_time;
  interf->n_hold_time = interf->l_hold_time;
  interf->i_hold_time = interf->n_hold_time;
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
  size_t i;

  OLSR_DEBUG(LOG_NHDP, "NHDP Interface change event: %s", ifl->interface->name);

  interf = container_of(ifl, struct nhdp_interface, rfc5444_if);

  /* mark all old addresses */
  avl_for_each_element_safe(&interf->_if_addresses, addr, _if_node, addr_it) {
    addr->_to_be_removed = true;
  }

  olsr_interf = olsr_rfc5444_get_core_interface(ifl->interface);
  if (olsr_interf->data.up) {
    /* handle local socket main addresses */
    if (interf->mode != NHDP_IPV6) {
      OLSR_DEBUG(LOG_NHDP, "NHDP Interface %s is ipv4", ifl->interface->name);
      netaddr_from_socket(&ip, &interf->rfc5444_if.interface->_socket.socket_v4.local_socket);
      _addr_add(interf, &ip);
    }
    if (interf->mode != NHDP_IPV4) {
      OLSR_DEBUG(LOG_NHDP, "NHDP Interface %s is ipv6", ifl->interface->name);
      netaddr_from_socket(&ip, &interf->rfc5444_if.interface->_socket.socket_v6.local_socket);
      _addr_add(interf, &ip);
    }

    /* get all socket addresses that are matching the filter */
    for (i = 0; i<olsr_interf->data.addrcount; i++) {
      struct netaddr *ifaddr = &olsr_interf->data.addresses[i];

      if (netaddr_get_address_family(ifaddr) == AF_INET && interf->mode ==  NHDP_IPV6) {
        /* ignore IPv6 addresses in IPv4 mode */
        continue;
      }
      if (netaddr_get_address_family(ifaddr) == AF_INET6 && interf->mode == NHDP_IPV4) {
        /* ignore IPv4 addresses in IPv6 mode */
        continue;
      }

      /* check if IP address fits to ACL */
      if (olsr_acl_check_accept(&interf->ifaddr_filter, ifaddr)) {
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
