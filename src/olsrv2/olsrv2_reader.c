
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

#include "common/common_types.h"
#include "common/netaddr.h"
#include "rfc5444/rfc5444.h"
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444_reader.h"
#include "core/olsr_logging.h"
#include "core/olsr_subsystem.h"
#include "tools/olsr_duplicate_set.h"
#include "tools/olsr_rfc5444.h"

#include "olsrv2/olsrv2.h"
#include "olsrv2/olsrv2_originator.h"
#include "olsrv2/olsrv2_reader.h"
#include "olsrv2/olsrv2_routing.h"
#include "olsrv2/olsrv2_tc.h"

/* NHDP message TLV array index */
enum {
  IDX_TLV_ITIME,
  IDX_TLV_VTIME,
  IDX_TLV_CONT_SEQ_NUM,
};

/* NHDP address TLV array index pass 1 */
enum {
  IDX_ADDRTLV_LINK_METRIC,
  IDX_ADDRTLV_NBR_ADDR_TYPE,
  IDX_ADDRTLV_GATEWAY,
};

struct _olsrv2_data {
  struct olsrv2_tc_node *node;

  uint64_t vtime;

  bool complete_tc;
};

static enum rfc5444_result
_cb_messagetlvs(struct rfc5444_reader_tlvblock_context *context);

static enum rfc5444_result
_cb_addresstlvs(struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result _cb_messagetlvs_end(
    struct rfc5444_reader_tlvblock_context *context, bool dropped);

/* definition of the RFC5444 reader components */
static struct rfc5444_reader_tlvblock_consumer _olsrv2_message_consumer = {
  .order = RFC5444_MAIN_PARSER_PRIORITY,
  .msg_id = RFC5444_MSGTYPE_TC,
  .block_callback = _cb_messagetlvs,
  .end_callback = _cb_messagetlvs_end,
};

static struct rfc5444_reader_tlvblock_consumer_entry _olsrv2_message_tlvs[] = {
  [IDX_TLV_ITIME] = { .type = RFC5444_MSGTLV_INTERVAL_TIME, .type_ext = 0, .match_type_ext = true,
      .min_length = 1, .max_length = 511, .match_length = true },
  [IDX_TLV_VTIME] = { .type = RFC5444_MSGTLV_VALIDITY_TIME, .type_ext = 0, .match_type_ext = true,
      .mandatory = true, .min_length = 1, .max_length = 511, .match_length = true },
  [IDX_TLV_CONT_SEQ_NUM] = { .type = RFC5444_MSGTLV_CONT_SEQ_NUM,
      .mandatory = true, .min_length = 2, .match_length = true },
};

static struct rfc5444_reader_tlvblock_consumer _olsrv2_address_consumer = {
  .order = RFC5444_MAIN_PARSER_PRIORITY,
  .msg_id = RFC5444_MSGTYPE_TC,
  .addrblock_consumer = true,
  .block_callback = _cb_addresstlvs,
};

static struct rfc5444_reader_tlvblock_consumer_entry _olsrv2_address_tlvs[] = {
  [IDX_ADDRTLV_LINK_METRIC] = { .type = RFC5444_ADDRTLV_LINK_METRIC,
    .min_length = 2, .match_length = true },
  [IDX_ADDRTLV_NBR_ADDR_TYPE] = { .type = RFC5444_ADDRTLV_NBR_ADDR_TYPE,
    .min_length = 1, .match_length = true },
  [IDX_ADDRTLV_GATEWAY] = { .type = RFC5444_ADDRTLV_GATEWAY,
    .min_length = 1, .match_length = true },
};

/* nhdp multiplexer/protocol */
static struct olsr_rfc5444_protocol *_protocol = NULL;

static enum log_source LOG_OLSRV2_R = LOG_MAIN;

static struct _olsrv2_data _current;

/**
 * Initialize nhdp reader
 */
void
olsrv2_reader_init(struct olsr_rfc5444_protocol *p) {
  _protocol = p;

  LOG_OLSRV2_R = olsr_log_register_source("olsrv2_r");

  rfc5444_reader_add_message_consumer(
      &_protocol->reader, &_olsrv2_message_consumer,
      _olsrv2_message_tlvs, ARRAYSIZE(_olsrv2_message_tlvs));
  rfc5444_reader_add_message_consumer(
      &_protocol->reader, &_olsrv2_address_consumer,
      _olsrv2_address_tlvs, ARRAYSIZE(_olsrv2_address_tlvs));
}

/**
 * Cleanup nhdp reader
 */
void
olsrv2_reader_cleanup(void) {
  rfc5444_reader_remove_message_consumer(
      &_protocol->reader, &_olsrv2_address_consumer);
  rfc5444_reader_remove_message_consumer(
      &_protocol->reader, &_olsrv2_message_consumer);
}

static enum rfc5444_result
_cb_messagetlvs(struct rfc5444_reader_tlvblock_context *context) {
  uint64_t itime;
  uint16_t ansn;
  uint8_t tmp;
  struct netaddr_str buf;

  OLSR_DEBUG(LOG_OLSRV2_R, "Received TC from %s",
      netaddr_to_string(&buf, _protocol->input_address));

  if (!context->has_origaddr || !context->has_hopcount
      || !context->has_hoplimit || !context->has_seqno) {
    OLSR_DEBUG(LOG_OLSRV2_R, "Missing message flag");
    return RFC5444_DROP_MESSAGE;
  }

  if (olsrv2_originator_is_local(&context->orig_addr)) {
    OLSR_DEBUG(LOG_OLSRV2_R, "We are hearing ourself");
    return RFC5444_DROP_MESSAGE;
  }

  OLSR_DEBUG(LOG_OLSRV2_R, "Originator: %s   Seqno: %u",
      netaddr_to_string(&buf, &context->orig_addr), context->seqno);

  /* clear session data */
  memset(&_current, 0, sizeof(_current));

  /* get cont_seq_num extension */
  tmp = _olsrv2_message_tlvs[IDX_TLV_CONT_SEQ_NUM].type_ext;
  if (tmp != RFC5444_CONT_SEQ_NUM_COMPLETE
      && tmp != RFC5444_CONT_SEQ_NUM_INCOMPLETE) {
    OLSR_DEBUG(LOG_OLSRV2_R, "Illegal extension of CONT_SEQ_NUM TLV: %u",
        tmp);
    return RFC5444_DROP_MESSAGE;
  }
  _current.complete_tc = tmp == RFC5444_CONT_SEQ_NUM_COMPLETE;

  /* get ANSN */
  memcpy(&ansn,
      _olsrv2_message_tlvs[IDX_TLV_CONT_SEQ_NUM].tlv->single_value, 2);
  ansn = ntohs(ansn);

  /* get VTime/ITime */
  tmp = rfc5444_timetlv_get_from_vector(
      _olsrv2_message_tlvs[IDX_TLV_VTIME].tlv->single_value,
      _olsrv2_message_tlvs[IDX_TLV_VTIME].tlv->length,
      context->hopcount);
  _current.vtime = rfc5444_timetlv_decode(tmp);

  if (_olsrv2_message_tlvs[IDX_TLV_ITIME].tlv) {
    tmp = rfc5444_timetlv_get_from_vector(
        _olsrv2_message_tlvs[IDX_TLV_ITIME].tlv->single_value,
        _olsrv2_message_tlvs[IDX_TLV_ITIME].tlv->length,
        context->hopcount);
    itime = rfc5444_timetlv_decode(tmp);
  }
  else {
    itime = 0;
  }

  /* test if we already forwarded the message */
  if (!olsrv2_mpr_shall_forwarding(context, _current.vtime)) {
    /* mark message as 'no forward */
    rfc5444_reader_prevent_forwarding(context);
  }

  /* test if we already processed the message */
  if (!olsrv2_mpr_shall_process(context, _current.vtime)) {
    OLSR_DEBUG(LOG_OLSRV2_R, "Processing set says 'do not process'");
    return RFC5444_DROP_MESSAGE;
  }

  /* get tc node */
  _current.node = olsrv2_tc_node_add(
      &context->orig_addr, _current.vtime, ansn);
  if (_current.node == NULL) {
    OLSR_DEBUG(LOG_OLSRV2_R, "Cannot create node");
    return RFC5444_DROP_MESSAGE;
  }

  /* check if the topology information is recent enough */
  if (rfc5444_seqno_is_smaller(ansn, _current.node->ansn)) {
    OLSR_DEBUG(LOG_OLSRV2_R, "ANSN %u is smaller than last stored ANSN %u",
        ansn, _current.node->ansn);
    return RFC5444_DROP_MESSAGE;
  }

  /* overwrite old ansn */
  _current.node->ansn = ansn;

  /* reset validity time and interval time */
  olsr_timer_set(&_current.node->_validity_time, _current.vtime);
  _current.node->interval_time = itime;

  /* continue parsing the message */
  return RFC5444_OKAY;
}

static enum rfc5444_result
_cb_addresstlvs(struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  struct rfc5444_reader_tlvblock_entry *tlv;
  struct nhdp_domain *domain;
  struct olsrv2_tc_edge *edge;
  struct olsrv2_tc_attached_endpoint *end;
  uint32_t cost_in[NHDP_MAXIMUM_DOMAINS];
  uint32_t cost_out[NHDP_MAXIMUM_DOMAINS];
  uint16_t tmp;
  struct netaddr_str buf;

  if (_current.node == NULL) {
    return RFC5444_OKAY;
  }

  for (int i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    cost_in[i] = RFC5444_METRIC_INFINITE;
    cost_out[i] = RFC5444_METRIC_INFINITE;
  }

  for (tlv = _olsrv2_address_tlvs[IDX_ADDRTLV_LINK_METRIC].tlv;
      tlv; tlv = tlv->next_entry) {
    domain = nhdp_domain_get_by_ext(tlv->type_ext);
    if (domain == NULL) {
      continue;
    }

    memcpy(&tmp, tlv->single_value, 2);
    // TODO tmp = ntohs(tmp);

    OLSR_DEBUG(LOG_OLSRV2_R, "Metric %d: %04x",
        domain->index, tmp);

    if (tmp & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
      cost_in[domain->index] =
          rfc5444_metric_decode(tmp & RFC5444_LINKMETRIC_COST_MASK);
    }

    if (tmp & RFC5444_LINKMETRIC_OUTGOING_NEIGH) {
      cost_out[domain->index] =
          rfc5444_metric_decode(tmp & RFC5444_LINKMETRIC_COST_MASK);
    }
  }

  for (tlv = _olsrv2_address_tlvs[IDX_ADDRTLV_NBR_ADDR_TYPE].tlv;
      tlv; tlv = tlv->next_entry) {
    /* find routing domain */
    domain = nhdp_domain_get_by_ext(tlv->type_ext);
    if (domain == NULL) {
      continue;
    }

    /* parse originator neighbor */
    if (tlv->single_value[0] == RFC5444_NBR_ADDR_TYPE_ORIGINATOR
        || tlv->single_value[0] == RFC5444_NBR_ADDR_TYPE_ROUTABLE_ORIG) {
      edge = olsrv2_tc_edge_add(_current.node, &context->addr);
      if (edge) {
        OLSR_DEBUG(LOG_OLSRV2_R, "Originator %s: ansn=%u metric=%d/%d",
            netaddr_to_string(&buf, &context->addr),
            _current.node->ansn,
            cost_out[domain->index], cost_in[domain->index]);
        edge->ansn = _current.node->ansn;
        edge->cost[domain->index] = cost_out[domain->index];

        if (edge->inverse->virtual) {
          edge->inverse->cost[domain->index] = cost_in[domain->index];
        }
      }
    }

    /* parse routable neighbor (which is not an originator) */
    if (tlv->single_value[0] == RFC5444_NBR_ADDR_TYPE_ROUTABLE) {
      end = olsrv2_tc_endpoint_add(_current.node, &context->addr, true);
      if (end) {
        OLSR_DEBUG(LOG_OLSRV2_R, "Routable %s: ansn=%u metric=%u",
            netaddr_to_string(&buf, &context->addr),
            _current.node->ansn,
            cost_out[domain->index]);
        end->ansn = _current.node->ansn;
        end->cost[domain->index] = cost_out[domain->index];
      }
    }
  }

  for (tlv = _olsrv2_address_tlvs[IDX_ADDRTLV_GATEWAY].tlv;
      tlv; tlv = tlv->next_entry) {
    /* find routing domain */
    domain = nhdp_domain_get_by_ext(tlv->type_ext);
    if (domain == NULL) {
      continue;
    }

    /* parse attached network */
    end = olsrv2_tc_endpoint_add(_current.node, &context->addr, false);
    if (end) {
      OLSR_DEBUG(LOG_OLSRV2_R, "Attached %s: ansn=%u metric=%u dist=%u",
          netaddr_to_string(&buf, &context->addr),
          _current.node->ansn,
          cost_out[domain->index],
          tlv->single_value[0]);
      end->ansn = _current.node->ansn;
      end->cost[domain->index] = cost_out[domain->index];
      end->distance[domain->index] = tlv->single_value[0];
    }
  }
  return RFC5444_OKAY;
}

static enum rfc5444_result
_cb_messagetlvs_end(struct rfc5444_reader_tlvblock_context *context __attribute__((unused)),
    bool dropped) {
  /* cleanup everything that is not the current ANSN */
  struct olsrv2_tc_edge *edge, *edge_it;
  struct olsrv2_tc_attached_endpoint *end, *end_it;

  if (dropped || _current.node == NULL) {
    return RFC5444_OKAY;
  }

  avl_for_each_element_safe(&_current.node->_edges, edge, _node, edge_it) {
    if (edge->ansn != _current.node->ansn) {
      olsrv2_tc_edge_remove(edge);
    }
  }

  avl_for_each_element_safe(&_current.node->_endpoints, end, _src_node, end_it) {
    if (end->ansn != _current.node->ansn) {
      olsrv2_tc_endpoint_remove(end);
    }
  }

  _current.node = NULL;

  /* Update routing table */
  olsrv2_routing_update();

  return RFC5444_OKAY;
}
