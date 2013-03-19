
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

#ifndef NHDP_MPR_H_
#define NHDP_MPR_H_

#include "common/common_types.h"
#include "common/list.h"

#include "rfc5444/rfc5444_writer.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_interfaces.h"

/* handler for generating MPR information of a link */
struct nhdp_mpr_handler {
  /* name of handler */
  const char *name;

  /* mpr extension, not used for flooding MPR */
  uint8_t ext;

  /* update mpr settings */
  void (*update_mpr)(struct nhdp_interface *);

  /*
   * true if nhdp message for interface doesn't need
   * to contain willingness TLV
   */
  bool no_willingness;

  /* storage for the additional mpr tlv */
  struct rfc5444_writer_tlvtype _mpr_addrtlv;

  /* index in the metric array */
  int _index;

  /* list of mpr handlers */
  struct list_entity _node;
};

EXPORT extern struct nhdp_mpr_handler *nhdp_mpr_handler[256];
EXPORT extern struct list_entity nhdp_mpr_handler_list;
EXPORT extern struct nhdp_mpr_handler *nhdp_flooding_mpr;

void nhdp_mpr_init(struct olsr_rfc5444_protocol *);
void nhdp_mpr_cleanup(void);

EXPORT int nhdp_mpr_add(struct nhdp_mpr_handler *h);
EXPORT void nhdp_mpr_remove(struct nhdp_mpr_handler *h);

EXPORT void nhdp_mpr_process_linktlv(struct nhdp_mpr_handler *h,
    struct nhdp_link *lnk, uint16_t tlvvalue);
EXPORT void nhdp_mpr_update(struct nhdp_interface *);
EXPORT bool nhdp_mpr_use_willingness(void);

static INLINE struct nhdp_mpr_handler *
nhdp_mpr_get_handler_by_ext(uint8_t ext) {
  return nhdp_mpr_handler[ext];
}

#endif /* NHDP_MPR_H_ */
