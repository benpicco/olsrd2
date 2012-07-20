
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
#include "core/olsr_memcookie.h"
#include "core/olsr_timer.h"
#include "tools/olsr_cfg.h"
#include "nhdp/nhdp.h"
#include "nhdp/nhdp_interfaces.h"

static void _cb_remove_addr(void *ptr);
static void _cb_remove_interf(void *ptr);
static void _cb_generate_hello(void *ptr);
static void _cb_cfg_interface_changed(void);
static void _cb_interface_event(struct olsr_rfc5444_interface_listener *);

/* global tree of nhdp interfaces */
struct avl_tree nhdp_interface_tree;

/* memory and timers for nhdp interface objects */
static struct olsr_memcookie_info _interface_info = {
  .name = "NHDP Interface",
  .size = sizeof(struct nhdp_interface),
};

static struct olsr_memcookie_info _addr_info = {
  .name = "NHDP interface address",
  .size = sizeof(struct nhdp_interface_addr),
};

static struct olsr_timer_info _interface_hello_timer = {
  .name = "NHDP hello timer",
  .periodic = true,
  .callback = _cb_generate_hello,
};

static struct olsr_timer_info _interface_vtime_timer = {
  .name = "NHDP interface hold timer",
  .callback = _cb_remove_interf,
};

static struct olsr_timer_info _addr_vtime_timer = {
  .name = "NHDP interface address hold timer",
  .callback = _cb_remove_addr,
};

/* additional configuration options for interface section */
static struct cfg_schema_section _interface_section = {
  .type = CFG_INTERFACE_SECTION,
  .mode = CFG_SSMODE_NAMED,
  .cb_delta_handler = _cb_cfg_interface_changed,
};

static struct cfg_schema_entry _interface_entries[] = {
    CFG_MAP_CLOCK_MIN(nhdp_interface, hello_vtime, "hellp-validity", "6.0",
        "Validity time for NHDP Hello Messages", 100),
    CFG_MAP_CLOCK_MIN(nhdp_interface, hello_itime, "hello-interval", "2.0",
        "Time interval between two NHDP Hello Messages", 100),
};

/* pointer to default rfc5444 protocol */
static struct olsr_rfc5444_protocol *_protocol;

// TODO: add callback provider

/**
 * Initialize NHDP interface subsystem
 */
void
nhdp_interfaces_init(void) {
  avl_init(&nhdp_interface_tree, avl_comp_strcasecmp, false, NULL);
  olsr_memcookie_add(&_interface_info);
  olsr_memcookie_add(&_addr_info);

  /* default protocol should be always available */
  _protocol = olsr_rfc5444_add_protocol(RFC5444_PROTOCOL);
  assert (_protocol);

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
    _cb_remove_interf(interf);
  }

  cfg_schema_remove_section(olsr_cfg_get_schema(), &_interface_section);

  olsr_memcookie_remove(&_addr_info);
  olsr_memcookie_remove(&_interface_info);

  olsr_rfc5444_remove_protocol(_protocol);
}

/**
 * Add a nhdp interface
 * @param name name of interface
 * @return pointer to nhdp interface, NULL if out of memory
 */
struct nhdp_interface *
nhdp_interface_add(const char *name) {
  struct nhdp_interface *interf;

  interf = avl_find_element(&nhdp_interface_tree, name, interf, _node);
  if (interf == NULL) {
    interf = olsr_memcookie_malloc(&_interface_info);
    if (interf == NULL) {
      OLSR_WARN(LOG_NHDP, "No memory left for NHDP interface");
      return NULL;
    }

    interf->rfc5444_if.cb_interface_changed = _cb_interface_event;
    if (!olsr_rfc5444_add_interface(_protocol, &interf->rfc5444_if, name)) {
      olsr_memcookie_free(&_interface_info, interf);
      OLSR_WARN(LOG_NHDP, "Cannot allocate rfc5444 interface for %s", name);
      return NULL;
    }

    /* initialize timers */
    interf->_hello_timer.info = &_interface_hello_timer;
    interf->_hello_timer.cb_context = interf;
    interf->_cleanup_timer.info = &_interface_vtime_timer;
    interf->_cleanup_timer.cb_context = interf;

    /* initialize tree for addresses */
    avl_init(&interf->_addrs, avl_comp_netaddr, false, NULL);

    /* hook into global interface tree */
    interf->_node.key = interf->rfc5444_if.interface->name;
    avl_insert(&nhdp_interface_tree, &interf->_node);
  }
  else if (!interf->active) {
    olsr_timer_stop(&interf->_cleanup_timer);
    interf->active = true;
  }
  return interf;
}

/**
 * Mark a nhdp interface as removed and start cleanup timer
 * @param interf pointer to nhdp interface
 */
void
nhdp_interface_remove(struct nhdp_interface *interf) {
  if (!interf->active) {
    return;
  }

  interf->active = false;

  if (interf->hello_vtime) {
    /* activate cleanup timer */
    olsr_timer_set(&interf->_cleanup_timer, interf->hello_vtime);
  }
  else {
    /* remove interface immediately if possible */
    _cb_remove_interf(interf);
  }
}

/**
 * Add a nhdp interface address to an interface
 * @param interf pointer to nhdp interface
 * @param addr address to add to interface
 * @return pointer to ndhp interface address, NULL if out of memory
 */
struct nhdp_interface_addr *
nhdp_interface_addr_add(struct nhdp_interface *interf, struct netaddr *addr) {
  struct nhdp_interface_addr *if_addr;

  if_addr = avl_find_element(&interf->_addrs, addr, if_addr, _node);
  if (if_addr == NULL) {
    if_addr = olsr_memcookie_malloc(&_addr_info);
    if (if_addr == NULL) {
      OLSR_WARN(LOG_NHDP, "No memory left for NHDP interface address");
      return NULL;
    }

    memcpy(&if_addr->addr, addr, sizeof(*addr));

    if_addr->active = true;
    if_addr->interf = interf;

    if_addr->_cleanup_timer.info = &_addr_vtime_timer;
    if_addr->_cleanup_timer.cb_context = if_addr;

    if_addr->_node.key = &if_addr->addr;
    avl_insert(&interf->_addrs, &interf->_node);
  }
  else if (!if_addr->active){
    olsr_timer_stop(&if_addr->_cleanup_timer);
    if_addr->active = true;
  }
  return if_addr;
}

/**
 * Mark a nhdp interface address as removed and start cleanup timer
 * @param addr pointer to nhdp interface address
 */
void
nhdp_interface_addr_remove(struct nhdp_interface_addr *addr) {
  if (!addr->active) {
    return;
  }

  addr->active = false;

  if (addr->interf->hello_vtime) {
    /* activate cleanup timer */
    olsr_timer_set(&addr->_cleanup_timer, addr->interf->hello_vtime);
  }
  else {
    /* remove interface address immediately if possible */
    _cb_remove_addr(addr);
  }
}

/**
 * Cleanup and remove a nhdp interface and its addresses
 * @param ptr pointer to nhdp interface
 */
static void
_cb_remove_interf(void *ptr) {
  struct nhdp_interface *interf;
  struct nhdp_interface_addr *addr, *addr_it;

  interf = ptr;

  olsr_timer_stop(&interf->_cleanup_timer);

  avl_for_each_element_safe(&interf->_addrs, addr, _node, addr_it) {
    _cb_remove_addr(addr);
  }

  olsr_rfc5444_remove_interface(interf->rfc5444_if.interface, &interf->rfc5444_if);
  avl_remove(&nhdp_interface_tree, &interf->_node);
  olsr_memcookie_free(&_interface_info, interf);
}

/**
 * Cleanup and remove an address from a nhdp interface
 * @param ptr pointer to nhdp interface address
 */
static void
_cb_remove_addr(void *ptr) {
  struct nhdp_interface_addr *addr;

  addr = ptr;

  olsr_timer_stop(&addr->_cleanup_timer);
  avl_remove(&addr->interf->_addrs, &addr->_node);
  olsr_memcookie_free(&_addr_info, addr);
}

/**
 * Generate Hellos on an interface
 * @param ptr pointer to nhdp interface
 */
static void
_cb_generate_hello(void *ptr) {
  struct nhdp_interface *interf;

  interf = ptr;

  /* send both IPv4 and IPv6 (if active) */
  olsr_rfc5444_send(interf->rfc5444_if.interface->multicast4, RFC5444_MSGTYPE_HELLO);
  olsr_rfc5444_send(interf->rfc5444_if.interface->multicast6, RFC5444_MSGTYPE_HELLO);
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

  /* get interface */
  interf = nhdp_interface_get(name);

  if (_interface_section.post == NULL) {
    /* section was removed */
    if (interf != NULL) {
      nhdp_interface_remove(interf);
    }
    return;
  }

  if (interf == NULL) {
    interf = nhdp_interface_add(name);
  }

  if (cfg_schema_tobin(interf, _interface_section.post,
      _interface_entries, ARRAYSIZE(_interface_entries))) {
    OLSR_WARN(LOG_NHDP, "Cannot convert timing settings for Hello.");
    return;
  }

  /* reset hello generation frequency */
  olsr_timer_set(&interf->_hello_timer, interf->hello_itime);
}

/**
 * Settings of an interface changed, fix the nhdp addresses if necessary
 * @param ifl
 */
static void
_cb_interface_event(struct olsr_rfc5444_interface_listener *ifl) {
  struct nhdp_interface *interf;
  struct nhdp_interface_addr *addr, *addr_it;
  struct netaddr ip;

  interf = container_of(ifl, struct nhdp_interface, rfc5444_if);

  /* mark all old socket addresses */
  avl_for_each_element_safe(&interf->_addrs, addr, _node, addr_it) {
    if (addr->socket_addr) {
      addr->_might_be_removed = true;
    }
  }

  /* create new socket addresses */
  if (olsr_packet_managed_is_active(&interf->rfc5444_if.interface->_socket, AF_INET)) {
    netaddr_from_socket(&ip, &interf->rfc5444_if.interface->_socket.socket_v4.local_socket);

    addr = nhdp_interface_addr_add(interf, &ip);
    addr->socket_addr = true;
    addr->_might_be_removed = false;
  }
  if (olsr_packet_managed_is_active(&interf->rfc5444_if.interface->_socket, AF_INET)) {
    netaddr_from_socket(&ip, &interf->rfc5444_if.interface->_socket.socket_v6.local_socket);

    addr = nhdp_interface_addr_add(interf, &ip);
    addr->socket_addr = true;
    addr->_might_be_removed = false;
  }

  /* remove outdated socket addresses */
  avl_for_each_element_safe(&interf->_addrs, addr, _node, addr_it) {
    if (addr->_might_be_removed) {
      addr->_might_be_removed = false;
      addr->socket_addr = false;
      nhdp_interface_addr_remove(addr);
    }
  }
}
