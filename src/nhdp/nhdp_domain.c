
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
#include "core/olsr_class.h"
#include "core/olsr_logging.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp.h"

static struct nhdp_domain *_get_new_domain(uint8_t ext);
static const char *_to_string(struct nhdp_metric_str *, uint32_t);

/* domain class */
struct olsr_class _domain_class = {
  .name = NHDP_CLASS_DOMAIN,
  .size = sizeof(struct nhdp_domain),
};

/* default metric handler (hopcount) */
static struct nhdp_domain_metric _no_metric = {
  .name = "No metric",

  .incoming_link_start = NHDP_METRIC_DEFAULT,
  .outgoing_link_start = NHDP_METRIC_DEFAULT,
  .incoming_2hop_start = NHDP_METRIC_DEFAULT,
  .outgoing_2hop_start = NHDP_METRIC_DEFAULT,

  .no_default_handling = true,
};

/* default MPR handler (no MPR handling) */
static struct nhdp_domain_mpr _no_mprs = {
  .name = "No MPRs",

  .mpr_start = true,
  .mprs_start = true,
  .willingness = RFC5444_WILLINGNESS_DEFAULT,

  .no_default_handling = true,
};

/* non-default routing domains registered to NHDP */
struct list_entity nhdp_domain_list;
static size_t _domain_counter = 0;

/* flooding MPR handler registered to NHDP */
struct nhdp_domain_mpr *_flooding_mpr = &_no_mprs;
uint8_t _flooding_ext = 0;

/* NHDP RFC5444 protocol */
static struct olsr_rfc5444_protocol *_protocol;

/**
 * Initialize nhdp metric core
 * @param p pointer to rfc5444 protocol
 */
void
nhdp_domain_init(struct olsr_rfc5444_protocol *p) {
  _protocol = p;

  olsr_class_add(&_domain_class);
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
    olsr_class_free(&_domain_class, domain);
  }

  olsr_class_remove(&_domain_class);
}

/**
 * @return number of registered nhdp domains
 */
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
        &metric->_metric_addrtlvs[i], -1);
  }

  /* initialize to_string method if empty */
  if (metric->to_string == NULL) {
    metric->to_string = _to_string;
  }

  olsr_class_event(&_domain_class, domain, OLSR_OBJECT_CHANGED);

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

/**
 * @param ext TLV extension value of MPR/Linkmetrics
 * @return NHDP domain registered to this extension, NULL if not found
 */
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
 * Initialize the domain data of a new NHDP link
 * @param lnk NHDP link
 */
void
nhdp_domain_init_link(struct nhdp_link *lnk) {
  struct nhdp_domain *domain;
  struct nhdp_link_domaindata *data;

  /* initialize metrics */
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    data = nhdp_domain_get_linkdata(domain, lnk);

    data->metric.in = domain->metric->incoming_link_start;
    data->metric.out = domain->metric->outgoing_link_start;
  }
}

/**
 * Initialize the domain data of a new NHDP twohop neighbor
 * @param l2hop NHDP twohop neighbor
 */
void
nhdp_domain_init_l2hop(struct nhdp_l2hop *l2hop) {
  struct nhdp_domain *domain;
  struct nhdp_l2hop_domaindata *data;

  /* initialize metrics */
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    data = nhdp_domain_get_l2hopdata(domain, l2hop);

    data->metric.in = domain->metric->incoming_2hop_start;
    data->metric.out = domain->metric->outgoing_2hop_start;
  }
}

/**
 * Initialize the domain data of a new NHDP neighbor
 * @param neigh NHDP neighbor
 */
void
nhdp_domain_init_neighbor(struct nhdp_neighbor *neigh) {
  struct nhdp_domain *domain;
  struct nhdp_neighbor_domaindata *data;

  /* initialize flooding MPR settings */
  neigh->flooding_willingness = _flooding_mpr->willingness;
  neigh->local_is_flooding_mpr = _flooding_mpr->mprs_start;
  neigh->neigh_is_flooding_mpr = _flooding_mpr->mpr_start;

  /* initialize metrics and mprs */
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    data = nhdp_domain_get_neighbordata(domain, neigh);

    data->metric.in = domain->metric->incoming_link_start;
    data->metric.out = domain->metric->outgoing_link_start;

    data->willingness = domain->mpr->willingness;
    data->local_is_mpr = domain->mpr->mprs_start;
    data->neigh_is_mpr = domain->mpr->mpr_start;
  }
}

/**
 * Process an in linkmetric tlv for a nhdp link
 * @param h pointer to metric handler
 * @param lnk pointer to nhdp link
 * @param tlvvalue value of metric tlv
 */
void
nhdp_domain_process_metric_linktlv(struct nhdp_domain *domain,
    struct nhdp_link *lnk, uint16_t tlvvalue) {
  uint32_t metric;

  metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_COST_MASK);

  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_LINK) {
    nhdp_domain_get_linkdata(domain, lnk)->metric.out = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
    nhdp_domain_get_neighbordata(domain, lnk->neigh)->metric.out = metric;
  }
}

/**
 * Process an in linkmetric tlv for a nhdp twohop neighbor
 * @param h pointer to metric handler
 * @param l2hop pointer to nhdp twohop neighbor
 * @param tlvvalue value of metric tlv
 */
void
nhdp_domain_process_metric_2hoptlv(struct nhdp_domain *domain,
    struct nhdp_l2hop *l2hop, uint16_t tlvvalue) {
  uint32_t metric;
  struct nhdp_l2hop_domaindata *data;

  metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_COST_MASK);

  data = nhdp_domain_get_l2hopdata(domain, l2hop);
  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
    data->metric.in = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_OUTGOING_NEIGH) {
    data->metric.out = metric;
  }
}

/**
 * Calculate the minimal metric cost for a neighbor
 * @param domain NHDP domain
 * @param neigh nhdp neighbor
 */
void
nhdp_domain_calculate_neighbor_metric(
    struct nhdp_domain *domain,
    struct nhdp_neighbor *neigh) {
  struct nhdp_link *lnk;
  struct nhdp_link_domaindata *linkdata;
  struct nhdp_neighbor_domaindata *neighdata;
  struct nhdp_metric oldmetric;

  neighdata = nhdp_domain_get_neighbordata(domain, neigh);

  /* copy old metric value */
  memcpy(&oldmetric, &neighdata->metric, sizeof(oldmetric));

  /* reset metric */
  neighdata->metric.in = RFC5444_METRIC_INFINITE;
  neighdata->metric.out = RFC5444_METRIC_INFINITE;

  /* get best metric */
  list_for_each_element(&neigh->_links, lnk, _neigh_node) {
    linkdata = nhdp_domain_get_linkdata(domain, lnk);

    if (linkdata->metric.out < neighdata->metric.out) {
      neighdata->metric.out = linkdata->metric.out;
    }
    if (linkdata->metric.in < neighdata->metric.in) {
      neighdata->metric.in = linkdata->metric.in;
    }
  }

  if (memcmp(&oldmetric, &neighdata->metric, sizeof(oldmetric)) != 0) {
    /* mark metric as updated */
    domain->metric_changed = true;
  }
}

/**
 * Process an in MPR tlv for a NHDP link
 * @param domain NHDP domain
 * @param lnk NHDP link
 * @param tlvvalue value of MPR tlv
 */
void
nhdp_domain_process_mpr_tlv(struct nhdp_domain *domain,
    struct nhdp_link *lnk, uint8_t tlvvalue) {
  if (domain->ext == _flooding_ext) {
    lnk->neigh->local_is_flooding_mpr =
        tlvvalue == RFC5444_MPR_FLOODING
        || tlvvalue == RFC5444_MPR_FLOOD_ROUTE;
  }

  nhdp_domain_get_neighbordata(domain, lnk->neigh)->local_is_mpr =
      tlvvalue == RFC5444_MPR_ROUTING
      || tlvvalue == RFC5444_MPR_FLOOD_ROUTE;
}

/**
 * Process an in Willingness tlv and put values into
 * temporary storage in MPR handler object
 * @param domain
 * @param lnk
 * @param tlvvalue
 */
void
nhdp_domain_process_willingness_tlv(struct nhdp_domain *domain,
    uint8_t tlvvalue) {
  /* copy routing willingness */
  domain->mpr->_tmp_willingness =
      tlvvalue & RFC5444_WILLINGNESS_ROUTING_MASK;

  if (domain->ext == _flooding_ext) {
    _flooding_mpr->_tmp_willingness =
        tlvvalue >> RFC5444_WILLINGNESS_FLOODING_SHIFT;
  }
}

/**
 * Calculates the tlvvalue of a Willingness tlv
 * @param domain domain of the Willingness tlv
 * @return tlvvalue
 */
uint8_t
nhdp_domain_get_willingness_tlvvalue(struct nhdp_domain *domain) {
  uint8_t tlvvalue;

  tlvvalue = domain->mpr->willingness;

  if (domain->ext == _flooding_ext) {
    tlvvalue |=
        (_flooding_mpr->willingness << RFC5444_WILLINGNESS_FLOODING_SHIFT);
  }

  return tlvvalue;
}

/**
 * Calculates the tlvvalue of a MPR tlv
 * @param domain domain of MPR tlv
 * @param lnk pointer to NHDP link for MPR tlv
 * @return tlvvalue
 */
uint8_t
nhdp_domain_get_mpr_tlvvalue(
    struct nhdp_domain *domain, struct nhdp_link *lnk) {
  struct nhdp_neighbor_domaindata *data;

  data = nhdp_domain_get_neighbordata(domain, lnk->neigh);

  if (domain->ext == _flooding_ext && lnk->neigh->neigh_is_flooding_mpr) {
    if (data->neigh_is_mpr) {
      return RFC5444_MPR_FLOOD_ROUTE;
    }
    else {
      return RFC5444_MPR_FLOODING;
    }
  }
  else {
    if (data->neigh_is_mpr) {
      return RFC5444_MPR_ROUTING;
    }
    else {
      return RFC5444_MPR_NOMPR;
    }
  }
}

/**
 * Update all MPR sets
 */
void
nhdp_domain_update_mprs(void) {
  struct nhdp_domain *domain;

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    if (domain->mpr->update_mpr) {
      domain->mpr->update_mpr();
    }
  }
}

/**
 * Sets a new flodding MPR algorithm
 * @param mpr pointer to flooding MPR handler
 * @param ext TLV extension to transport flooding MPR settings
 */
void
nhdp_domain_set_flooding_mpr(struct nhdp_domain_mpr *mpr, uint8_t ext) {
  if (mpr == NULL) {
    _flooding_mpr = &_no_mprs;
    _flooding_ext = 0;
  }
  else {
    _flooding_mpr = mpr;
    _flooding_ext = ext;
  }
}

/**
 * @param ext domain TLV extension value
 * @return NHDP domain for this extension value, create a new one
 *   if necessary, NULL if out of memory or maximum domains number
 *   is reached
 */
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
    domain->index = _domain_counter++;
    domain->metric = &_no_metric;
    domain->mpr = &_no_mprs;

    list_add_tail(&nhdp_domain_list, &domain->_node);

    olsr_class_event(&_domain_class, domain, OLSR_OBJECT_ADDED);
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
