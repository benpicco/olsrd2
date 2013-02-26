/*
 * nhdp_metric.c
 *
 *  Created on: Feb 21, 2013
 *      Author: rogge
 */

#include "common/common_types.h"
#include "rfc5444/rfc5444_reader.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_linkmetric.h"

static void _cb_get_link_metric(struct nhdp_metric *,
    struct nhdp_link *);
static void _cb_get_neighbor_metric(struct nhdp_metric *,
    struct nhdp_neighbor *);
static void _cb_process_tlv(struct nhdp_link *, uint16_t);

static struct nhdp_linkmetric_handler _no_linkcost = {
  .name = "No link metric",
  .ext = 0,
  .create_tlvs = false,

  .get_link_metric = _cb_get_link_metric,
  .get_neighbor_metric = _cb_get_neighbor_metric,
  .process_linkmetric_tlv = _cb_process_tlv,
};

static struct nhdp_linkmetric_handler *_handler = &_no_linkcost;

static struct olsr_rfc5444_protocol *_protocol;

void
nhdp_linkmetric_init(struct olsr_rfc5444_protocol *p) {
  _protocol = p;
}

void
nhdp_linkmetric_cleanup(void) {
}


struct nhdp_linkmetric_handler *
nhdp_linkmetric_handler_get(void) {
  return _handler;
}

void
nhdp_linkmetric_handler_add(struct nhdp_linkmetric_handler *h) {
  int i;

  if (_handler != &_no_linkcost) {
    nhdp_linkmetric_handler_remove(_handler);
  }

  _handler = h;

  for (i=0; i<4; i++) {
    h->_metric_addrtlvs[i].type = RFC5444_ADDRTLV_LINK_METRIC;
    h->_metric_addrtlvs[i].exttype = h->ext;

    rfc5444_writer_register_addrtlvtype(&_protocol->writer,
        &h->_metric_addrtlvs[i], RFC5444_MSGTYPE_HELLO);
  }
}

void
nhdp_linkmetric_handler_remove(
    struct nhdp_linkmetric_handler *h) {
  int i;

  if (h != _handler) {
    return;
  }

  for (i=0; i<4; i++) {
    rfc5444_writer_unregister_addrtlvtype(&_protocol->writer,
        &h->_metric_addrtlvs[i]);
  }
  _handler = &_no_linkcost;
}

void
nhdp_linkmetric_calculate_neighbor_metric(
    struct nhdp_neighbor *neigh, struct nhdp_metric *metric) {
  struct nhdp_link *lnk;
  struct nhdp_metric lmetric;

  metric->incoming = RFC5444_LINKMETRIC_INFINITE;
  metric->outgoing = RFC5444_LINKMETRIC_INFINITE;

  list_for_each_element(&neigh->_links, lnk, _neigh_node) {
    _handler->get_link_metric(&lmetric, lnk);

    if (lmetric.outgoing < metric->outgoing) {
      memcpy(metric, &lmetric, sizeof(*metric));
    }
  }
}

static void
_cb_process_tlv(struct nhdp_link *lnk __attribute__((unused)),
    uint16_t value __attribute__((unused))) {
}

static void
_cb_get_link_metric(struct nhdp_metric *m,
    struct nhdp_link *lnk __attribute__((unused))) {
  m->incoming = 0x10000;
  m->outgoing = 0x10000;
}

static void
_cb_get_neighbor_metric(struct nhdp_metric *m,
    struct nhdp_neighbor *neigh __attribute__((unused))) {
  m->incoming = 0x10000;
  m->outgoing = 0x10000;
}
