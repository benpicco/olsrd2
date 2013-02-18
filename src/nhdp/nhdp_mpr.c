
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

#include "common/common_types.h"
#include "config/cfg.h"
#include "config/cfg_schema.h"
#include "core/olsr_logging.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_mpr.h"

/* Prototypes */
static void _update_mpr(struct nhdp_interface *);
static void _set_mprs(struct nhdp_link *lnk, bool);
static bool _is_mpr(struct nhdp_link *);
static bool _use_willingness(struct nhdp_interface *);

/* MPR default handler */
static struct nhdp_mpr_handler _handler = {
  .name = "No MPRs",
  .update_mpr = _update_mpr,
  .set_mprs = _set_mprs,
  .is_mpr = _is_mpr,
  .use_willingness = _use_willingness,
};

struct nhdp_mpr_handler *nhdp_routing_mpr = &_handler;
struct nhdp_mpr_handler *nhdp_flooding_mpr = &_handler;

/**
 * Set a handler for routing or flodding MPR calculation
 * @param handler pointer to handler of NULL to reset to default
 * @param routing true if routing handler, false for flooding handler
 */
void
nhdp_mpr_set_handler(struct nhdp_mpr_handler *handler, bool routing) {
  struct nhdp_mpr_handler **ptr;

  if (routing) {
    ptr = &nhdp_routing_mpr;
  }
  else {
    ptr = &nhdp_flooding_mpr;
  }

  if (handler == NULL) {
    *ptr = &_handler;
  }
  else {
    *ptr = handler;
  }
}

/**
 * Dummy function for updating MPR set (does nothing)
 * @param interf pointer to local nhdp interface
 */
static void
_update_mpr(struct nhdp_interface *interf __attribute((unused))) {
  return;
}

/**
 * Dummy function to story MPR selectors of a neighbor (does nothing)
 * @param lnk pointer to nhdp link
 * @param selected true if neighbor selected us as a MPR
 */
static void
_set_mprs(struct nhdp_link *lnk __attribute((unused)),
    bool selected __attribute((unused))) {
  return;
}

/**
 * Dummy function to calculate links mpr TLV value.
 * @param lnk pointer to link
 * @return always true
 */
static bool
_is_mpr(struct nhdp_link *lnk __attribute((unused))) {
  return true;
}

/**
 * Dummy function to decide if interface hello message should
 * contain willingness TLV
 * @param interf pointer to local nhdp interface
 * @return always false
 */
static bool
_use_willingness(struct nhdp_interface *interf __attribute((unused))) {
  return false;
}
