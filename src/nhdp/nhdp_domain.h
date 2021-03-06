
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

#ifndef NHDP_DOMAIN_H_
#define NHDP_DOMAIN_H_

#include "common/common_types.h"
#include "common/list.h"
#include "rfc5444/rfc5444_writer.h"
#include "subsystems/oonf_rfc5444.h"

#include "nhdp/nhdp_db.h"

#define NHDP_CLASS_DOMAIN             "nhdp_domain"

/* constants with maximum length of metric/mpr name */
enum {
  NHDP_DOMAIN_METRIC_MAXLEN = 16,
  NHDP_DOMAIN_MPR_MAXLEN = 16,
};

/* Buffer for string representation of a linkmetric value */
struct nhdp_metric_str {
  char buf[128];
};

/* Metric handler for a NHDP domain. */
struct nhdp_domain_metric {
  /* name of linkmetric */
  const char *name;

  /* range of metric */
  uint32_t metric_minimum, metric_maximum;

  /* default values to initialize database */
  uint32_t incoming_link_start, outgoing_link_start;
  uint32_t incoming_2hop_start, outgoing_2hop_start;

  /* true if metrics should not be handled by nhdp reader/writer */
  bool no_default_handling;

  /* backpointer to domain */
  struct nhdp_domain *domain;

  /* conversion of metric value into string function */
  const char *(*to_string)(struct nhdp_metric_str *, uint32_t);

  /* node for tree of metrics */
  struct avl_node _node;
};

/* MPR handler for a NHDP domain */
struct nhdp_domain_mpr {
  /* name of handler */
  const char *name;

  /* calculate MPR set */
  void (*update_mpr)(void);

  /* routing willingness */
  enum rfc5444_willingness_values willingness;

  /* default value for neighbor MPR setting */
  bool mpr_start;

  /* default value for local MPR (selector) setting */
  bool mprs_start;

  /* true if MPRs/Willingness should not be handled by nhdp reader/writer */
  bool no_default_handling;

  /* backpointer to domain */
  struct nhdp_domain *domain;

  /* temporary storage of willingness during message parsing */
  uint8_t _tmp_willingness;

  /* node for tree of MPR algorithms */
  struct avl_node _node;
};

/*
 * NHDP domain
 *
 * A domain is a topology on the mesh, including its own
 * metric and routing MPR set. Both is transmitted over a
 * specified TLV extension value on MPR and LQ TLVs.
 */
struct nhdp_domain {
  char metric_name[NHDP_DOMAIN_METRIC_MAXLEN];
  char mpr_name[NHDP_DOMAIN_MPR_MAXLEN];

  struct nhdp_domain_metric *metric;
  struct nhdp_domain_mpr *mpr;

  /*
   * true if a neighbor metric of this domain has changed
   * since the last reset of this variable
   */
  bool metric_changed;

  /* tlv extension */
  uint8_t ext;

  /* index in the metric array */
  int index;

  /* storage for the up to four additional link metrics */
  struct rfc5444_writer_tlvtype _metric_addrtlvs[4];

  /* storage for the additional mpr tlv */
  struct rfc5444_writer_tlvtype _mpr_addrtlv;

  /* list of nhdp domains */
  struct list_entity _node;
};

/* listener for NHDP domain updates */
struct nhdp_domain_listener {
  void (*update)(struct nhdp_neighbor *);

  struct list_entity _node;
};

EXPORT extern struct list_entity nhdp_domain_list;
EXPORT extern struct list_entity nhdp_domain_listener_list;

void nhdp_domain_init(struct oonf_rfc5444_protocol *);
void nhdp_domain_cleanup(void);

EXPORT size_t nhdp_domain_get_count(void);
EXPORT struct nhdp_domain *nhdp_domain_add(uint8_t ext);
EXPORT struct nhdp_domain *nhdp_domain_configure(
    uint8_t ext, const char *metric_name, const char *mpr_name);

EXPORT int nhdp_domain_metric_add(struct nhdp_domain_metric *);
EXPORT void nhdp_domain_metric_remove(struct nhdp_domain_metric *);

EXPORT int nhdp_domain_mpr_add(struct nhdp_domain_mpr *);
EXPORT void nhdp_domain_mpr_remove(struct nhdp_domain_mpr *);

EXPORT void nhdp_domain_listener_add(struct nhdp_domain_listener *);
EXPORT void nhdp_domain_listener_remove(struct nhdp_domain_listener *);

EXPORT void nhdp_domain_set_flooding_mpr(
    struct nhdp_domain_mpr *, uint8_t ext);

EXPORT struct nhdp_domain *nhdp_domain_get_by_ext(uint8_t);

EXPORT void nhdp_domain_init_link(struct nhdp_link *);
EXPORT void nhdp_domain_init_l2hop(struct nhdp_l2hop *);
EXPORT void nhdp_domain_init_neighbor(struct nhdp_neighbor *);

EXPORT void nhdp_domain_process_metric_linktlv(struct nhdp_domain *,
    struct nhdp_link *lnk, uint16_t tlvvalue);
EXPORT void nhdp_domain_process_metric_2hoptlv(struct nhdp_domain *d,
    struct nhdp_l2hop *l2hop, uint16_t tlvvalue);

EXPORT void nhdp_domain_neighborhood_changed(void);
EXPORT void nhdp_domain_neighbor_changed(struct nhdp_neighbor *neigh);

EXPORT void nhdp_domain_process_mpr_tlv(struct nhdp_domain *,
    struct nhdp_link *lnk, uint8_t tlvvalue);
EXPORT void nhdp_domain_process_willingness_tlv(
    struct nhdp_domain *, uint8_t tlvvalue);
EXPORT uint8_t nhdp_domain_get_willingness_tlvvalue(
    struct nhdp_domain *);
EXPORT uint8_t nhdp_domain_get_mpr_tlvvalue(
    struct nhdp_domain *, struct nhdp_link *);

EXPORT void nhdp_domain_set_incoming_metric(
    struct nhdp_domain *domain, struct nhdp_link *lnk, uint32_t metric_in);

/**
 * @param domain NHDP domain
 * @param lnk NHDP link
 * @return domain data of specified link
 */
static INLINE struct nhdp_link_domaindata *
nhdp_domain_get_linkdata(struct nhdp_domain *domain, struct nhdp_link *lnk) {
  return &lnk->_domaindata[domain->index];
}

/**
 * @param domain NHDP domain
 * @param neigh NHDP neighbor
 * @return domain data of specified neighbor
 */
static INLINE struct nhdp_neighbor_domaindata *
nhdp_domain_get_neighbordata(
    struct nhdp_domain *domain, struct nhdp_neighbor *neigh) {
  return &neigh->_domaindata[domain->index];
}

/**
 * @param domain NHDP domain
 * @param l2hop NHDP twohop neighbor
 * @return domain data of specified twohop neighbor
 */
static INLINE struct nhdp_l2hop_domaindata *
nhdp_domain_get_l2hopdata(
    struct nhdp_domain *domain, struct nhdp_l2hop *l2hop) {
  return &l2hop->_domaindata[domain->index];
}

#endif /* NHDP_DOMAIN_H_ */
