
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
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444.h"
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_logging.h"
#include "tools/olsr_rfc5444.h"

#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_domain.h"

#include "olsrv2/olsrv2.h"
#include "olsrv2/olsrv2_lan.h"
#include "olsrv2/olsrv2_originator.h"
#include "olsrv2/olsrv2_writer.h"

/* constants */
enum {
  IDX_ADDRTLV_NBR_ADDR_TYPE,
  IDX_ADDRTLV_GATEWAY,
};

/* Prototypes */
static bool _cb_tc_interface_selector(struct rfc5444_writer *,
    struct rfc5444_writer_target *rfc5444_target, void *ptr);

static void _cb_addMessageHeader(
    struct rfc5444_writer *, struct rfc5444_writer_message *);
static void _cb_addMessageTLVs(struct rfc5444_writer *);
static void _cb_addAddresses(struct rfc5444_writer *);
static void _cb_finishMessageTLVs(struct rfc5444_writer *,
  struct rfc5444_writer_address *start,
  struct rfc5444_writer_address *end, bool complete);

/* definition of NHDP writer */
static struct rfc5444_writer_message *_olsrv2_message = NULL;

static struct rfc5444_writer_content_provider _olsrv2_msgcontent_provider = {
  .msg_type = RFC5444_MSGTYPE_TC,
  .addMessageTLVs = _cb_addMessageTLVs,
  .addAddresses = _cb_addAddresses,
  .finishMessageTLVs = _cb_finishMessageTLVs,
};

static struct rfc5444_writer_tlvtype _olsrv2_addrtlvs[] = {
  [IDX_ADDRTLV_NBR_ADDR_TYPE] =  { .type = RFC5444_ADDRTLV_NBR_ADDR_TYPE },
  [IDX_ADDRTLV_GATEWAY]       =  { .type = RFC5444_ADDRTLV_GATEWAY },
};

static int _send_msg_type;

static struct olsr_rfc5444_protocol *_protocol;

static enum log_source LOG_OLSRV2_W = LOG_MAIN;

int
olsrv2_writer_init(struct olsr_rfc5444_protocol *protocol) {
  _protocol = protocol;

  LOG_OLSRV2_W = olsr_log_register_source("olsrv2_w");

  _olsrv2_message = rfc5444_writer_register_message(
      &_protocol->writer, RFC5444_MSGTYPE_TC, true, 4);
  if (_olsrv2_message == NULL) {
    OLSR_WARN(LOG_OLSRV2, "Could not register OLSRv2 TC message");
    return -1;
  }

  _olsrv2_message->addMessageHeader = _cb_addMessageHeader;

  if (rfc5444_writer_register_msgcontentprovider(
      &_protocol->writer, &_olsrv2_msgcontent_provider,
      _olsrv2_addrtlvs, ARRAYSIZE(_olsrv2_addrtlvs))) {

    OLSR_WARN(LOG_OLSRV2, "Count not register OLSRv2 msg contentprovider");
    rfc5444_writer_unregister_message(&_protocol->writer, _olsrv2_message);
    return -1;
  }
  return 0;
}

void
olsrv2_writer_cleanup(void) {
  /* remove pbb writer */
  rfc5444_writer_unregister_content_provider(
      &_protocol->writer, &_olsrv2_msgcontent_provider,
      _olsrv2_addrtlvs, ARRAYSIZE(_olsrv2_addrtlvs));
  rfc5444_writer_unregister_message(&_protocol->writer, _olsrv2_message);
}

void
olsrv2_writer_send_tc(void) {
  /* send IPv4 */
  _send_msg_type = AF_INET;
  olsr_rfc5444_send_all(_protocol, RFC5444_MSGTYPE_TC, _cb_tc_interface_selector);

  /* send IPv6 */
  _send_msg_type = AF_INET;
  olsr_rfc5444_send_all(_protocol, RFC5444_MSGTYPE_TC, _cb_tc_interface_selector);

  _send_msg_type = AF_UNSPEC;
}

static void
_cb_addMessageHeader(struct rfc5444_writer *writer,
    struct rfc5444_writer_message *message) {
  const struct netaddr *orig;

  orig = olsrv2_originator_get(_send_msg_type);

  /* initialize message header */
  rfc5444_writer_set_msg_header(writer, message, true, true, true, true);
  rfc5444_writer_set_msg_addrlen(writer, message, netaddr_get_binlength(orig));
  rfc5444_writer_set_msg_originator(writer, message, netaddr_get_binptr(orig));
  rfc5444_writer_set_msg_hopcount(writer, message, 0);
  rfc5444_writer_set_msg_hoplimit(writer, message, 255);
  rfc5444_writer_set_msg_seqno(writer, message,
      olsr_rfc5444_get_next_message_seqno(_protocol));

  OLSR_DEBUG(LOG_OLSRV2_W, "Generate TC");
}

static void
_cb_addMessageTLVs(struct rfc5444_writer *writer) {
  uint8_t vtime_encoded, itime_encoded;
  struct nhdp_domain *domain;

  /* generate validity time and interval time */
  itime_encoded = rfc5444_timetlv_encode(olsrv2_get_tc_interval());
  vtime_encoded = rfc5444_timetlv_encode(olsrv2_get_tc_validity());

  /* update metric version numbers */
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    nhdp_domain_update_metric_version(domain);
  }

  /* allocate space for ANSN tlv */
  rfc5444_writer_allocate_messagetlv(writer, true,
      sizeof(uint16_t) * nhdp_domain_get_count());

  /* add validity and interval time TLV */
  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_VALIDITY_TIME, 0,
      &vtime_encoded, sizeof(vtime_encoded));
  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_INTERVAL_TIME, 0,
      &itime_encoded, sizeof(itime_encoded));
}

/**
 * Selector for outgoing target
 * @param writer rfc5444 writer
 * @param target rfc5444 target
 * @param ptr custom pointer, contains rfc5444 target
 * @return true if target corresponds to selection
 */
static bool
_cb_tc_interface_selector(struct rfc5444_writer *writer __attribute__((unused)),
    struct rfc5444_writer_target *rfc5444_target, void *ptr __attribute__((unused))) {
  struct olsr_rfc5444_target *target;
  struct nhdp_interface *interf;
  struct nhdp_link *lnk;
  int target_af_type;

  target = container_of(rfc5444_target, struct olsr_rfc5444_target, rfc5444_target);

  if (target == target->interface->multicast4) {
    target_af_type = AF_INET;
  }
  else if (target == target->interface->multicast6) {
    target_af_type = AF_INET6;
  }
  else {
    /* do not use unicast targets with this selector */
    return false;
  }

  interf = nhdp_interface_get(target->interface->name);
  if (interf == NULL) {
    /* unknown interface */
    return false;
  }

  if (list_is_empty(&interf->_links)) {
    /* no neighbor */
    return false;
  }

  /*
   * search for a link beyond the interface that is
   * symmetric and needs a message of the specified address type
   * on this target type
   */
  list_for_each_element(&interf->_links, lnk, _if_node) {
    if (lnk->status != NHDP_LINK_SYMMETRIC) {
      /* link is not symmetric */
      continue;
    }
    if (netaddr_get_address_family(&lnk->neigh->originator) != target_af_type) {
      /* link cannot receive this targets address type */
      continue;
    }
    if (netaddr_get_address_family(&lnk->neigh->originator) == _send_msg_type
        && lnk->dualstack_partner == NULL) {
      /* link type is right and node is not dualstack */
      return true;
    }
    if (nhdp_db_link_is_ipv6_dualstack(lnk)) {
      /* prefer IPv6 for dualstack neighbors */
      return true;
    }
  }

  /* nothing to do with this interface */
  return false;
}

static void
_cb_addAddresses(struct rfc5444_writer *writer __attribute__((unused))) {

}

static void
_cb_finishMessageTLVs(struct rfc5444_writer *writer,
    struct rfc5444_writer_address *start __attribute__((unused)),
    struct rfc5444_writer_address *end __attribute__((unused)),
    bool complete) {
  struct nhdp_domain *domain;
  uint16_t ansn[NHDP_MAXIMUM_DOMAINS];

  /* calculate answer set array */
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    ansn[domain->index] = htons(domain->version);
  }

  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_CONT_SEQ_NUM,
      complete ? RFC5444_CONT_SEQ_NUM_COMPLETE : RFC5444_CONT_SEQ_NUM_INCOMPLETE,
      ansn, sizeof(uint16_t) * nhdp_domain_get_count());
}
