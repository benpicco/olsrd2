
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
#include "rfc5444/rfc5444_conversion.h"
#include "rfc5444/rfc5444_reader.h"
#include "core/olsr_logging.h"
#include "core/olsr_subsystem.h"
#include "tools/olsr_rfc5444.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_hysteresis.h"
#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_linkmetric.h"
#include "nhdp/nhdp_mpr.h"
#include "nhdp/nhdp_reader.h"

/* NHDP message TLV array index */
enum {
  IDX_TLV_ITIME,
  IDX_TLV_VTIME,
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
static int _parse_address(struct netaddr *dst, const void *ptr, uint8_t len);
static enum rfc5444_result _pass2_process_localif(struct netaddr *addr, uint8_t local_if);

static enum rfc5444_result
_cb_messagetlvs(struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);

static enum rfc5444_result
_cb_addresstlvs_pass1(struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result _cb_addresstlvs_pass1_end(struct rfc5444_reader_tlvblock_consumer *,
    struct rfc5444_reader_tlvblock_context *context, bool dropped);

static enum rfc5444_result _cb_addr_pass2_block(
    struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result _cb_msg_pass2_end(
    struct rfc5444_reader_tlvblock_consumer *,
    struct rfc5444_reader_tlvblock_context *context, bool dropped);

/* definition of the RFC5444 reader components */
static struct rfc5444_reader_tlvblock_consumer _nhdp_message_pass1_consumer = {
  .order = RFC5444_MAIN_PARSER_PRIORITY,
  .msg_id = RFC5444_MSGTYPE_HELLO,
  .block_callback = _cb_messagetlvs,
  .end_callback = _cb_addresstlvs_pass1_end,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_message_tlvs[] = {
  [IDX_TLV_ITIME] = { .type = RFC5444_MSGTLV_INTERVAL_TIME, .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_VTIME] = { .type = RFC5444_MSGTLV_VALIDITY_TIME, .mandatory = true, .min_length = 1, .match_length = true },
};

static struct rfc5444_reader_tlvblock_consumer _nhdp_address_pass1_consumer = {
  .order = RFC5444_MAIN_PARSER_PRIORITY,
  .msg_id = RFC5444_MSGTYPE_HELLO,
  .addrblock_consumer = true,
  .block_callback = _cb_addresstlvs_pass1,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_address_pass1_tlvs[] = {
  [IDX_ADDRTLV1_LOCAL_IF] = { .type = RFC5444_ADDRTLV_LOCAL_IF, .min_length = 1, .match_length = true },
  [IDX_ADDRTLV1_LINK_STATUS] = { .type = RFC5444_ADDRTLV_LINK_STATUS, .min_length = 1, .match_length = true },
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
  [IDX_ADDRTLV2_LOCAL_IF] = { .type = RFC5444_ADDRTLV_LOCAL_IF, .min_length = 1, .match_length = true },
  [IDX_ADDRTLV2_LINK_STATUS] = { .type = RFC5444_ADDRTLV_LINK_STATUS, .min_length = 1, .match_length = true },
  [IDX_ADDRTLV2_OTHER_NEIGHB] = { .type = RFC5444_ADDRTLV_OTHER_NEIGHB, .min_length = 1, .match_length = true },
  [IDX_ADDRTLV2_MPR] = { .type = RFC5444_ADDRTLV_MPR, .min_length = 1, .match_length = true },
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

  bool naddr_conflict, laddr_conflict;
  bool link_heard, link_lost;
  bool has_thisif;

  bool has_ipv4, has_ipv6;

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

  olsr_rfc5444_remove_protocol(_protocol);
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
 * Parse an incoming address, do a IPv6 to IPv4 translation if necessary and
 * check if the address should be processed.
 * @param dst pointer to destination netaddr object
 * @param ptr pointer to source of address
 * @param len length of address in bytes
 * @return -1 if an error happened, 0 if address should be processed,
 *   1 if address should not be processed.
 */
static int
_parse_address(struct netaddr *dst, const void *ptr, uint8_t len) {
  if (len == 16
      && netaddr_binary_is_in_subnet(&NETADDR_IPV6_IPV4COMPATIBLE, ptr, len, AF_INET6)) {
    struct netaddr addr;

    if (_current.localif->mode == NHDP_IPV6) {
      /* ignore embedded v4 when not in dualstack mode */
      return 1;
    }

    if (netaddr_from_binary(&addr, ptr, len, AF_INET6)) {
      OLSR_WARN(LOG_NHDP_R, "Could not read incoming ipv6 address");
      return -1;
    }

    netaddr_extract_ipv4_compatible(dst, &addr);
    return 0;
  }

  /* convert binary address to netaddr object */
  if (netaddr_from_binary(dst, ptr, len, 0)) {
    OLSR_WARN(LOG_NHDP_R, "Could not read incoming address of length %u", len);
    return -1;
  }

  /* ignore wrong IP type if restricted to the other one */
  if (_current.localif->mode == NHDP_IPV4 && netaddr_get_address_family(dst) == AF_INET6) {
    return 1;
  }
  if (_current.localif->mode == NHDP_IPV6 && netaddr_get_address_family(dst) == AF_INET) {
    return 1;
  }

  return 0;
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
    /* update vtime_v6 timer */
    olsr_timer_set(&_current.neighbor->vtime_v4, _current.vtime);
  }
  if (netaddr_get_address_family(&naddr->neigh_addr) == AF_INET6) {
    /* update vtime_v6 timer */
    olsr_timer_set(&_current.neighbor->vtime_v6, _current.vtime);
  }

  return RFC5444_OKAY;
}

/**
 * Handle incoming HELLO messages and its TLVs
 * @param consumer tlvblock consumer
 * @param context message context
 * @return see rfc5444_result enum
 */
static enum rfc5444_result
_cb_messagetlvs(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  struct nhdp_neighbor *neigh;
  struct nhdp_link *lnk;

  struct netaddr_str buf;

  OLSR_DEBUG(LOG_NHDP_R, "Incoming message type %d from %s through %s, got message tlvs",
      context->msg_type, netaddr_socket_to_string(&buf, _protocol->input_socket),
      _protocol->input_interface->name);

  if (!_protocol->input_is_multicast) {
    /* NHDP doesn't care about unicast messages */
    return RFC5444_DROP_MESSAGE;
  }

  memset(&_current, 0, sizeof(_current));

  /* remember local NHDP interface */
  _current.localif = nhdp_interface_get(_protocol->input_interface->name);

  if ((context->addr_len == 4 && _current.localif->mode == NHDP_IPV6)
      || (context->addr_len == 16 && _current.localif->mode == NHDP_IPV4)) {
    return RFC5444_DROP_MESSAGE;
  }

  _current.vtime = rfc5444_timetlv_decode(
      _nhdp_message_tlvs[IDX_TLV_VTIME].tlv->single_value[0]);

  if (_nhdp_message_tlvs[IDX_TLV_ITIME].tlv) {
    _current.itime = rfc5444_timetlv_decode(
        _nhdp_message_tlvs[IDX_TLV_ITIME].tlv->single_value[0]);
  }

  /* clear flags in neighbors */
  list_for_each_element(&nhdp_neigh_list, neigh, _node) {
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
_cb_addresstlvs_pass1(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  uint8_t local_if, link_status;
  struct nhdp_naddr *naddr;
  struct nhdp_laddr *laddr;
  struct netaddr addr;
  struct netaddr_str buf;

  local_if = 255;
  link_status = 255;

  switch(_parse_address(&addr, context->addr, context->addr_len)) {
    case -1:
      /* error, could not parse address */
      return RFC5444_DROP_ADDRESS;

    case 1:
      /* silent ignore */
      return RFC5444_OKAY;

    default:
      break;
  }

  _current.has_ipv4 |= netaddr_get_address_family(&addr) == AF_INET;
  _current.has_ipv6 |= netaddr_get_address_family(&addr) == AF_INET6;

  if (_nhdp_address_pass1_tlvs[IDX_ADDRTLV1_LOCAL_IF].tlv) {
    local_if = _nhdp_address_pass1_tlvs[IDX_ADDRTLV1_LOCAL_IF].tlv->single_value[0];
  }
  if (_nhdp_address_pass1_tlvs[IDX_ADDRTLV1_LINK_STATUS].tlv) {
    link_status = _nhdp_address_pass1_tlvs[IDX_ADDRTLV1_LINK_STATUS].tlv->single_value[0];
  }

  OLSR_DEBUG(LOG_NHDP_R, "Pass 1: address %s, local_if %u, link_status: %u",
      netaddr_to_string(&buf, &addr), local_if, link_status);

  if (local_if == RFC5444_LOCALIF_THIS_IF
        || local_if == RFC5444_LOCALIF_OTHER_IF) {
    /* still no neighbor address conflict, so keep checking */
    naddr = nhdp_db_neighbor_addr_get(&addr);
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
    laddr = nhdp_interface_get_link_addr(_current.localif, &addr);
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
  if (link_status != 255 && nhdp_interface_addr_if_get(_current.localif, &addr) != NULL) {
    if (link_status == RFC5444_LINKSTATUS_LOST) {
      OLSR_DEBUG(LOG_NHDP_R, "Link neighbor lost this node address: %s",
          netaddr_to_string(&buf, &addr));
      _current.link_lost = true;
    }
    else {
      OLSR_DEBUG(LOG_NHDP_R, "Link neighbor heard this node address: %s",
          netaddr_to_string(&buf, &addr));
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
_cb_addresstlvs_pass1_end(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
    struct rfc5444_reader_tlvblock_context *context __attribute__((unused)), bool dropped) {
  struct nhdp_naddr *naddr;
  struct nhdp_laddr *laddr;

  if (dropped) {
    cleanup_error();
    return RFC5444_OKAY;
  }

  /* allocate neighbor and link if necessary */
  if (_current.neighbor == NULL) {
    OLSR_DEBUG(LOG_NHDP_R, "Create new neighbor");
    _current.neighbor = nhdp_db_neighbor_add();
    if (_current.neighbor == NULL) {
      return RFC5444_DROP_MESSAGE;
    }
  }
  if (_current.link == NULL) {
    OLSR_DEBUG(LOG_NHDP_R, "Create new link");
    _current.link = nhdp_db_link_add(_current.neighbor, _current.localif);
    if (_current.link == NULL) {
      return RFC5444_DROP_MESSAGE;
    }
  }

  /* mark existing neighbor addresses */
  avl_for_each_element(&_current.neighbor->_neigh_addresses, naddr, _neigh_node) {
    if ((netaddr_get_address_family(&naddr->neigh_addr) == AF_INET && _current.has_ipv4)
      || (netaddr_get_address_family(&naddr->neigh_addr) == AF_INET6 && _current.has_ipv6)) {
      naddr->_might_be_removed = true;
    }
  }

  /* mark existing link addresses */
  avl_for_each_element(&_current.link->_addresses, laddr, _link_node) {
    laddr->_might_be_removed = true;
  }

  if (!_current.has_thisif) {
    struct netaddr addr;

    /* translate like a RFC5444 address */
    if(_parse_address(&addr, netaddr_get_binptr(_protocol->input_address),
        netaddr_get_binlength(_protocol->input_address)) != 0) {
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

  OLSR_DEBUG(LOG_NHDP_R, "pass1 finished");

  return RFC5444_OKAY;
}

// TODO: move to block callback
#if 0
/**
 * Parse Linkmetric TLVs and store them in the handlers buffer
 * @param c
 * @param entry
 * @param context
 * @return
 */
static enum rfc5444_result
_cb_addr_pass2_tlvs(struct rfc5444_reader_tlvblock_consumer *c __attribute((unused)),
    struct rfc5444_reader_tlvblock_entry *entry,
    struct rfc5444_reader_tlvblock_context *context __attribute((unused))) {
  struct nhdp_linkmetric_handler *h;

  uint16_t tlvvalue;
  uint32_t metric;
  if (entry->type != RFC5444_ADDRTLV_LINK_METRIC) {
    /* ignore all TLVs except link metric TLVs */
    return RFC5444_OKAY;
  }

  if (entry->length != 2) {
    /* ignore everything except length 2 */
    return RFC5444_OKAY;
  }

  /* read value and convert it to metric */
  memcpy(&tlvvalue, entry->single_value, sizeof(tlvvalue));
  tlvvalue = ntohs(tlvvalue);

  metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_COST_MASK);

  /* store the TLV(s) for later processing */
  h = nhdp_linkmetric_handler_get_by_ext(entry->type_ext);

  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_LINK) {
    h->_metric[NHDP_TEMP_INCOMING_LINK] = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_OUTGOING_LINK) {
    h->_metric[NHDP_TEMP_INCOMING_NEIGH] = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
    h->_metric[NHDP_TEMP_OUTGOING_LINK] = metric;
  }
  if (tlvvalue & RFC5444_LINKMETRIC_OUTGOING_NEIGH) {
    h->_metric[NHDP_TEMP_OUTGOING_NEIGH] = metric;
  }
  return RFC5444_OKAY;
}
#endif

/**
 * Second pass for processing the addresses of the NHDP Hello. This one will update
 * the database
 * @param consumer
 * @param context
 * @return
 */
static enum rfc5444_result
_cb_addr_pass2_block(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  uint8_t local_if, link_status, other_neigh, mprs;
  struct rfc5444_reader_tlvblock_entry *tlv;
  struct nhdp_linkmetric_handler *h;
  struct nhdp_l2hop *l2hop;
  struct netaddr addr;
  struct netaddr_str buf;
  uint16_t tlvvalue;
  uint32_t metric;

  switch(_parse_address(&addr, context->addr, context->addr_len)) {
    case -1:
      /* error, could not parse address */
      return RFC5444_DROP_ADDRESS;

    case 1:
      /* silent ignore */
      return RFC5444_OKAY;

    default:
      break;
  }

  local_if = 255;
  link_status = 255;
  other_neigh = 255;
  mprs = 255;

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
  if (_nhdp_address_pass2_tlvs[IDX_ADDRTLV2_MPR].tlv) {
    mprs = _nhdp_address_pass2_tlvs[IDX_ADDRTLV2_MPR].tlv->single_value[0];
  }

  OLSR_DEBUG(LOG_NHDP_R, "Pass 2: address %s, local_if %u, link_status: %u, other_neigh: %u, mprs: %u",
      netaddr_to_string(&buf, &addr), local_if, link_status, other_neigh, mprs);

  if (local_if == RFC5444_LOCALIF_THIS_IF || local_if == RFC5444_LOCALIF_OTHER_IF) {
    /* parse LOCAL_IF TLV */
    _pass2_process_localif(&addr, local_if);
  }

  /* handle 2hop-addresses */
  if (link_status != 255 || other_neigh != 255) {
    if (nhdp_interface_addr_if_get(_current.localif, &addr) != NULL) {
      /* update MPR selector if this is "our" address on the local interface */

      // TODO: what is with MPRs and multitopology routing?
      nhdp_mpr_set_mprs(nhdp_mpr_get_flooding_handler(), _current.link,
          mprs == RFC5444_MPR_FLOODING || mprs == RFC5444_MPR_FLOOD_ROUTE);
      nhdp_mpr_set_mprs(nhdp_mpr_get_routing_handler(), _current.link,
          mprs == RFC5444_MPR_ROUTING || mprs == RFC5444_MPR_FLOOD_ROUTE);

      /* clear metric values that should be present in HELLO */
      list_for_each_element(&nhdp_metric_handler_list, h, _node) {
        _current.link->_metric[h->_index].outgoing = h->metric_default.outgoing;
        _current.neighbor->_metric[h->_index].outgoing = h->metric_default.outgoing;
      }

      /* update outgoing metric with other sides incoming metric */
      tlv = _nhdp_address_pass2_tlvs[IDX_ADDRTLV2_LINKMETRIC].tlv;
      while (tlv) {
        /* extract tlv value */
        memcpy(&tlvvalue, tlv->single_value, sizeof(tlvvalue));
        tlvvalue = ntohs(tlvvalue);

        /* get metric handler */
        h = nhdp_linkmetric_handler_get_by_ext(tlv->type_ext);

        /* decode metric part */
        metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_COST_MASK);

        if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_LINK) {
          _current.link->_metric[h->_index].outgoing = metric;
        }
        if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
          _current.neighbor->_metric[h->_index].outgoing = metric;
        }
        tlv = tlv->next_entry;
      }
    }
    else if (nhdp_interface_addr_global_get(&addr) != NULL) {
      OLSR_DEBUG(LOG_NHDP_R, "Link neighbor heard this node address: %s",
          netaddr_to_string(&buf, &addr));
    }
    else if (link_status == RFC5444_LINKSTATUS_SYMMETRIC
        || other_neigh == RFC5444_OTHERNEIGHB_SYMMETRIC){
      l2hop  = ndhp_db_link_2hop_get(_current.link, &addr);
      if (l2hop == NULL) {
        /* create new 2hop address */
        l2hop = nhdp_db_link_2hop_add(_current.link, &addr);
        if (l2hop == NULL) {
          return RFC5444_DROP_MESSAGE;
        }
      }

      /* refresh validity time of 2hop address */
      nhdp_db_link_2hop_set_vtime(l2hop, _current.vtime);

      /* clear metric values that should be present in HELLO */
      list_for_each_element(&nhdp_metric_handler_list, h, _node) {
        l2hop->_metric[h->_index].incoming = h->metric_default.incoming;
        l2hop->_metric[h->_index].outgoing = h->metric_default.outgoing;
      }

      /* update 2-hop metric (no direction reversal!) */
      tlv = _nhdp_address_pass2_tlvs[IDX_ADDRTLV2_LINKMETRIC].tlv;
      while (tlv) {
        /* extract tlv value */
        memcpy(&tlvvalue, tlv->single_value, sizeof(tlvvalue));
        tlvvalue = ntohs(tlvvalue);

        /* get metric handler */
        h = nhdp_linkmetric_handler_get_by_ext(tlv->type_ext);

        /* decode metric part */
        metric = rfc5444_metric_decode(tlvvalue & RFC5444_LINKMETRIC_COST_MASK);

        if (tlvvalue & RFC5444_LINKMETRIC_INCOMING_NEIGH) {
          l2hop->_metric[h->_index].incoming= metric;
        }
        if (tlvvalue & RFC5444_LINKMETRIC_OUTGOING_NEIGH) {
          l2hop->_metric[h->_index].outgoing= metric;
        }
        tlv = tlv->next_entry;
      }
    }
    else {
      l2hop = ndhp_db_link_2hop_get(_current.link, &addr);
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
_cb_msg_pass2_end(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
    struct rfc5444_reader_tlvblock_context *context __attribute__((unused)), bool dropped) {
  struct nhdp_naddr *naddr;
  struct nhdp_laddr *laddr, *la_it;
  struct nhdp_l2hop *twohop, *twohop_it;
  struct nhdp_linkmetric_handler *h;
  uint64_t t;

  if (dropped) {
    cleanup_error();
    return RFC5444_OKAY;
  }

  /* remember when we saw the last IPv4/IPv6 */
  if (_current.has_ipv4) {
    olsr_timer_set(&_current.neighbor->vtime_v4, _current.vtime);
  }
  if (_current.has_ipv6) {
    olsr_timer_set(&_current.neighbor->vtime_v6, _current.vtime);
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

  /* update v4/v6-only status of interface */
  nhdp_interfaces_update_neigh_addresstype(_current.localif);

  /* update MPR set */
  nhdp_mpr_update(nhdp_mpr_get_flooding_handler(), _current.localif);
  nhdp_mpr_update(nhdp_mpr_get_routing_handler(), _current.localif);

  /* update link metrics */
  list_for_each_element(&nhdp_metric_handler_list, h, _node) {
    nhdp_linkmetric_calculate_neighbor_metric(h, _current.neighbor);
  }

  /* update link status */
  nhdp_db_link_update_status(_current.link);

  return RFC5444_OKAY;
}
