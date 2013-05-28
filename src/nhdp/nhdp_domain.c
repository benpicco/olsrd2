
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

#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/common_types.h"
#include "common/list.h"
#include "common/netaddr.h"
#include "rfc5444/rfc5444.h"
#include "rfc5444/rfc5444_reader.h"
#include "core/oonf_cfg.h"
#include "core/oonf_logging.h"
#include "subsystems/oonf_class.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp_interfaces.h"

static void _apply_metric(struct nhdp_domain *domain, const char *metric_name);
static void _remove_metric(struct nhdp_domain *);
static void _apply_mpr(struct nhdp_domain *domain, const char *mpr_name);
static void _remove_mpr(struct nhdp_domain *);

static void _recalculate_neighbor_metric(struct nhdp_domain *domain,
    struct nhdp_neighbor *neigh);
static const char *_to_string(struct nhdp_metric_str *, uint32_t);

/* domain class */
struct oonf_class _domain_class = {
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
struct list_entity nhdp_domain_listener_list;

static size_t _domain_counter = 0;

/* tree of known routing metrics/mpr-algorithms */
struct avl_tree nhdp_domain_metrics;
struct avl_tree nhdp_domain_mprs;

/* flooding MPR handler registered to NHDP */
struct nhdp_domain_mpr *_flooding_mpr = &_no_mprs;
uint8_t _flooding_ext = 0;

/* NHDP RFC5444 protocol */
static struct oonf_rfc5444_protocol *_protocol;

/**
 * Initialize nhdp metric core
 * @param p pointer to rfc5444 protocol
 */
void
nhdp_domain_init(struct oonf_rfc5444_protocol *p) {
  _protocol = p;

  oonf_class_add(&_domain_class);
  list_init_head(&nhdp_domain_list);
  list_init_head(&nhdp_domain_listener_list);

  avl_init(&nhdp_domain_metrics, avl_comp_strcasecmp, false);
  avl_init(&nhdp_domain_mprs, avl_comp_strcasecmp, false);
}

/**
 * cleanup allocated resources for nhdp metric core
 */
void
nhdp_domain_cleanup(void) {
  struct nhdp_domain *domain, *d_it;
  struct nhdp_domain_listener *listener, *l_it;
  int i;

  list_for_each_element_safe(&nhdp_domain_list, domain, _node, d_it) {
    /* free allocated TLVs */
    for (i=0; i<4; i++) {
      rfc5444_writer_unregister_addrtlvtype(
          &_protocol->writer, &domain->_metric_addrtlvs[i]);
    }
    rfc5444_writer_unregister_addrtlvtype(
        &_protocol->writer, &domain->_mpr_addrtlv);

    /* remove domain */
    list_remove(&domain->_node);
    oonf_class_free(&_domain_class, domain);
  }

  list_for_each_element_safe(&nhdp_domain_listener_list, listener, _node, l_it) {
    nhdp_domain_listener_remove(listener);
  }
  oonf_class_remove(&_domain_class);
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
 * @param metric pointer to NHDP link metric
 * @return 0 if successful, -1 if metric was already registered
 */
int
nhdp_domain_metric_add(struct nhdp_domain_metric *metric) {
  struct nhdp_domain *domain;

  /* initialize key */
  metric->_node.key = metric->name;

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

  /* initialize to_string method if empty */
  if (metric->to_string == NULL) {
    metric->to_string = _to_string;
  }

  /* hook into tree */
  if (avl_insert(&nhdp_domain_metrics, &metric->_node)) {
    return -1;
  }

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    if (domain->metric == &_no_metric) {
      _apply_metric(domain, domain->metric_name);
    }
  }
  return 0;
}

/**
 * Remove a metric handler from the nhdp metric core
 * @param metric pointer to metric handler
 */
void
nhdp_domain_metric_remove(struct nhdp_domain_metric *metric) {
  struct nhdp_domain *domain;

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    if (domain->metric == metric) {
      _remove_metric(domain);
      break;
    }
  }

  avl_remove(&nhdp_domain_metrics, &metric->_node);
}

/**
 * Add a new mpr handler to nhdp
 * @param mpr pointer to mpr handler
 * @return 0 if successful, -1 if metric is already registered
 */
int
nhdp_domain_mpr_add(struct nhdp_domain_mpr *mpr) {
  struct nhdp_domain *domain;

  /* initialize key */
  mpr->_node.key = mpr->name;

  if (avl_insert(&nhdp_domain_mprs, &mpr->_node)) {
    return -1;
  }

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    if (domain->mpr == &_no_mprs) {
      _apply_mpr(domain, domain->mpr_name);
    }
  }
  return 0;
}

/**
 * Remove a metric handler from the nhdp metric core
 * @param mpr pointer to mpr handler
 */
void
nhdp_domain_mpr_remove(struct nhdp_domain_mpr *mpr) {
  struct nhdp_domain *domain;

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    if (domain->mpr == mpr) {
      _remove_mpr(domain);
      break;
    }
  }

  avl_remove(&nhdp_domain_mprs, &mpr->_node);
}

/**
 * Adds a listener to the NHDP domain system
 * @param listener pointer to NHDP domain listener
 */
void
nhdp_domain_listener_add(struct nhdp_domain_listener *listener) {
  list_add_tail(&nhdp_domain_listener_list, &listener->_node);
}

/**
 * Removes a listener from the NHDP domain system
 * @param listener pointer to NHDP domain listener
 */
void
nhdp_domain_listener_remove(struct nhdp_domain_listener *listener) {
  if (list_is_node_added(&listener->_node)) {
    list_remove(&listener->_node);
  }
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

    data->best_link = NULL;

    data->willingness = domain->mpr->willingness;
    data->local_is_mpr = domain->mpr->mprs_start;
    data->neigh_is_mpr = domain->mpr->mpr_start;
  }
}

/**
 * Process an in linkmetric tlv for a nhdp link
 * @param domain pointer to NHDP domain
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
 * @param domain pointer to NHDP domain
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
 * Neighborhood changed in terms of metrics or connectivity.
 * This will trigger a MPR set recalculation.
 */
void
nhdp_domain_neighborhood_changed(void) {
  struct nhdp_domain_listener *listener;
  struct nhdp_domain *domain;
  struct nhdp_neighbor *neigh;

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    list_for_each_element(&nhdp_neigh_list, neigh, _global_node) {
      _recalculate_neighbor_metric(domain, neigh);
      domain->metric_changed = true;
    }

    if (domain->mpr->update_mpr != NULL) {
      domain->mpr->update_mpr();
    }
  }

  // TODO: flooding mpr ?

  list_for_each_element(&nhdp_domain_listener_list, listener, _node) {
    if (listener->update) {
      listener->update(NULL);
    }
  }
}

/**
 * One neighbor changed in terms of metrics or connectivity.
 * This will trigger a MPR set recalculation.
 * @param neigh neighbor where the changed happened
 */
void
nhdp_domain_neighbor_changed(struct nhdp_neighbor *neigh) {
  struct nhdp_domain_listener *listener;
  struct nhdp_domain *domain;

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    _recalculate_neighbor_metric(domain, neigh);

    if (domain->mpr->update_mpr != NULL) {
      domain->mpr->update_mpr();
    }
  }

  // TODO: flooding mpr ?

  list_for_each_element(&nhdp_domain_listener_list, listener, _node) {
    if (listener->update) {
      listener->update(neigh);
    }
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
 * @param domain pointer to NHDP domain
 * @param tlvvalue willingness value to parse
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
 * Sets the incoming metric of a link. This is the only function external
 * code should use to commit the calculated metric values to the nhdp db.
 * @param domain NHDP domain
 * @param lnk NHDP link
 * @param metric_in incoming metric value for NHDP link
 */
void
nhdp_domain_set_incoming_metric(struct nhdp_domain *domain,
    struct nhdp_link *lnk, uint32_t metric_in) {
  struct nhdp_link_domaindata *domaindata;

  domaindata = nhdp_domain_get_linkdata(domain, lnk);
  domaindata->metric.in = metric_in;
}

/**
 * Recalculate the 'best link/metric' values of a neighbor
 * @param domain NHDP domain
 * @param neigh NHDP neighbor
 */
static void
_recalculate_neighbor_metric(
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

  /* reset best link */
  neighdata->best_link = NULL;

  /* get best metric */
  list_for_each_element(&neigh->_links, lnk, _neigh_node) {
    linkdata = nhdp_domain_get_linkdata(domain, lnk);

    if (linkdata->metric.out < neighdata->metric.out) {
      neighdata->metric.out = linkdata->metric.out;
      neighdata->best_link = lnk;
    }
    if (linkdata->metric.in < neighdata->metric.in) {
      neighdata->metric.in = linkdata->metric.in;
    }
  }

  if (neighdata->best_link != NULL) {
    neighdata->best_link_ifindex =
        nhdp_interface_get_coreif(neighdata->best_link->local_if)->data.index;
  }

  if (memcmp(&oldmetric, &neighdata->metric, sizeof(oldmetric)) != 0) {
    /* mark metric as updated */
    domain->metric_changed = true;
  }
}

/**
 * Add a new domain to the NHDP system
 * @param ext TLV extension type used for new domain
 * @return pointer to new domain, NULL, if out of memory or
 *   maximum number of domains has been reached.
 */
struct nhdp_domain *
nhdp_domain_add(uint8_t ext) {
  struct nhdp_domain *domain;
  int i;

  domain = nhdp_domain_get_by_ext(ext);
  if (domain) {
    return domain;
  }

  if (_domain_counter == NHDP_MAXIMUM_DOMAINS) {
    OONF_WARN(LOG_NHDP, "Maximum number of NHDP domains reached: %d",
        NHDP_MAXIMUM_DOMAINS);
    return NULL;
  }

  /* initialize new domain */
  domain = oonf_class_malloc(&_domain_class);
  if (domain == NULL) {
    return NULL;
  }

  domain->ext = ext;
  domain->index = _domain_counter++;
  domain->metric = &_no_metric;
  domain->mpr = &_no_mprs;

  /* initialize metric TLVs */
  for (i=0; i<4; i++) {
    domain->_metric_addrtlvs[i].type = RFC5444_ADDRTLV_LINK_METRIC;
    domain->_metric_addrtlvs[i].exttype = domain->ext;

    rfc5444_writer_register_addrtlvtype(&_protocol->writer,
        &domain->_metric_addrtlvs[i], -1);
  }

  /* initialize mpr tlv */
  domain->_mpr_addrtlv.type = RFC5444_ADDRTLV_MPR;
  domain->_mpr_addrtlv.exttype = domain->ext;

  rfc5444_writer_register_addrtlvtype(&_protocol->writer,
      &domain->_mpr_addrtlv, RFC5444_MSGTYPE_HELLO);

  /* add to domain list */

  list_add_tail(&nhdp_domain_list, &domain->_node);

  oonf_class_event(&_domain_class, domain,OONF_OBJECT_ADDED);
  return domain;
}

/**
 * Configure a NHDP domain to a metric and a MPR algorithm
 * @param ext TLV extension type used for new domain
 * @param metric_name name of the metric algorithm to be used,
 *   might be CFG_DOMAIN_NO_METRIC (for hopcount metric)
 *   or CFG_DOMAIN_ANY_METRIC (for a metric the NHDP core should
 *   choose).
 * @param mpr_name name of the MPR algorithm to be used,
 *   might be CFG_DOMAIN_NO_MPR (every node is MPR)
 *   or CFG_DOMAIN_ANY_MPR (for a MPR the NHDP core should
 *   choose).
 * @return pointer to configured domain, NULL, if out of memory or
 *   maximum number of domains has been reached.
 */
struct nhdp_domain *
nhdp_domain_configure(uint8_t ext, const char *metric_name, const char *mpr_name) {
  struct nhdp_domain *domain;

  domain = nhdp_domain_add(ext);
  if (domain == NULL) {
    return NULL;
  }

  _apply_metric(domain, metric_name);
  _apply_mpr(domain, mpr_name);

  oonf_class_event(&_domain_class, domain, OONF_OBJECT_CHANGED);

  return domain;
}

/**
 * Apply a new metric algorithm to a NHDP domain
 * @param domain pointer to NHDP domain
 * @param metric_name name of the metric algorithm to be used,
 *   might be CFG_DOMAIN_NO_METRIC (for hopcount metric)
 *   or CFG_DOMAIN_ANY_METRIC (for a metric the NHDP core should
 *   choose).
 */
static void
_apply_metric(struct nhdp_domain *domain, const char *metric_name) {
  struct nhdp_domain_metric *metric;

  /* check if we have to remove the old metric first */
  if (strcasecmp(domain->metric_name, metric_name) != 0) {
    if (domain->metric != &_no_metric) {
      _remove_metric(domain);
      strscpy(domain->metric_name, CFG_DOMAIN_NO_METRIC, sizeof(domain->metric_name));
    }
  }

  /* Handle wildcard metric name first */
  if (strcasecmp(metric_name, CFG_DOMAIN_ANY_METRIC) == 0
      && !avl_is_empty(&nhdp_domain_metrics)) {
    metric_name = avl_first_element(&nhdp_domain_metrics, metric, _node)->name;
  }

  /* copy new metric name */
  strscpy(domain->metric_name, metric_name, sizeof(domain->metric_name));

  /* look for metric implementation */
  metric = avl_find_element(&nhdp_domain_metrics, metric_name, metric, _node);
  if (metric == NULL) {
    domain->metric = &_no_metric;
    return;
  }

  /* link domain and metric */
  domain->metric = metric;
  metric->domain = domain;
}

/**
 * Reset the metric of a NHDP domain to hopcount
 * @param domain pointer to NHDP domain
 */
static void
_remove_metric(struct nhdp_domain *domain) {
  strscpy(domain->metric_name, CFG_DOMAIN_NO_METRIC, sizeof(domain->metric_name));
  domain->metric->domain = NULL;
  domain->metric = &_no_metric;
}

/**
 * Apply a new MPR algorithm to a NHDP domain
 * @param domain pointer to NHDP domain
 * @param mpr_name name of the MPR algorithm to be used,
 *   might be CFG_DOMAIN_NO_MPR (every node is MPR)
 *   or CFG_DOMAIN_ANY_MPR (for a MPR the NHDP core should
 *   choose).
 */
static void
_apply_mpr(struct nhdp_domain *domain, const char *mpr_name) {
  struct nhdp_domain_mpr *mpr;

  /* check if we have to remove the old mpr first */
  if (strcasecmp(domain->mpr_name, mpr_name) != 0) {
    if (domain->mpr != &_no_mprs) {
      _remove_mpr(domain);
      strscpy(domain->mpr_name, CFG_DOMAIN_NO_MPR, sizeof(domain->mpr_name));
    }
  }

  /* Handle wildcard mpr name first */
  if (strcasecmp(mpr_name, CFG_DOMAIN_ANY_METRIC) == 0
      && !avl_is_empty(&nhdp_domain_mprs)) {
    mpr_name = avl_first_element(&nhdp_domain_mprs, mpr, _node)->name;
  }

  /* copy new metric name */
  strscpy(domain->mpr_name, mpr_name, sizeof(domain->mpr_name));

  /* look for mpr implementation */
  mpr = avl_find_element(&nhdp_domain_mprs, mpr_name, mpr, _node);
  if (mpr == NULL) {
    domain->mpr = &_no_mprs;
    return;
  }

  /* link domain and mpr */
  domain->mpr = mpr;
  mpr->domain = domain;
}

/**
 * Reset the MPR of a NHDP domain to 'everyone is MPR'
 * @param domain pointer to NHDP domain
 */
static void
_remove_mpr(struct nhdp_domain *domain) {
  strscpy(domain->mpr_name, CFG_DOMAIN_NO_MPR, sizeof(domain->mpr_name));
  domain->mpr->domain = NULL;
  domain->mpr = &_no_mprs;
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
