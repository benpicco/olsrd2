
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
#include "nhdp/nhdp_metric.h"
#include "nhdp/nhdp.h"

static const char *_to_string(struct nhdp_metric_str *, uint32_t);

static struct nhdp_metric_handler *nhdp_metric_handler[256];
static struct nhdp_mpr_handler *nhdp_mpr_handler[256];
struct list_entity nhdp_metric_handler_list;
struct list_entity nhdp_mpr_handler_list;

static struct olsr_rfc5444_protocol *_protocol;

/**
 * Initialize nhdp metric core
 * @param p pointer to rfc5444 protocol
 */
void
nhdp_domain_init(struct olsr_rfc5444_protocol *p) {
  size_t i;

  _protocol = p;

  list_init_head(&nhdp_metric_handler_list);
  list_init_head(&nhdp_mpr_handler_list);

  for (i=0; i<ARRAYSIZE(nhdp_metric_handler); i++) {
    nhdp_metric_handler[i] = NULL;
    nhdp_mpr_handler[i] = NULL;
  }
}

/**
 * cleanup allocated resources for nhdp metric core
 */
void
nhdp_domain_cleanup(void) {
  struct nhdp_metric_handler *metric, *metr_it;
  struct nhdp_mpr_handler *mpr, *mpr_it;

  list_for_each_element_safe(&nhdp_metric_handler_list, metric, _node, metr_it) {
    nhdp_metric_handler_remove(metric);
  }

  list_for_each_element_safe(&nhdp_mpr_handler_list, mpr, _node, mpr_it) {
    nhdp_mpr_handler_remove(mpr);
  }
}

/**
 * Add a new metric handler to nhdp
 * @param h pointer to handler
 * @return 0 if successful, -1 if metric extension is already blocked
 */
int
nhdp_metric_handler_add(struct nhdp_metric_handler *h) {
  int i;

  if (nhdp_metric_handler[h->ext]) {
    OLSR_WARN(LOG_NHDP, "Error, link metric extension %u collision between '%s' and '%s'",
        h->ext, h->name, nhdp_metric_handler[h->ext]->name);
    return -1;
  }

  /* add to lq extension cache */
  nhdp_metric_handler[h->ext] = h;

  /* add to metric handler list */
  list_add_tail(&nhdp_metric_handler_list, &h->_node);

  for (i=0; i<4; i++) {
    h->_metric_addrtlvs[i].type = RFC5444_ADDRTLV_LINK_METRIC;
    h->_metric_addrtlvs[i].exttype = h->ext;

    rfc5444_writer_register_addrtlvtype(&_protocol->writer,
        &h->_metric_addrtlvs[i], RFC5444_MSGTYPE_HELLO);
  }

  if (nhdp_mpr_handler[h->ext]) {
    /* copy index from mpr handler */
    h->_index = nhdp_mpr_handler[h->ext]->_index;
  }
  else {
    /* initialize index and update nhdp db */
    h->_index = nhdp_db_get_metriccount();
    nhdp_db_add_metric();
  }

  /* initialize to_string method if empty */
  if (h->to_string == NULL) {
    h->to_string = _to_string;
  }

  return 0;
}

/**
 * Remove a metric handler from the nhdp metric core
 * @param h pointer to handler
 */
void
nhdp_metric_handler_remove(struct nhdp_metric_handler *h) {
  int i;

  if (!h) {
    return;
  }

  /* unregister TLV handlers */
  for (i=0; i<4; i++) {
    rfc5444_writer_unregister_addrtlvtype(&_protocol->writer,
        &h->_metric_addrtlvs[i]);
  }

  /* remove from list */
  list_remove(&h->_node);

  /* remove from cache */
  nhdp_metric_handler[h->ext] = NULL;
}

/**
 * Add a new metric handler to nhdp
 * @param h pointer to handler
 * @return 0 if successful, -1 if metric extension is already blocked
 */
int
nhdp_mpr_handler_add(struct nhdp_mpr_handler *h) {
  if (nhdp_mpr_handler[h->ext]) {
    OLSR_WARN(LOG_NHDP, "Error, mpr extension %u collision between '%s' and '%s'",
        h->ext, h->name, nhdp_mpr_handler[h->ext]->name);
    return -1;
  }

  /* add to mpr handler cache */
  nhdp_mpr_handler[h->ext] = h;

  /* add to mpr handler list */
  list_add_tail(&nhdp_mpr_handler_list, &h->_node);

  h->_mpr_addrtlv.type = RFC5444_ADDRTLV_MPR;
  h->_mpr_addrtlv.exttype = h->ext;

  rfc5444_writer_register_addrtlvtype(&_protocol->writer,
      &h->_mpr_addrtlv, RFC5444_MSGTYPE_HELLO);

  if (nhdp_metric_handler[h->ext]) {
    /* copy index from mpr handler */
    h->_index = nhdp_metric_handler[h->ext]->_index;
  }
  else {
    /* initialize index and update nhdp db */
    h->_index = nhdp_db_get_metriccount();
    nhdp_db_add_metric();
  }

  return 0;
}

/**
 * Remove a metric handler from the nhdp metric core
 * @param h pointer to handler
 */
void
nhdp_mpr_handler_remove(struct nhdp_mpr_handler *h) {
  if (!h) {
    return;
  }

  /* unregister TLV handler */
  rfc5444_writer_unregister_addrtlvtype(&_protocol->writer,
      &h->_mpr_addrtlv);

  /* remove from list */
  list_remove(&h->_node);

  /* remove from cache */
  nhdp_mpr_handler[h->ext] = NULL;
}

struct nhdp_metric_handler *
nhdp_domain_get_metric_by_ext(uint8_t ext) {
  return nhdp_metric_handler[ext];
}

struct nhdp_mpr_handler *
nhdp_domain_get_mpr_by_ext(uint8_t ext) {
  return nhdp_mpr_handler[ext];
}

/**
 * Process an incoming linkmetric tlv for a nhdp link
 * @param h pointer to metric handler
 * @param lnk pointer to nhdp link
 * @param tlvvalue value of metric tlv
 */
void
nhdp_metric_process_linktlv(struct nhdp_metric_handler *h,
    struct nhdp_link *lnk, uint16_t tlvvalue) {
  uint32_t metric;

  metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_COST_MASK);

  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_LINK) {
    lnk->_metric[h->_index].m.outgoing = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
    lnk->neigh->_metric[h->_index].m.outgoing = metric;
  }
}

/**
 * Process an incoming linkmetric tlv for a nhdp twohop neighbor
 * @param h pointer to metric handler
 * @param l2hop pointer to nhdp twohop neighbor
 * @param tlvvalue value of metric tlv
 */
void
nhdp_metric_process_2hoptlv(struct nhdp_metric_handler *h,
    struct nhdp_l2hop *l2hop, uint16_t tlvvalue) {
  uint32_t metric;

  metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_COST_MASK);

  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
    l2hop->_metric[h->_index].incoming = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_OUTGOING_NEIGH) {
    l2hop->_metric[h->_index].outgoing = metric;
  }
}

/**
 * Calculate the minimal metric cost for a neighbor
 * @param h pointer to metric handler
 * @param neigh nhdp neighbor
 */
void
nhdp_metric_calculate_neighbor_metric(
    struct nhdp_metric_handler *h,
    struct nhdp_neighbor *neigh) {
  struct nhdp_link *lnk;

  neigh->_metric[h->ext].m.incoming = RFC5444_METRIC_INFINITE;
  neigh->_metric[h->ext].m.outgoing = RFC5444_METRIC_INFINITE;

  list_for_each_element(&neigh->_links, lnk, _neigh_node) {
    if (lnk->_metric[h->ext].m.outgoing < neigh->_metric[h->ext].m.outgoing) {
      neigh->_metric[h->ext].m.outgoing = lnk->_metric[h->ext].m.outgoing;
    }
    if (lnk->_metric[h->ext].m.incoming < neigh->_metric[h->ext].m.incoming) {
      neigh->_metric[h->ext].m.incoming = lnk->_metric[h->ext].m.incoming;
    }
  }
}

void
nhdp_domain_process_mpr_tlv(struct nhdp_mpr_handler *h,
    struct nhdp_link *lnk, uint8_t tlvvalue) {
  lnk->flooding_mpr = tlvvalue == RFC5444_MPR_FLOODING
      || tlvvalue == RFC5444_MPR_FLOOD_ROUTE;
  lnk->neigh->_metric[h->_index].local_is_mpr =
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
nhdp_domain_get_willingness(void) {
  if (nhdp_db_get_metriccount() == 0) {
    return RFC5444_WILLINGNESS_UNDEFINED;
  }

  // TODO: make this configurable
  return RFC5444_WILLINGNESS_DEFAULT;
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
