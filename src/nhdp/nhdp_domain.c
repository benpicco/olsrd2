
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

#include <stdio.h>

#include "common/common_types.h"
#include "rfc5444/rfc5444.h"
#include "rfc5444/rfc5444_reader.h"
#include "core/olsr_logging.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp.h"

static struct nhdp_domain *_get_new_domain(uint8_t ext);
static const char *_to_string(struct nhdp_metric_str *, uint32_t);

static struct nhdp_domain_metric _no_metric = {
  .name = "No metric",

  .incoming_link_start = NHDP_METRIC_DEFAULT,
  .outgoing_link_start = NHDP_METRIC_DEFAULT,
  .incoming_2hop_start = NHDP_METRIC_DEFAULT,
  .outgoing_2hop_start = NHDP_METRIC_DEFAULT,

  .no_default_handling = true,
};

static struct nhdp_domain_mpr _no_mprs = {
  .name = "No MPRs",

  .mpr_start = true,
  .mprs_start = true,

  .no_default_handling = true,
};

struct list_entity nhdp_domain_list;

static size_t _domain_counter = 0;
static struct olsr_rfc5444_protocol *_protocol;

/**
 * Initialize nhdp metric core
 * @param p pointer to rfc5444 protocol
 */
void
nhdp_domain_init(struct olsr_rfc5444_protocol *p) {
  _protocol = p;

  list_init_head(&nhdp_domain_list);
}

/**
 * cleanup allocated resources for nhdp metric core
 */
void
nhdp_domain_cleanup(void) {
  struct nhdp_domain *domain, *d_it;

  list_for_each_element_safe(&nhdp_domain_list, domain, _node, d_it) {
    /* remove metric */
    list_remove(&domain->_node);
    free(domain);
  }
}

size_t
nhdp_domain_get_count(void) {
  return _domain_counter;
}

/**
 * Add a new metric handler to nhdp
 * @param h pointer to handler
 * @return 0 if successful, -1 if metric extension is already blocked
 */
struct nhdp_domain *
nhdp_domain_metric_add(struct nhdp_domain_metric *metric, uint8_t ext) {
  struct nhdp_domain *domain;
  int i;

  domain = _get_new_domain(ext);
  if (domain == NULL) {
    return NULL;
  }
  if (domain->metric != &_no_metric) {
    OLSR_WARN(LOG_NHDP, "Error, link metric extension %u collision between '%s' and '%s'",
        domain->ext, metric->name, domain->metric->name);
    return NULL;
  }

  /* link metric */
  domain->metric = metric;

  /* insert default values if not set */
  if (metric->incoming_link_start == 0) {
    metric->incoming_link_start = NHDP_METRIC_DEFAULT;
  }
  if (metric->outgoing_link_start == 0) {
    metric->outgoing_link_start = RFC5444_METRIC_INFINITE;
  }
  if (metric->incoming_2hop_start == 0) {
    metric->incoming_2hop_start = RFC5444_METRIC_INFINITE;
  }
  if (metric->outgoing_2hop_start == 0) {
    metric->outgoing_2hop_start = RFC5444_METRIC_INFINITE;
  }

  /* add to metric handler list */
  for (i=0; i<4; i++) {
    metric->_metric_addrtlvs[i].type = RFC5444_ADDRTLV_LINK_METRIC;
    metric->_metric_addrtlvs[i].exttype = ext;

    rfc5444_writer_register_addrtlvtype(&_protocol->writer,
        &metric->_metric_addrtlvs[i], RFC5444_MSGTYPE_HELLO);
  }

  /* initialize to_string method if empty */
  if (metric->to_string == NULL) {
    metric->to_string = _to_string;
  }

  return domain;
}

/**
 * Remove a metric handler from the nhdp metric core
 * @param h pointer to handler
 */
void
nhdp_domain_metric_remove(struct nhdp_domain *domain) {
  int i;

  /* unregister TLV handlers */
  for (i=0; i<4; i++) {
    rfc5444_writer_unregister_addrtlvtype(&_protocol->writer,
        &domain->metric->_metric_addrtlvs[i]);
  }

  domain->metric = &_no_metric;
}

/**
 * Add a new metric handler to nhdp
 * @param h pointer to handler
 * @return 0 if successful, -1 if metric extension is already blocked
 */
struct nhdp_domain *
nhdp_domain_mpr_add(struct nhdp_domain_mpr *mpr, uint8_t ext) {
  struct nhdp_domain *domain;

  domain = _get_new_domain(ext);
  if (domain == NULL) {
    return NULL;
  }
  if (domain->mpr != &_no_mprs) {
    OLSR_WARN(LOG_NHDP, "Error, mpr extension %u collision between '%s' and '%s'",
        domain->ext, mpr->name, domain->mpr->name);
    return NULL;
  }

  /* link mpr */
  domain->mpr = mpr;

  /* add to metric handler list */
  mpr->_mpr_addrtlv.type = RFC5444_ADDRTLV_MPR;
  mpr->_mpr_addrtlv.exttype = ext;

  rfc5444_writer_register_addrtlvtype(&_protocol->writer,
      &mpr->_mpr_addrtlv, RFC5444_MSGTYPE_HELLO);

  return domain;
}

/**
 * Remove a metric handler from the nhdp metric core
 * @param h pointer to handler
 */
void
nhdp_domain_mpr_remove(struct nhdp_domain *domain) {
  /* unregister TLV handler */
  rfc5444_writer_unregister_addrtlvtype(&_protocol->writer,
      &domain->mpr->_mpr_addrtlv);

  domain->mpr = &_no_mprs;
}

struct nhdp_domain *
nhdp_domain_get_by_ext(uint8_t ext) {
  struct nhdp_domain *d;

  list_for_each_element(&nhdp_domain_list, d, _node) {
    if (d->ext == ext) {
      return d;
    }
  }
  return NULL;
}

/**
 * Process an incoming linkmetric tlv for a nhdp link
 * @param h pointer to metric handler
 * @param lnk pointer to nhdp link
 * @param tlvvalue value of metric tlv
 */
void
nhdp_domain_process_metric_linktlv(struct nhdp_domain *d,
    struct nhdp_link *lnk, uint16_t tlvvalue) {
  uint32_t metric;

  metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_COST_MASK);

  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_LINK) {
    lnk->_metric[d->_index].m.outgoing = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
    lnk->neigh->_metric[d->_index].m.outgoing = metric;
  }
}

/**
 * Process an incoming linkmetric tlv for a nhdp twohop neighbor
 * @param h pointer to metric handler
 * @param l2hop pointer to nhdp twohop neighbor
 * @param tlvvalue value of metric tlv
 */
void
nhdp_domain_process_metric_2hoptlv(struct nhdp_domain *d,
    struct nhdp_l2hop *l2hop, uint16_t tlvvalue) {
  uint32_t metric;

  metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_COST_MASK);

  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
    l2hop->_metric[d->_index].incoming = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_OUTGOING_NEIGH) {
    l2hop->_metric[d->_index].outgoing = metric;
  }
}

/**
 * Calculate the minimal metric cost for a neighbor
 * @param h pointer to metric handler
 * @param neigh nhdp neighbor
 */
void
nhdp_domain_calculate_neighbor_metric(
    struct nhdp_domain *d,
    struct nhdp_neighbor *neigh) {
  struct nhdp_link *lnk;

  neigh->_metric[d->ext].m.incoming = RFC5444_METRIC_INFINITE;
  neigh->_metric[d->ext].m.outgoing = RFC5444_METRIC_INFINITE;

  list_for_each_element(&neigh->_links, lnk, _neigh_node) {
    if (lnk->_metric[d->ext].m.outgoing < neigh->_metric[d->ext].m.outgoing) {
      neigh->_metric[d->ext].m.outgoing = lnk->_metric[d->ext].m.outgoing;
    }
    if (lnk->_metric[d->ext].m.incoming < neigh->_metric[d->ext].m.incoming) {
      neigh->_metric[d->ext].m.incoming = lnk->_metric[d->ext].m.incoming;
    }
  }
}

void
nhdp_domain_process_mpr_tlv(struct nhdp_domain *d,
    struct nhdp_link *lnk, uint8_t tlvvalue) {
  lnk->flooding_mpr = tlvvalue == RFC5444_MPR_FLOODING
      || tlvvalue == RFC5444_MPR_FLOOD_ROUTE;
  lnk->neigh->_metric[d->_index].local_is_mpr =
      tlvvalue == RFC5444_MPR_ROUTING
      || tlvvalue == RFC5444_MPR_FLOOD_ROUTE;
}

/**
 * Update all MPR sets
 */
void
nhdp_domain_update_mprs(void) {

}

enum rfc5444_willingness_values
nhdp_domain_get_flooding_willingness(void) {
  return RFC5444_WILLINGNESS_DEFAULT;
}

static struct nhdp_domain *
_get_new_domain(uint8_t ext) {
  struct nhdp_domain *domain;

  domain = nhdp_domain_get_by_ext(ext);
  if (domain == NULL) {
    if (_domain_counter == NHDP_MAXIMUM_DOMAINS) {
      OLSR_WARN(LOG_NHDP, "Maximum number of NHDP domains reached: %d",
          NHDP_MAXIMUM_DOMAINS);
      return NULL;
    }

    /* initialize new domain */
    domain = calloc(1, sizeof(struct nhdp_domain));
    domain->ext = ext;
    domain->_index = _domain_counter++;
    domain->metric = &_no_metric;
    domain->mpr = &_no_mprs;

    list_add_tail(&nhdp_domain_list, &domain->_node);
  }
  return domain;
}
/**
 * Default implementation to convert a metric value into text
 * @param buf pointer to metric output buffer
 * @param metric metric value
 * @return pointer to string representation of metric value
 */
static const char *
_to_string(struct nhdp_metric_str *buf, uint32_t metric) {
  snprintf(buf->buf, sizeof(*buf), "0x%x", metric);

  return buf->buf;
}
