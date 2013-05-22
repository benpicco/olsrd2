
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

#ifndef OONFV2_LAN_H_
#define OONFV2_LAN_H_

#include "common/avl.h"
#include "common/common_types.h"
#include "common/netaddr.h"

#include "nhdp/nhdp.h"

/* per-domain data for locally attached networks */
struct olsrv2_lan_domaindata {
  uint32_t outgoing_metric;
  uint8_t distance;
  bool active;
};

/* one locally attached network */
struct olsrv2_lan_entry {
  struct netaddr prefix;

  struct olsrv2_lan_domaindata data[NHDP_MAXIMUM_DOMAINS];

  struct avl_node _node;
};

EXPORT extern struct avl_tree olsrv2_lan_tree;

void olsrv2_lan_init(void);
void olsrv2_lan_cleanup(void);

EXPORT struct olsrv2_lan_entry *olsrv2_lan_add(
    struct nhdp_domain *domain, struct netaddr *prefix,
    uint32_t metric, uint8_t distance);
EXPORT void olsrv2_lan_remove(struct nhdp_domain *,
    struct netaddr *prefix);

EXPORT int olsrv2_lan_validate(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out);

/**
 * @param addr originator address
 * @return pointer to LAN set entry, NULL if not found
 */
static INLINE struct olsrv2_lan_entry *
olsrv2_lan_get(struct netaddr *addr) {
  struct olsrv2_lan_entry *entry;
  return avl_find_element(&olsrv2_lan_tree, addr, entry, _node);
}

#endif /* OONFV2_LAN_H_ */
