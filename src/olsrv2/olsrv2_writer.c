
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
#include "core/olsr_class.h"
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

static void _cb_initialize_gatewaytlv(void *);

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

/* handling of gateway TLVs (they are domain specific) */
static struct rfc5444_writer_tlvtype _gateway_addrtlvs[NHDP_MAXIMUM_DOMAINS];

static struct olsr_class_listener _domain_listener = {
  .name = "olsrv2 writer",
  .class_name = NHDP_CLASS_DOMAIN,

  .cb_add = _cb_initialize_gatewaytlv,
};

static int _send_msg_type;

static struct olsr_rfc5444_protocol *_protocol;

static enum log_source LOG_OLSRV2_W = LOG_MAIN;
static bool _cleanedup = false;

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
  _olsrv2_message->forward_target_selector = olsrv2_mpr_forwarding_selector;

  if (rfc5444_writer_register_msgcontentprovider(
      &_protocol->writer, &_olsrv2_msgcontent_provider,
      _olsrv2_addrtlvs, ARRAYSIZE(_olsrv2_addrtlvs))) {

    OLSR_WARN(LOG_OLSRV2, "Count not register OLSRv2 msg contentprovider");
    rfc5444_writer_unregister_message(&_protocol->writer, _olsrv2_message);
    return -1;
  }

  olsr_class_listener_add(&_domain_listener);
  return 0;
}

void
olsrv2_writer_cleanup(void) {
  int i;

  _cleanedup = true;

  olsr_class_listener_remove(&_domain_listener);

  /* unregister address tlvs */
  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    if (_gateway_addrtlvs[i].type) {
      rfc5444_writer_unregister_addrtlvtype(&_protocol->writer, &_gateway_addrtlvs[i]);
    }
  }

  /* remove pbb writer */
  rfc5444_writer_unregister_content_provider(
      &_protocol->writer, &_olsrv2_msgcontent_provider,
      _olsrv2_addrtlvs, ARRAYSIZE(_olsrv2_addrtlvs));
  rfc5444_writer_unregister_message(&_protocol->writer, _olsrv2_message);
}

void
olsrv2_writer_send_tc(void) {
  if (_cleanedup) {
    /* do not send more TCs during shutdown */
    return;
  }

  /* send IPv4 */
  OLSR_INFO(LOG_OLSRV2_W, "Emit IPv4 TC message.");
  _send_msg_type = AF_INET;
  olsr_rfc5444_send_all(_protocol, RFC5444_MSGTYPE_TC, _cb_tc_interface_selector);

  /* send IPv6 */
  OLSR_INFO(LOG_OLSRV2_W, "Emit IPv6 TC message.");
  _send_msg_type = AF_INET6;
  olsr_rfc5444_send_all(_protocol, RFC5444_MSGTYPE_TC, _cb_tc_interface_selector);

  _send_msg_type = AF_UNSPEC;
}

static void
_cb_initialize_gatewaytlv(void *ptr) {
  struct nhdp_domain *domain = ptr;

  _gateway_addrtlvs[domain->index].type = RFC5444_ADDRTLV_GATEWAY;
  _gateway_addrtlvs[domain->index].exttype = domain->ext;

  rfc5444_writer_register_addrtlvtype(&_protocol->writer,
      &_gateway_addrtlvs[domain->index], RFC5444_MSGTYPE_TC);
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
      OLSR_DEBUG(LOG_OLSRV2_W, "Found link with AF %s which is not dualstack",
          _send_msg_type == AF_INET ? "ipv4" : "ipv6");
      return true;
    }
    if (nhdp_db_link_is_ipv6_dualstack(lnk)) {
      /* prefer IPv6 for dualstack neighbors */
      OLSR_DEBUG(LOG_OLSRV2_W, "Found link with AF ipv6 which is dualstack");

      return true;
    }
  }

  /* nothing to do with this interface */
  return false;
}

static void
_cb_addMessageTLVs(struct rfc5444_writer *writer) {
  uint8_t vtime_encoded, itime_encoded;

  /* generate validity time and interval time */
  itime_encoded = rfc5444_timetlv_encode(olsrv2_get_tc_interval());
  vtime_encoded = rfc5444_timetlv_encode(olsrv2_get_tc_validity());

  /* allocate space for ANSN tlv */
  rfc5444_writer_allocate_messagetlv(writer, true, 2);

  /* add validity and interval time TLV */
  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_VALIDITY_TIME, 0,
      &vtime_encoded, sizeof(vtime_encoded));
  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_INTERVAL_TIME, 0,
      &itime_encoded, sizeof(itime_encoded));
}

static void
_cb_addAddresses(struct rfc5444_writer *writer) {
  const struct netaddr_acl *routable_acl;
  struct rfc5444_writer_address *addr;
  struct nhdp_neighbor *neigh;
  struct nhdp_naddr *naddr;

  struct nhdp_neighbor_domaindata *neigh_domain;
  struct nhdp_domain *domain;

  struct olsrv2_lan_entry *lan;
  bool any_advertised;
  uint8_t nbr_addrtype_value;

  uint16_t metric_in, metric_out;

  struct netaddr_str buf;

  routable_acl = olsrv2_get_routable();

  /* iterate over neighbors */
  list_for_each_element(&nhdp_neigh_list, neigh, _global_node) {
    any_advertised = false;
    /* calculate advertised array */
    list_for_each_element(&nhdp_domain_list, domain, _node) {
      if (nhdp_domain_get_neighbordata(domain, neigh)->neigh_is_mpr) {
        any_advertised = true;
        break;
      }
    }

    if (!any_advertised) {
      /* neighbor is not advertised */
      OLSR_DEBUG(LOG_OLSRV2_W, "Unadvertised neighbor");
      continue;
    }

    /* iterate over neighbors addresses */
    avl_for_each_element(&neigh->_neigh_addresses, naddr, _neigh_node) {
      if (netaddr_get_address_family(&naddr->neigh_addr) != _send_msg_type) {
        /* wrong address family */
        OLSR_DEBUG(LOG_OLSRV2_W, "Wrong address type of neighbor %s",
            netaddr_to_string(&buf, &naddr->neigh_addr));
        continue;
      }

      nbr_addrtype_value = 0;

      if (netaddr_acl_check_accept(routable_acl, &naddr->neigh_addr)) {
        nbr_addrtype_value += RFC5444_NBR_ADDR_TYPE_ROUTABLE;
      }
      if (netaddr_cmp(&neigh->originator, &naddr->neigh_addr) == 0) {
        nbr_addrtype_value += RFC5444_NBR_ADDR_TYPE_ORIGINATOR;
      }

      if (nbr_addrtype_value == 0) {
        /* skip this address */
        OLSR_DEBUG(LOG_OLSRV2_W, "Address %s is neither routable"
            " nor an originator", netaddr_to_string(&buf, &naddr->neigh_addr));
        continue;
      }

      OLSR_DEBUG(LOG_OLSRV2_W, "Add address %s to TC",
          netaddr_to_string(&buf, &naddr->neigh_addr));
      addr = rfc5444_writer_add_address(writer, _olsrv2_msgcontent_provider.creator,
          netaddr_get_binptr(&naddr->neigh_addr),
          netaddr_get_prefix_length(&naddr->neigh_addr), false);
      if (addr == NULL) {
        OLSR_WARN(LOG_OLSRV2_W, "Out of memory error for olsrv2 address");
        return;
      }

      /* add neighbor type TLV */
      OLSR_DEBUG(LOG_OLSRV2_W, "Add NBRAddrType TLV with value %u", nbr_addrtype_value);
      rfc5444_writer_add_addrtlv(writer, addr, &_olsrv2_addrtlvs[IDX_ADDRTLV_NBR_ADDR_TYPE],
          &nbr_addrtype_value, sizeof(nbr_addrtype_value), false);

      /* add linkmetric TLVs */
      list_for_each_element(&nhdp_domain_list, domain, _node) {
        neigh_domain = nhdp_domain_get_neighbordata(domain, neigh);

        metric_in = rfc5444_metric_encode(neigh_domain->metric.in);
        metric_out = rfc5444_metric_encode(neigh_domain->metric.out);

        if (!nhdp_domain_get_neighbordata(domain, neigh)->neigh_is_mpr) {
          /* just put in an empty metric so we don't need to start a second TLV */
          metric_in = 0;

          OLSR_DEBUG(LOG_OLSRV2_W, "Add Linkmetric (ext %u) TLV with value 0x%04x",
              domain->ext, metric_in);
          rfc5444_writer_add_addrtlv(writer, addr, &domain->_metric_addrtlvs[0],
              &metric_in, sizeof(metric_in), true);
        }
        else if (metric_in == metric_out) {
          /* incoming and outgoing metric are the same */
          metric_in |= RFC5444_LINKMETRIC_INCOMING_NEIGH;
          metric_in |= RFC5444_LINKMETRIC_OUTGOING_NEIGH;

          OLSR_DEBUG(LOG_OLSRV2_W, "Add Linkmetric (ext %u) TLV with value 0x%04x",
              domain->ext, metric_in);
          rfc5444_writer_add_addrtlv(writer, addr, &domain->_metric_addrtlvs[0],
              &metric_in, sizeof(metric_in), true);
        }
        else {
          /* different metrics for incoming and outgoing link */
          metric_in |= RFC5444_LINKMETRIC_INCOMING_NEIGH;
          metric_out |= RFC5444_LINKMETRIC_OUTGOING_NEIGH;

          OLSR_DEBUG(LOG_OLSRV2_W, "Add Linkmetric (ext %u) TLV with value 0x%04x",
              domain->ext, metric_in);
          rfc5444_writer_add_addrtlv(writer, addr, &domain->_metric_addrtlvs[0],
              &metric_in, sizeof(metric_in), true);

          OLSR_DEBUG(LOG_OLSRV2_W, "Add Linkmetric (ext %u) TLV with value 0x%04x",
              domain->ext, metric_out);
          rfc5444_writer_add_addrtlv(writer, addr, &domain->_metric_addrtlvs[1],
              &metric_out, sizeof(metric_out), true);
        }
      }
    }
  }

  /* Iterate over locally attached networks */
  avl_for_each_element(&olsrv2_lan_tree, lan, _node) {
    if (netaddr_get_address_family(&lan->prefix) != _send_msg_type) {
      /* wrong address family */
      continue;
    }

    OLSR_DEBUG(LOG_OLSRV2_W, "Add address %s to TC",
        netaddr_to_string(&buf, &lan->prefix));
    addr = rfc5444_writer_add_address(writer, _olsrv2_msgcontent_provider.creator,
        netaddr_get_binptr(&lan->prefix),
        netaddr_get_prefix_length(&lan->prefix), false);
    if (addr == NULL) {
      OLSR_WARN(LOG_OLSRV2_W, "Out of memory error for olsrv2 address");
      return;
    }

    /* add Gateway TLV and Metric TLV */
    list_for_each_element(&nhdp_domain_list, domain, _node) {
      metric_out = rfc5444_metric_encode(lan->data[domain->index].outgoing_metric);
      metric_out |= RFC5444_LINKMETRIC_OUTGOING_NEIGH;

      /* add Metric TLV */
      OLSR_DEBUG(LOG_OLSRV2_W, "Add Linkmetric (ext %u) TLV with value 0x%04x",
          domain->ext, metric_out);
      rfc5444_writer_add_addrtlv(writer, addr, &domain->_metric_addrtlvs[0],
          &metric_out, sizeof(metric_out), false);

      /* add Gateway TLV */
      OLSR_DEBUG(LOG_OLSRV2_W, "Add Gateway (ext %u) TLV with value 0x%04x",
          domain->ext, metric_in);
      rfc5444_writer_add_addrtlv(writer, addr, &_gateway_addrtlvs[domain->index],
          &lan->data[domain->index]. distance, 1, false);
    }
  }
}

static void
_cb_finishMessageTLVs(struct rfc5444_writer *writer,
    struct rfc5444_writer_address *start __attribute__((unused)),
    struct rfc5444_writer_address *end __attribute__((unused)),
    bool complete) {
  uint16_t ansn;

  /* get ANSN */
  ansn = htons(olsrv2_update_ansn());

  rfc5444_writer_set_messagetlv(writer, RFC5444_MSGTLV_CONT_SEQ_NUM,
      complete ? RFC5444_CONT_SEQ_NUM_COMPLETE : RFC5444_CONT_SEQ_NUM_INCOMPLETE,
      &ansn, sizeof(ansn));
}
