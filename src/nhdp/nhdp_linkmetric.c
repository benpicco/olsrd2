/*
 * nhdp_metric.c
 *
 *  Created on: Feb 21, 2013
 *      Author: rogge
 */

#include <stdio.h>

#include "common/common_types.h"
#include "rfc5444/rfc5444_conversion.h"
#include "rfc5444/rfc5444_reader.h"
#include "core/olsr_logging.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_linkmetric.h"
#include "nhdp/nhdp.h"

static const char *_to_string(struct nhdp_linkmetric_str *, uint32_t);

static struct nhdp_linkmetric_handler _no_linkcost = {
  .name = "No link metric",
  .create_tlvs = false,

  .metric_default = {
      .incoming = RFC5444_METRIC_DEFAULT,
      .outgoing = RFC5444_METRIC_DEFAULT,
  },
  .metric_minimum = RFC5444_METRIC_DEFAULT,
  .metric_maximum = RFC5444_METRIC_DEFAULT,

  .to_string = _to_string,
};

struct nhdp_linkmetric_handler *nhdp_metric_handler[256];
struct list_entity nhdp_metric_handler_list;

static struct olsr_rfc5444_protocol *_protocol;

void
nhdp_linkmetric_init(struct olsr_rfc5444_protocol *p) {
  size_t i;

  _protocol = p;

  list_init_head(&nhdp_metric_handler_list);

  for (i=0; i<ARRAYSIZE(nhdp_metric_handler); i++) {
    nhdp_metric_handler[i] = &_no_linkcost;
  }
}

void
nhdp_linkmetric_cleanup(void) {
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

int
nhdp_linkmetric_handler_add(struct nhdp_linkmetric_handler *h) {
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

void
nhdp_linkmetric_handler_remove(struct nhdp_linkmetric_handler *h) {
  int i;

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

void
nhdp_linkmetric_process_linktlv(struct nhdp_linkmetric_handler *h,
    struct nhdp_link *lnk, uint16_t tlvvalue) {
  uint32_t metric;

  metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_FLAGS_MASK);

  if (tlvvalue & RFC5444_LINKMETRIC_OUTGOING_LINK) {
    lnk->_metric[h->_index].outgoing = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_OUTGOING_NEIGH) {
    lnk->neigh->_metric[h->_index].incoming = metric;
  }
}

void
nhdp_linkmetric_process_2hoptlv(struct nhdp_linkmetric_handler *h,
    struct nhdp_l2hop *l2hop, uint16_t tlvvalue) {
  uint32_t metric;

  metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_FLAGS_MASK);

  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_LINK) {
    l2hop->_metric[h->_index].incoming = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_OUTGOING_LINK) {
    l2hop->_metric[h->_index].outgoing = metric;
  }
}

void
nhdp_linkmetric_calculate_neighbor_metric(
    struct nhdp_linkmetric_handler *h,
    struct nhdp_neighbor *neigh) {
  struct nhdp_link *lnk;

  neigh->_metric[h->ext].incoming = RFC5444_METRIC_INFINITE;
  neigh->_metric[h->ext].outgoing = RFC5444_METRIC_INFINITE;

  list_for_each_element(&neigh->_links, lnk, _neigh_node) {
    if (lnk->_metric[h->ext].outgoing < neigh->_metric[h->ext].outgoing) {
      memcpy(&neigh->_metric[h->ext], &lnk->_metric[h->ext],
          sizeof(struct nhdp_metric));
    }
  }
}

static const char *
_to_string(struct nhdp_linkmetric_str *buf, uint32_t metric) {
    snprintf(buf->buf, sizeof(*buf), "0x%x", metric);

  return buf->buf;
}
