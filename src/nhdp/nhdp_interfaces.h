
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

#include "common/common_types.h"
#include "common/avl.h"
#include "common/netaddr.h"
#include "core/olsr_interface.h"
#include "core/olsr_timer.h"
#include "tools/olsr_rfc5444.h"

struct nhdp_interface {
  struct olsr_rfc5444_interface_listener rfc5444_if;

  bool active;

  uint64_t hello_itime, hello_vtime;

  struct olsr_timer_entry _hello_timer;
  struct olsr_timer_entry _cleanup_timer;

  struct avl_node _node;
  struct avl_tree _addrs;
};

struct nhdp_interface_addr {
  struct netaddr addr;
  bool active;
  bool socket_addr;

  struct nhdp_interface *interf;

  bool _might_be_removed;
  struct avl_node _node;
  struct olsr_timer_entry _cleanup_timer;
};

#define NHDP_FOR_ALL_ACTIVE_INTERFACE_ADDRS(interf, addr) avl_for_each_element(&interf->_addrs, addr, _node) if (addr->active)
#define NHDP_FOR_ALL_INACTIVE_INTERFACE_ADDRS(interf, addr) avl_for_each_element(&interf->_addrs, addr, _node) if (!addr->active)

EXPORT extern struct avl_tree nhdp_interface_tree;

void nhdp_interfaces_init(void);
void nhdp_interfaces_cleanup(void);

struct nhdp_interface *nhdp_interface_add(const char *name);
void nhdp_interface_remove(struct nhdp_interface *);

struct nhdp_interface_addr *nhdp_interface_addr_add(
    struct nhdp_interface *, struct netaddr *);
void nhdp_interface_addr_remove(struct nhdp_interface_addr *);

/**
 *
 * @param
 * @return
 */
static INLINE struct nhdp_interface *
nhdp_interface_get(const char *name) {
  struct nhdp_interface *interf;

  return avl_find_element(&nhdp_interface_tree, name, interf, _node);
}

/**
 *
 * @param interf
 * @return
 */
static INLINE const char *
nhdp_interface_get_name(struct nhdp_interface *interf) {
  return interf->_node.key;
}

#endif /* NHDP_INTERFACES_H_ */
