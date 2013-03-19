
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

#include "common/common_types.h"
#include "config/cfg.h"
#include "config/cfg_schema.h"
#include "core/olsr_logging.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_mpr.h"

static void _update_mpr(struct nhdp_interface *);

static struct nhdp_mpr_handler _everyone_is_mpr = {
  .name = "everyone is MPR",
  .no_willingness = true,
  .update_mpr = _update_mpr,
};

struct nhdp_mpr_handler *nhdp_mpr_handler[256];
struct list_entity nhdp_mpr_handler_list;
struct nhdp_mpr_handler *nhdp_flooding_mpr;

static struct olsr_rfc5444_protocol *_protocol;

void
nhdp_mpr_init(struct olsr_rfc5444_protocol *p) {
  size_t i;

  /* remember protocol */
  _protocol = p;

  /* initialize mpr handler cache */
  for (i=0; i<ARRAYSIZE(nhdp_mpr_handler); i++) {
    nhdp_mpr_handler[i] = &_everyone_is_mpr;
  }
}

void
nhdp_mpr_cleanup(void) {
  size_t i;

  for (i=0; i<ARRAYSIZE(nhdp_mpr_handler); i++) {

  }
}

int
nhdp_mpr_add(struct nhdp_mpr_handler *h __attribute__((unused))) {
  return 0;
}

void
nhdp_mpr_remove(struct nhdp_mpr_handler *h __attribute__((unused))) {
}

void nhdp_mpr_process_linktlv(struct nhdp_mpr_handler *h __attribute__((unused)),
    struct nhdp_link *lnk __attribute__((unused)), uint16_t tlvvalue __attribute__((unused))) {

}

void
nhdp_mpr_update(struct nhdp_interface * interf __attribute__((unused))) {

}

bool
nhdp_mpr_use_willingness(void) {
  return false;
}

static void
_update_mpr(struct nhdp_interface *interf __attribute__((unused))) {

}
