
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

static struct nhdp_metric_handler _no_linkcost = {
  .name = "No link metric",
  .no_tlvs = true,

  .metric_minimum = RFC5444_METRIC_DEFAULT,
  .metric_start = RFC5444_METRIC_DEFAULT,
  .metric_maximum = RFC5444_METRIC_DEFAULT,

  .to_string = _to_string,
};

struct nhdp_metric_handler *nhdp_metric_handler[256];
struct list_entity nhdp_metric_handler_list;

static struct olsr_rfc5444_protocol *_protocol;

/**
 * Initialize nhdp metric core
 * @param p pointer to rfc5444 protocol
 */
void
nhdp_metric_init(struct olsr_rfc5444_protocol *p) {
  size_t i;

  _protocol = p;

  list_init_head(&nhdp_metric_handler_list);

  for (i=0; i<ARRAYSIZE(nhdp_metric_handler); i++) {
    nhdp_metric_handler[i] = &_no_linkcost;
  }
}

/**
 * cleanup allocated resources for nhdp metric core
 */
void
nhdp_metric_cleanup(void) {
  size_t i,j;

  for (j=0; j<ARRAYSIZE(nhdp_metric_handler); j++) {
    if (nhdp_metric_handler[j] == &_no_linkcost) {
      continue;
    }

    for (i=0; i<4; i++) {
      rfc5444_writer_unregister_addrtlvtype(&_protocol->writer,
          &nhdp_metric_handler[j]->_metric_addrtlvs[i]);
    }
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

  if (nhdp_metric_handler[h->ext] != &_no_linkcost) {
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

  /* initialize index and update nhdp db */
  h->_index = nhdp_db_get_metriccount();
  nhdp_db_add_metric();

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

  if (h == &_no_linkcost) {
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
  nhdp_metric_handler[h->ext] = &_no_linkcost;
}

#include <stdio.h>

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

  if (h == &_no_linkcost) {
    return;
  }

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

  if (h == &_no_linkcost) {
    return;
  }

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

  if (h == &_no_linkcost) {
    return;
  }

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
