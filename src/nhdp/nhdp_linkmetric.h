
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

struct nhdp_metric;

#include "common/common_types.h"
#include "rfc5444/rfc5444_writer.h"
#include "tools/olsr_rfc5444.h"

#include "nhdp/nhdp_db.h"

struct nhdp_linkmetric_str {
  char buf[128];
};

struct nhdp_linkmetric_handler {
  /* name of linkmetric */
  const char *name;

  /* TLV extension value */
  int ext;

  /* true if NHDP writer should NOT create metric TLVs */
  bool no_tlvs;

  /* range of metric */
  uint32_t metric_minimum, metric_start, metric_maximum;

  const char *(*to_string)(struct nhdp_linkmetric_str *, uint32_t);

  /* storage for the up to four additional link metrics */
  struct rfc5444_writer_tlvtype _metric_addrtlvs[4];

  /* index in the metric array */
  int _index;

  /* list of metric handlers */
  struct list_entity _node;
};

EXPORT extern struct nhdp_linkmetric_handler *nhdp_metric_handler[256];
EXPORT extern struct list_entity nhdp_metric_handler_list;

void nhdp_linkmetric_init(struct olsr_rfc5444_protocol *);
void nhdp_linkmetric_cleanup(void);

EXPORT int nhdp_linkmetric_handler_add(struct nhdp_linkmetric_handler *h);
EXPORT void nhdp_linkmetric_handler_remove(struct nhdp_linkmetric_handler *h);

EXPORT void nhdp_linkmetric_process_linktlv(struct nhdp_linkmetric_handler *h,
    struct nhdp_link *lnk, uint16_t tlvvalue);
EXPORT void nhdp_linkmetric_process_2hoptlv(struct nhdp_linkmetric_handler *h,
    struct nhdp_l2hop *l2hop, uint16_t tlvvalue);
EXPORT void nhdp_linkmetric_calculate_neighbor_metric(
    struct nhdp_linkmetric_handler *, struct nhdp_neighbor *);

static INLINE struct nhdp_linkmetric_handler *
nhdp_linkmetric_handler_get_by_ext(uint8_t ext) {
  return nhdp_metric_handler[ext];
}

#endif /* NHDP_LINKCOST_H_ */
