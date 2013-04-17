
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
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444.h"
#include "rfc5444/rfc5444_reader.h"
#include "core/olsr_logging.h"
#include "core/olsr_subsystem.h"
#include "tools/olsr_rfc5444.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_hysteresis.h"
#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp_reader.h"

#ifdef RIOT
#include "sys/net/net_help/net_help.h"
#endif

/* NHDP message TLV array index */
enum {
  IDX_TLV_ITIME,
  IDX_TLV_VTIME,
  IDX_TLV_WILLINGNESS,
  IDX_TLV_IPV6ORIG,
};

/* NHDP address TLV array index pass 1 */
enum {
  IDX_ADDRTLV1_LOCAL_IF,
  IDX_ADDRTLV1_LINK_STATUS,
};

/* NHDP address TLV array index pass 2 */
enum {
  IDX_ADDRTLV2_LOCAL_IF,
  IDX_ADDRTLV2_LINK_STATUS,
  IDX_ADDRTLV2_OTHER_NEIGHB,
  IDX_ADDRTLV2_MPR,
  IDX_ADDRTLV2_LINKMETRIC,
};

/* prototypes */
static void cleanup_error(void);
static enum rfc5444_result _pass2_process_localif(struct netaddr *addr, uint8_t local_if);
static void _handle_originator(struct rfc5444_reader_tlvblock_context *context);

static enum rfc5444_result
_cb_messagetlvs(struct rfc5444_reader_tlvblock_context *context);

static enum rfc5444_result
_cb_addresstlvs_pass1(struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result _cb_addresstlvs_pass1_end(
    struct rfc5444_reader_tlvblock_context *context, bool dropped);

static enum rfc5444_result _cb_addr_pass2_block(
      struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result _cb_msg_pass2_end(
    struct rfc5444_reader_tlvblock_context *context, bool dropped);

/* definition of the RFC5444 reader components */
static struct rfc5444_reader_tlvblock_consumer _nhdp_message_pass1_consumer = {
  .order = RFC5444_MAIN_PARSER_PRIORITY,
  .msg_id = RFC5444_MSGTYPE_HELLO,
  .block_callback = _cb_messagetlvs,
  .end_callback = _cb_addresstlvs_pass1_end,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_message_tlvs[] = {
  [IDX_TLV_ITIME] = { .type = RFC5444_MSGTLV_INTERVAL_TIME, .type_ext = 0, .match_type_ext = true,
      .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_VTIME] = { .type = RFC5444_MSGTLV_VALIDITY_TIME, .type_ext = 0, .match_type_ext = true,
      .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_WILLINGNESS] = { .type = RFC5444_MSGTLV_MPR_WILLING, .type_ext = 0, .match_type_ext = true,
    .min_length = 1, .match_length = true },
  [IDX_TLV_IPV6ORIG] = { .type = NHDP_MSGTLV_IPV6ORIGINATOR, .type_ext = 0, .match_type_ext = true,
      .min_length = 16, .match_length = true },
};

static struct rfc5444_reader_tlvblock_consumer _nhdp_address_pass1_consumer = {
  .order = RFC5444_MAIN_PARSER_PRIORITY,
  .msg_id = RFC5444_MSGTYPE_HELLO,
  .addrblock_consumer = true,
  .block_callback = _cb_addresstlvs_pass1,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_address_pass1_tlvs[] = {
  [IDX_ADDRTLV1_LOCAL_IF] = { .type = RFC5444_ADDRTLV_LOCAL_IF, .type_ext = 0, .match_type_ext = true,
      .min_length = 1, .match_length = true },
  [IDX_ADDRTLV1_LINK_STATUS] = { .type = RFC5444_ADDRTLV_LINK_STATUS, .type_ext = 0, .match_type_ext = true,
      .min_length = 1, .match_length = true },
};

static struct rfc5444_reader_tlvblock_consumer _nhdp_message_pass2_consumer = {
  .order = RFC5444_MAIN_PARSER_PRIORITY + 1,
  .msg_id = RFC5444_MSGTYPE_HELLO,
  .end_callback = _cb_msg_pass2_end,
};

static struct rfc5444_reader_tlvblock_consumer _nhdp_address_pass2_consumer= {
  .order = RFC5444_MAIN_PARSER_PRIORITY + 1,
  .msg_id = RFC5444_MSGTYPE_HELLO,
  .addrblock_consumer = true,
  .block_callback = _cb_addr_pass2_block,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_address_pass2_tlvs[] = {
  [IDX_ADDRTLV2_LOCAL_IF] = { .type = RFC5444_ADDRTLV_LOCAL_IF, .type_ext = 0, .match_type_ext = true,
      .min_length = 1, .match_length = true },
  [IDX_ADDRTLV2_LINK_STATUS] = { .type = RFC5444_ADDRTLV_LINK_STATUS, .type_ext = 0, .match_type_ext = true,
      .min_length = 1, .match_length = true },
  [IDX_ADDRTLV2_OTHER_NEIGHB] = { .type = RFC5444_ADDRTLV_OTHER_NEIGHB, .type_ext = 0, .match_type_ext = true,
      .min_length = 1, .match_length = true },
  [IDX_ADDRTLV2_MPR] = { .type = RFC5444_ADDRTLV_MPR,
      .min_length = 1, .match_length = true },
  [IDX_ADDRTLV2_LINKMETRIC] = { .type = RFC5444_ADDRTLV_LINK_METRIC, .min_length = 2, .match_length = true },
};

/* nhdp multiplexer/protocol */
static struct olsr_rfc5444_protocol *_protocol = NULL;

static enum log_source LOG_NHDP_R = LOG_MAIN;

/* temporary variables for message parsing */
static struct {
  struct nhdp_interface *localif;
  struct nhdp_neighbor *neighbor;

  struct nhdp_link *link;

  struct netaddr originator_v6;

  bool naddr_conflict, laddr_conflict;
  bool link_heard, link_lost;
  bool has_thisif;

  uint64_t vtime, itime;
} _current;

/**
 * Initialize nhdp reader
 */
void
nhdp_reader_init(struct olsr_rfc5444_protocol *p) {
  _protocol = p;

  LOG_NHDP_R = olsr_log_register_source("nhdp_r");

  rfc5444_reader_add_message_consumer(
      &_protocol->reader, &_nhdp_message_pass1_consumer,
      _nhdp_message_tlvs, ARRAYSIZE(_nhdp_message_tlvs));
  rfc5444_reader_add_message_consumer(
      &_protocol->reader, &_nhdp_address_pass1_consumer,
      _nhdp_address_pass1_tlvs, ARRAYSIZE(_nhdp_address_pass1_tlvs));
  rfc5444_reader_add_message_consumer(
      &_protocol->reader, &_nhdp_message_pass2_consumer, NULL, 0);
  rfc5444_reader_add_message_consumer(
      &_protocol->reader, &_nhdp_address_pass2_consumer,
      _nhdp_address_pass2_tlvs, ARRAYSIZE(_nhdp_address_pass2_tlvs));
}

/**
 * Cleanup nhdp reader
 */
void
nhdp_reader_cleanup(void) {
  rfc5444_reader_remove_message_consumer(
      &_protocol->reader, &_nhdp_address_pass2_consumer);
  rfc5444_reader_remove_message_consumer(
      &_protocol->reader, &_nhdp_message_pass2_consumer);
  rfc5444_reader_remove_message_consumer(
      &_protocol->reader, &_nhdp_address_pass1_consumer);
  rfc5444_reader_remove_message_consumer(
      &_protocol->reader, &_nhdp_message_pass1_consumer);
}

/**
 * An error happened during processing and the message was dropped.
 * Make sure that there are no uninitialized datastructures left.
 */
static void
cleanup_error(void) {
  if (_current.link) {
    nhdp_db_link_remove(_current.link);
    _current.link = NULL;
  }

  if (_current.neighbor) {
    nhdp_db_neighbor_remove(_current.neighbor);
    _current.neighbor = NULL;
  }
}

/**
 * Process an address with a LOCAL_IF TLV
 * @param addr pointer to netaddr object with address
 * @param local_if value of LOCAL_IF TLV
 * @return
 */
static enum rfc5444_result
_pass2_process_localif(struct netaddr *addr, uint8_t local_if) {
  struct nhdp_neighbor *neigh;
  struct nhdp_naddr *naddr;
  struct nhdp_link *lnk;
  struct nhdp_laddr *laddr;

  /* make sure link addresses are added to the right link */
  if (local_if == RFC5444_LOCALIF_THIS_IF) {
    laddr = nhdp_interface_get_link_addr(_current.localif, addr);
    if (laddr == NULL) {
      /* create new link address */
      laddr = nhdp_db_link_addr_add(_current.link, addr);
      if (laddr == NULL) {
        return RFC5444_DROP_MESSAGE;
      }
    }
    else {
      /* move to target link if necessary */
      lnk = laddr->link;
      lnk->_process_count--;

      if (lnk != _current.link) {
        nhdp_db_link_addr_move(_current.link, laddr);

        if (lnk->_process_count == 0) {
          /* no address left to process, remove old link */
          nhdp_db_link_remove(lnk);
        }
      }

      /* remove mark from address */
      laddr->_might_be_removed = false;
    }
  }

  /* make sure neighbor addresses are added to the right neighbor */
  naddr = nhdp_db_neighbor_addr_get(addr);
  if (naddr == NULL) {
    /* create new neighbor address */
    naddr = nhdp_db_neighbor_addr_add(_current.neighbor, addr);
    if (naddr == NULL) {
      return RFC5444_DROP_MESSAGE;
    }
  }
  else {
    /* move to target neighbor if necessary */
    neigh = naddr->neigh;
    neigh->_process_count--;

    if (neigh != _current.neighbor) {
      nhdp_db_neighbor_addr_move(_current.neighbor, naddr);

      if (neigh->_process_count == 0) {
        /* no address left to process, remove old neighbor */
        nhdp_db_neighbor_remove(neigh);
      }
    }

    /* remove mark from address */
    naddr->_might_be_removed = false;

    /* mark as not lost */
    nhdp_db_neighbor_addr_not_lost(naddr);
  }


  if (netaddr_get_address_family(&naddr->neigh_addr) == AF_INET) {
    /* update _vtime_v6 timer */
    olsr_timer_set(&_current.neighbor->_vtime_v4, _current.vtime);
  }
  if (netaddr_get_address_family(&naddr->neigh_addr) == AF_INET6) {
    /* update _vtime_v6 timer */
    olsr_timer_set(&_current.neighbor->_vtime_v6, _current.vtime);
  }

  return RFC5444_OKAY;
}

/**
 * Handle in originator address of NHDP Hello
 */
static void
_handle_originator(struct rfc5444_reader_tlvblock_context *context) {
  struct nhdp_neighbor *neigh;
  struct netaddr_str buf;

  OLSR_DEBUG(LOG_NHDP_R, "Handle originator %s",
      netaddr_to_string(&buf, &context->orig_addr));

  neigh = nhdp_db_neighbor_get_by_originator(&context->orig_addr);
  if (!neigh) {
    return;
  }

  if (_current.neighbor == neigh) {
    /* everything is fine, move along */
    return;
  }

  if (_current.neighbor == NULL && !_current.naddr_conflict) {
    /* we take the neighbor selected by the originator */
    _current.neighbor = neigh;
    return;
  }

  if (neigh->_process_count > 0) {
    /* neighbor selected by originator will already be cleaned up */
    return;
  }

  nhdp_db_neighbor_set_originator(neigh, &NETADDR_UNSPEC);
}

/**
 * Handle in HELLO messages and its TLVs
 * @param consumer tlvblock consumer
 * @param context message context
 * @return see rfc5444_result enum
 */
static enum rfc5444_result
_cb_messagetlvs(struct rfc5444_reader_tlvblock_context *context) {
  struct rfc5444_reader_tlvblock_entry *tlv;
  struct nhdp_neighbor *neigh;
  struct nhdp_link *lnk;
  struct nhdp_domain *domain;

  struct netaddr_str buf;

  OLSR_DEBUG(LOG_NHDP_R,
      "Incoming message type %d from %s through %s (addrlen = %u), got message tlvs",
      context->msg_type, netaddr_socket_to_string(&buf, _protocol->input_socket),
      _protocol->input_interface->name, context->addr_len);

  if (!_protocol->input_is_multicast) {
    /* NHDP doesn't care about unicast messages */
    return RFC5444_DROP_MESSAGE;
  }

  if (context->addr_len != 4 && context->addr_len != 16) {
    /* strange address length */
    return RFC5444_DROP_MESSAGE;
  }
  memset(&_current, 0, sizeof(_current));

  /* remember local NHDP interface */
  _current.localif = nhdp_interface_get(_protocol->input_interface->name);

  /* extract originator address */
  if (context->has_origaddr) {
    OLSR_DEBUG(LOG_NHDP_R, "Got originator: %s",
        netaddr_to_string(&buf, &context->orig_addr));
  }

  /* extract validity time and interval time */
  _current.vtime = rfc5444_timetlv_decode(
      _nhdp_message_tlvs[IDX_TLV_VTIME].tlv->single_value[0]);

  if (_nhdp_message_tlvs[IDX_TLV_ITIME].tlv) {
    _current.itime = rfc5444_timetlv_decode(
        _nhdp_message_tlvs[IDX_TLV_ITIME].tlv->single_value[0]);
  }

  /* extract willingness */
  tlv = _nhdp_message_tlvs[IDX_TLV_WILLINGNESS].tlv;
  while (tlv) {
    domain = nhdp_domain_get_by_ext(tlv->type_ext);
    if (domain != NULL && !domain->mpr->no_default_handling) {
      nhdp_domain_process_willingness_tlv(domain, tlv->single_value[0]);
    }
    tlv = tlv->next_entry;
  }

  /* extract v6 originator in dualstack messages */
  if (_nhdp_message_tlvs[IDX_TLV_IPV6ORIG].tlv) {
    if (netaddr_from_binary(&_current.originator_v6,
        _nhdp_message_tlvs[IDX_TLV_IPV6ORIG].tlv->single_value, 16, AF_INET6)) {
      /* error, could not parse address */
      return RFC5444_DROP_MESSAGE;
    }

    OLSR_DEBUG(LOG_NHDP_R, "Got originator: %s",
        netaddr_to_string(&buf, &_current.originator_v6));
  }

  /* clear flags in neighbors */
  list_for_each_element(&nhdp_neigh_list, neigh, _global_node) {
    neigh->_process_count = 0;
  }

  list_for_each_element(&_current.localif->_links, lnk, _if_node) {
    lnk->_process_count = 0;
  }

  return RFC5444_OKAY;
}

/**
 * Process addresses of NHDP Hello message to determine link/neighbor status
 * @param consumer
 * @param context
 * @return
 */
static enum rfc5444_result
_cb_addresstlvs_pass1(struct rfc5444_reader_tlvblock_context *context) {
  uint8_t local_if, link_status;
  struct nhdp_naddr *naddr;
  struct nhdp_laddr *laddr;
  struct netaddr_str buf;

  local_if = 255;
  link_status = 255;

  if (_nhdp_address_pass1_tlvs[IDX_ADDRTLV1_LOCAL_IF].tlv) {
    local_if = _nhdp_address_pass1_tlvs[IDX_ADDRTLV1_LOCAL_IF].tlv->single_value[0];
  }
  if (_nhdp_address_pass1_tlvs[IDX_ADDRTLV1_LINK_STATUS].tlv) {
    link_status = _nhdp_address_pass1_tlvs[IDX_ADDRTLV1_LINK_STATUS].tlv->single_value[0];
  }

  OLSR_DEBUG(LOG_NHDP_R, "Pass 1: address %s, local_if %u, link_status: %u",
      netaddr_to_string(&buf, &context->addr), local_if, link_status);

  if (local_if == RFC5444_LOCALIF_THIS_IF
        || local_if == RFC5444_LOCALIF_OTHER_IF) {
    /* still no neighbor address conflict, so keep checking */
    naddr = nhdp_db_neighbor_addr_get(&context->addr);
    if (naddr != NULL) {
      OLSR_DEBUG(LOG_NHDP_R, "Found neighbor in database");
      naddr->neigh->_process_count++;

      if (!_current.naddr_conflict) {
        if (_current.neighbor == NULL) {
          /* first neighbor, just remember it */
          _current.neighbor = naddr->neigh;
        }
        else if (_current.neighbor != naddr->neigh) {
          /* this is a neighbor address conflict */
          OLSR_DEBUG(LOG_NHDP_R, "Conflict between neighbor addresses detected");
          _current.neighbor = NULL;
          _current.naddr_conflict = true;
        }
      }
    }
  }

  if (local_if == RFC5444_LOCALIF_THIS_IF) {
    /* check for link address conflict */
    laddr = nhdp_interface_get_link_addr(_current.localif, &context->addr);
    if (laddr != NULL) {
      OLSR_DEBUG(LOG_NHDP_R, "Found link in database");
      laddr->link->_process_count++;

      if (!_current.laddr_conflict) {
        if (_current.link == NULL) {
          /* first link, just remember it */
          _current.link = laddr->link;
        }
        else if (_current.link != laddr->link) {
          /* this is a link address conflict */
          OLSR_DEBUG(LOG_NHDP_R, "Conflict between link addresses detected");
          _current.link = NULL;
          _current.laddr_conflict = true;
        }
      }
    }

    /* remember that we had a local_if = THIS_IF address */
    _current.has_thisif = true;
  }

  /* detect if our own node is seen by our neighbor */
  if (link_status != 255
      && nhdp_interface_addr_if_get(_current.localif, &context->addr) != NULL) {
    if (link_status == RFC5444_LINKSTATUS_LOST) {
      OLSR_DEBUG(LOG_NHDP_R, "Link neighbor lost this node address: %s",
          netaddr_to_string(&buf, &context->addr));
      _current.link_lost = true;
    }
    else {
      OLSR_DEBUG(LOG_NHDP_R, "Link neighbor heard this node address: %s",
          netaddr_to_string(&buf, &context->addr));
      _current.link_heard = true;
    }
  }

  /* we do nothing in this pass except for detecting the situation */
  return RFC5444_OKAY;
}

/**
 * Handle end of message for pass1 processing. Create link/neighbor if necessary,
 * mark addresses as potentially lost.
 * @param consumer
 * @param context
 * @param dropped
 * @return
 */
static enum rfc5444_result
_cb_addresstlvs_pass1_end(struct rfc5444_reader_tlvblock_context *context, bool dropped) {
  struct nhdp_naddr *naddr;
  struct nhdp_laddr *laddr;

  if (dropped) {
    cleanup_error();
    return RFC5444_OKAY;
  }

  /* handle originator address */
  if (netaddr_get_address_family(&context->orig_addr) != AF_UNSPEC) {
    _handle_originator(context);
  }

  /* allocate neighbor and link if necessary */
  if (_current.neighbor == NULL) {
    OLSR_DEBUG(LOG_NHDP_R, "Create new neighbor");
    _current.neighbor = nhdp_db_neighbor_add();
    if (_current.neighbor == NULL) {
      return RFC5444_DROP_MESSAGE;
    }
  }
  else {
    /* mark existing neighbor addresses */
    avl_for_each_element(&_current.neighbor->_neigh_addresses, naddr, _neigh_node) {
      if (netaddr_get_binlength(&naddr->neigh_addr) == context->addr_len) {
        naddr->_might_be_removed = true;
      }
    }
  }

  /* allocate link if necessary */
  if (_current.link == NULL) {
    OLSR_DEBUG(LOG_NHDP_R, "Create new link");
    _current.link = nhdp_db_link_add(_current.neighbor, _current.localif);
    if (_current.link == NULL) {
      return RFC5444_DROP_MESSAGE;
    }
  }
  else {
    /* mark existing link addresses */
    avl_for_each_element(&_current.link->_addresses, laddr, _link_node) {
      laddr->_might_be_removed = true;
    }
  }

  if (!_current.has_thisif) {
    struct netaddr addr;

    /* translate like a RFC5444 address */
    if(netaddr_from_binary(&addr, netaddr_get_binptr(_protocol->input_address),
        netaddr_get_binlength(_protocol->input_address), 0)) {
      return RFC5444_DROP_MESSAGE;
    }

    /* parse as if it would be tagged with a LOCAL_IF = THIS_IF TLV */
    _pass2_process_localif(&addr, RFC5444_LOCALIF_THIS_IF);
  }

  /* remember vtime and itime */
  _current.link->vtime_value = _current.vtime;
  _current.link->itime_value = _current.itime;

  /* update hysteresis */
  nhdp_hysteresis_update(_current.link, context);

  /* handle dualstack information */
  if (netaddr_get_address_family(&_current.originator_v6) != AF_UNSPEC) {
    struct nhdp_neighbor *neigh2;
    struct nhdp_link *lnk2;

    neigh2 = nhdp_db_neighbor_get_by_originator(&_current.originator_v6);
    if (neigh2) {
      nhdp_db_neighbor_connect_dualstack(_current.neighbor, neigh2);
    }

    lnk2 = nhdp_interface_link_get_by_originator(_current.localif, &_current.originator_v6);
    if (lnk2) {
      nhdp_db_link_connect_dualstack(_current.link, lnk2);
    }
  }
  else if (netaddr_get_address_family(&context->orig_addr) == AF_INET
      && netaddr_get_address_family(&_current.originator_v6) == AF_UNSPEC) {
    nhdp_db_neigbor_disconnect_dualstack(_current.neighbor);
    nhdp_db_link_disconnect_dualstack(_current.link);
  }

  OLSR_DEBUG(LOG_NHDP_R, "pass1 finished");

  return RFC5444_OKAY;
}

/**
 * Process MPR, Willingness and Linkmetric TLVs for local neighbor
 * @param addr address the TLVs are attached to
 */
static void
_process_domainspecific_linkdata(struct netaddr *addr) {
  struct rfc5444_reader_tlvblock_entry *tlv;
  struct nhdp_domain *domain;
  struct nhdp_neighbor_domaindata *neighdata;
  uint16_t tlvvalue;

  struct netaddr_str buf;

  /*
   * clear routing mpr, willingness and metric values
   * that should be present in HELLO
   */
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    neighdata = nhdp_domain_get_neighbordata(domain, _current.neighbor);

    if (!domain->mpr->no_default_handling) {
      neighdata->local_is_mpr = false;
      neighdata->willingness = 0;
    }

    if (!domain->metric->no_default_handling) {
      nhdp_domain_get_linkdata(domain, _current.link)->metric.out =
          RFC5444_METRIC_INFINITE;
      neighdata->metric.out = RFC5444_METRIC_INFINITE;
    }
  }

  /* update MPR selector if this is "our" address on the local interface */
  tlv = _nhdp_address_pass2_tlvs[IDX_ADDRTLV2_MPR].tlv;
  while (tlv) {
    OLSR_DEBUG(LOG_NHDP_R, "Pass 2: address %s, MPR (ext %u): %d",
        netaddr_to_string(&buf, addr), tlv->type_ext, tlv->single_value[0]);

    /* get MPR handler */
    domain = nhdp_domain_get_by_ext(tlv->type_ext);
    if (domain != NULL && !domain->mpr->no_default_handling) {
      nhdp_domain_process_mpr_tlv(domain, _current.link, tlv->single_value[0]);
    }
    tlv = tlv->next_entry;
  }

  /* update out metric with other sides in metric */
  tlv = _nhdp_address_pass2_tlvs[IDX_ADDRTLV2_LINKMETRIC].tlv;
  while (tlv) {
    /* extract tlv value */
    memcpy(&tlvvalue, tlv->single_value, sizeof(tlvvalue));
    tlvvalue = ntohs(tlvvalue);

    OLSR_DEBUG(LOG_NHDP_R, "Pass 2: address %s, LQ (ext %u): %04x",
        netaddr_to_string(&buf, addr), tlv->type_ext, tlvvalue);

    /* get metric handler */
    domain = nhdp_domain_get_by_ext(tlv->type_ext);
    if (domain != NULL && !domain->metric->no_default_handling) {
      nhdp_domain_process_metric_linktlv(domain, _current.link, tlvvalue);
    }

    tlv = tlv->next_entry;
  }

  /* process willingness TLVs stored from message TLV processing */
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    if (!domain->mpr->no_default_handling) {
      nhdp_domain_process_willingness_tlv(domain, domain->mpr->_tmp_willingness);
    }
  }
}

/**
 * Process Linkmetric TLVs for twohop neighbor
 * @param l2hop pointer to twohop neighbor
 * @param addr address the TLVs are attached to
 */
static void
_process_domainspecific_2hopdata(struct nhdp_l2hop *l2hop, struct netaddr *addr) {
  struct rfc5444_reader_tlvblock_entry *tlv;
  struct nhdp_domain *domain;
  struct nhdp_l2hop_domaindata *data;
  uint16_t tlvvalue;

  struct netaddr_str buf;

  /* clear metric values that should be present in HELLO */
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    if (!domain->metric->no_default_handling) {
      data = nhdp_domain_get_l2hopdata(domain, l2hop);
      data->metric.in = RFC5444_METRIC_INFINITE;
      data->metric.out = RFC5444_METRIC_INFINITE;
    }
  }

  /* update 2-hop metric (no direction reversal!) */
  tlv = _nhdp_address_pass2_tlvs[IDX_ADDRTLV2_LINKMETRIC].tlv;
  while (tlv) {
    /* extract tlv value */
    memcpy(&tlvvalue, tlv->single_value, sizeof(tlvvalue));
    tlvvalue = ntohs(tlvvalue);

    OLSR_DEBUG(LOG_NHDP_R, "Pass 2: address %s, LQ (ext %u): %04x",
        netaddr_to_string(&buf, addr), tlv->type_ext, tlvvalue);

    /* get metric handler */
    domain = nhdp_domain_get_by_ext(tlv->type_ext);
    if (domain != NULL && !domain->metric->no_default_handling) {
      nhdp_domain_process_metric_2hoptlv(domain, l2hop, tlvvalue);
    }

    tlv = tlv->next_entry;
  }
}

/**
 * Second pass for processing the addresses of the NHDP Hello. This one will update
 * the database
 * @param consumer
 * @param context
 * @return
 */
static enum rfc5444_result
_cb_addr_pass2_block(struct rfc5444_reader_tlvblock_context *context) {
  uint8_t local_if, link_status, other_neigh;
  struct nhdp_l2hop *l2hop;
  struct netaddr_str buf;

  local_if = 255;
  link_status = 255;
  other_neigh = 255;

  /* read values of TLVs that can only be present once */
  if (_nhdp_address_pass2_tlvs[IDX_ADDRTLV2_LOCAL_IF].tlv) {
    local_if = _nhdp_address_pass2_tlvs[IDX_ADDRTLV2_LOCAL_IF].tlv->single_value[0];
  }
  if (_nhdp_address_pass2_tlvs[IDX_ADDRTLV2_LINK_STATUS].tlv) {
    link_status = _nhdp_address_pass2_tlvs[IDX_ADDRTLV2_LINK_STATUS].tlv->single_value[0];
  }
  if (_nhdp_address_pass2_tlvs[IDX_ADDRTLV2_OTHER_NEIGHB].tlv) {
    other_neigh = _nhdp_address_pass2_tlvs[IDX_ADDRTLV2_OTHER_NEIGHB].tlv->single_value[0];
  }
  OLSR_DEBUG(LOG_NHDP_R, "Pass 2: address %s, local_if %u, link_status: %u, other_neigh: %u",
      netaddr_to_string(&buf, &context->addr), local_if, link_status, other_neigh);

  if (local_if == RFC5444_LOCALIF_THIS_IF || local_if == RFC5444_LOCALIF_OTHER_IF) {
    /* parse LOCAL_IF TLV */
    _pass2_process_localif(&context->addr, local_if);
  }

  /* handle 2hop-addresses */
  if (link_status != 255 || other_neigh != 255) {
    if (nhdp_interface_addr_if_get(_current.localif, &context->addr) != NULL) {
      _process_domainspecific_linkdata(&context->addr);
    }
    else if (nhdp_interface_addr_global_get(&context->addr) != NULL) {
      OLSR_DEBUG(LOG_NHDP_R, "Link neighbor heard this node address: %s",
          netaddr_to_string(&buf, &context->addr));
    }
    else if (link_status == RFC5444_LINKSTATUS_SYMMETRIC
        || other_neigh == RFC5444_OTHERNEIGHB_SYMMETRIC){
      l2hop  = ndhp_db_link_2hop_get(_current.link, &context->addr);
      if (l2hop == NULL) {
        /* create new 2hop address */
        l2hop = nhdp_db_link_2hop_add(_current.link, &context->addr);
        if (l2hop == NULL) {
          return RFC5444_DROP_MESSAGE;
        }
      }

      /* refresh validity time of 2hop address */
      nhdp_db_link_2hop_set_vtime(l2hop, _current.vtime);

      _process_domainspecific_2hopdata(l2hop, &context->addr);
    }
    else {
      l2hop = ndhp_db_link_2hop_get(_current.link, &context->addr);
      if (l2hop) {
        /* remove 2hop address */
        nhdp_db_link_2hop_remove(l2hop);
      }
    }
  }

  return RFC5444_OKAY;
}

/**
 * Finalize changes of the database and update the status of the link
 * @param consumer
 * @param context
 * @param dropped
 * @return
 */
static enum rfc5444_result
_cb_msg_pass2_end(struct rfc5444_reader_tlvblock_context *context, bool dropped) {
  struct nhdp_naddr *naddr;
  struct nhdp_laddr *laddr, *la_it;
  struct nhdp_l2hop *twohop, *twohop_it;
  struct nhdp_domain *domain;
  uint64_t t;

  if (dropped) {
    cleanup_error();
    return RFC5444_OKAY;
  }

  /* remember when we saw the last IPv4/IPv6 */
  if (context->addr_len == 4) {
    olsr_timer_set(&_current.neighbor->_vtime_v4, _current.vtime);
  }
  else {
    olsr_timer_set(&_current.neighbor->_vtime_v6, _current.vtime);
  }

  /* remove leftover link addresses */
  avl_for_each_element_safe(&_current.link->_addresses, laddr, _link_node, la_it) {
    if (laddr->_might_be_removed) {
      nhdp_db_link_addr_remove(laddr);
    }
  }

  /* remove leftover neighbor addresses */
  avl_for_each_element(&_current.neighbor->_neigh_addresses, naddr, _neigh_node) {
    if (naddr->_might_be_removed) {
      /* mark as lost */
      nhdp_db_neighbor_addr_set_lost(naddr, _current.localif->n_hold_time);

      /* section 12.6.1: remove all similar n2 addresses */
      // TODO: not nice, replace with new iteration macro
      twohop_it = avl_find_element(&_current.link->_2hop, &naddr->neigh_addr, twohop_it, _link_node);
      while (twohop_it) {
        twohop = twohop_it;
        twohop_it = avl_next_element_safe(&_current.link->_2hop, twohop_it, _link_node);
        if (twohop_it != NULL && !twohop_it->_link_node.follower) {
          twohop_it = NULL;
        }

        nhdp_db_link_2hop_remove(twohop);
      }
    }
  }

  /* Section 12.5.4: update link */
  if (_current.link_heard) {
    /* Section 12.5.4.1.1: we have been heard, so the link is symmetric */
    olsr_timer_set(&_current.link->sym_time, _current.vtime);
  }
  else if (_current.link_lost) {
    /* Section 12.5.4.1.2 */
    if (olsr_timer_is_active(&_current.link->sym_time)) {
      olsr_timer_stop(&_current.link->sym_time);

      /*
       * the stop timer might have modified to link status, but do not trigger
       * cleanup until this processing is over
       */
      if (_nhdp_db_link_calculate_status(_current.link)== RFC5444_LINKSTATUS_HEARD) {
        nhdp_db_link_set_vtime(_current.link, _current.localif->l_hold_time);
      }
    }
  }

  /* Section 12.5.4.3 */
  t = olsr_timer_get_due(&_current.link->sym_time);
  if (!olsr_timer_is_active(&_current.link->sym_time) || t < _current.vtime) {
    t = _current.vtime;
  }
  olsr_timer_set(&_current.link->heard_time, t);

  /* Section 12.5.4.4: link status pending is not influenced by the code above */
  if (_current.link->status != NHDP_LINK_PENDING) {
    t += _current.localif->l_hold_time;
  }

  /* Section 12.5.4.5 */
  if (!olsr_timer_is_active(&_current.link->vtime)
      || (int64_t)t > olsr_timer_get_due(&_current.link->vtime)) {
    olsr_timer_set(&_current.link->vtime, t);
  }

  /* overwrite originator of neighbor entry */
  nhdp_db_neighbor_set_originator(_current.neighbor, &context->orig_addr);

  /* update MPR sets */
  nhdp_domain_update_mprs();

  /* update link metrics */
  list_for_each_element(&nhdp_domain_list, domain, _node) {
    nhdp_domain_calculate_neighbor_metric(domain, _current.neighbor);
  }

  /* update ip flooding settings */
  nhdp_interface_update_status(_current.localif);

  /* update link status */
  nhdp_db_link_update_status(_current.link);

  return RFC5444_OKAY;
}
