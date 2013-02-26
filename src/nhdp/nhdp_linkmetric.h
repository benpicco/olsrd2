
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

#ifndef NHDP_LINKCOST_H_
#define NHDP_LINKCOST_H_

#include "common/common_types.h"
#include "rfc5444/rfc5444_writer.h"
#include "tools/olsr_rfc5444.h"

#include "nhdp/nhdp_db.h"

struct nhdp_linkmetric {
  uint32_t incoming;
  uint32_t outgoing;
};

struct nhdp_linkmetric_handler {
  const char *name;

  uint8_t ext;
  bool create_tlvs;

  void (*get_link_metric)(uint32_t *, uint32_t *, struct nhdp_link *);
  void (*get_neighbor_metric)(uint32_t *, uint32_t *,struct nhdp_neighbor *);

  void (*process_linkmetric_tlv)(struct nhdp_link *, uint16_t);

  /* storage for the up to four additional link metrics */
  struct rfc5444_writer_tlvtype _metric_addrtlvs[4];
};

void nhdp_linkmetric_init(struct olsr_rfc5444_protocol *);
void nhdp_linkmetric_cleanup(void);

void nhdp_linkmetric_handler_add(struct nhdp_linkmetric_handler *h);
void nhdp_linkmetric_handler_remove(struct nhdp_linkmetric_handler *h);

EXPORT struct nhdp_linkmetric_handler *nhdp_linkmetric_handler_get(void);

static INLINE void
nhdp_linkmetric_get_link_metric(struct nhdp_linkmetric *dst,
    struct nhdp_link *lnk, uint8_t ext __attribute__((unused))) {
  nhdp_linkmetric_handler_get()->get_link_metric(
      &dst->incoming, &dst->outgoing, lnk);
}

static INLINE void
nhdp_linkmetric_get_neighbor_metric(struct nhdp_linkmetric *dst,
    struct nhdp_neighbor *neigh, uint8_t ext __attribute__((unused))) {
  nhdp_linkmetric_handler_get()->get_neighbor_metric(
      &dst->incoming, &dst->outgoing, neigh);
}

static INLINE void
nhdp_linkmetric_process_tlv(struct nhdp_link *lnk,
    uint8_t ext, uint16_t value) {
  struct nhdp_linkmetric_handler *h;

  h = nhdp_linkmetric_handler_get();
  if (h->ext == ext) {
    h->process_linkmetric_tlv(lnk, value);
  }
}

#endif /* NHDP_LINKCOST_H_ */
