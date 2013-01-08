
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2012, the olsr.org team - see HISTORY file
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
#include "rfc5444/rfc5444_conversion.h"
#include "rfc5444/rfc5444_reader.h"
#include "core/olsr_logging.h"
#include "core/olsr_subsystem.h"
#include "tools/olsr_rfc5444.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_interfaces.h"
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
  IDX_ADDRTLV2_LINK_STATUS,
  IDX_ADDRTLV2_OTHER_NEIGHB,
};

/* prototypes */
static struct nhdp_addr *_process_localif(struct netaddr *addr);

static enum rfc5444_result _cb_message_start_callback(struct rfc5444_reader_tlvblock_consumer *,
    struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result _cb_localif_end_callback(struct rfc5444_reader_tlvblock_consumer *,
    struct rfc5444_reader_tlvblock_context *context, bool dropped);
static enum rfc5444_result
_cb_messagetlvs(struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result
_cb_localif_addresstlvs(struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result
_cb_neigh2_addresstlvs(struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);

/* definition of the RFC5444 reader components */
static struct rfc5444_reader_tlvblock_consumer _nhdp_message_consumer = {
  .start_callback = _cb_message_start_callback,
  .end_callback = _cb_localif_end_callback,
  .block_callback = _cb_messagetlvs,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_message_tlvs[] = {
  [IDX_TLV_ITIME] = { .type = RFC5444_MSGTLV_INTERVAL_TIME, .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_VTIME] = { .type = RFC5444_MSGTLV_VALIDITY_TIME, .mandatory = true, .min_length = 1, .match_length = true },
};

static struct rfc5444_reader_tlvblock_consumer _nhdp_localif_address_consumer = {
  .block_callback = _cb_localif_addresstlvs,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_localif_address_tlvs[] = {
  [IDX_ADDRTLV1_LOCAL_IF] = { .type = RFC5444_ADDRTLV_LOCAL_IF, .min_length = 1, .match_length = true },
  [IDX_ADDRTLV1_LINK_STATUS] = { .type = RFC5444_ADDRTLV_LINK_STATUS, .min_length = 1, .match_length = true },
};

static struct rfc5444_reader_tlvblock_consumer _nhdp_neigh_address_consumer = {
  .block_callback = _cb_neigh2_addresstlvs,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_neigh_address_tlvs[] = {
  [IDX_ADDRTLV2_LINK_STATUS] = { .type = RFC5444_ADDRTLV_LINK_STATUS, .min_length = 1, .match_length = true },
  [IDX_ADDRTLV2_OTHER_NEIGHB] = { .type = RFC5444_ADDRTLV_OTHER_NEIGHB, .min_length = 1, .match_length = true },
};

/* nhdp multiplexer/protocol */
static struct olsr_rfc5444_protocol *_protocol = NULL;

static enum log_source LOG_NHDP_R = LOG_MAIN;

/* temporary variables for message parsing */
static struct {
  struct nhdp_interface *localif;
  struct nhdp_neighbor *neighbor;

  struct nhdp_link *link;
  bool multiple_links;

  uint64_t vtime;
  bool link_heard, link_lost;
} _current;

/**
 * Initialize nhdp reader
 */
void
nhdp_reader_init(struct olsr_rfc5444_protocol *p) {
  _protocol = p;

  LOG_NHDP_R = olsr_log_register_source("nhdp_r");

  rfc5444_reader_add_message_consumer(
      &_protocol->reader, &_nhdp_message_consumer,
      _nhdp_message_tlvs, ARRAYSIZE(_nhdp_message_tlvs), RFC5444_MSGTYPE_HELLO, 0);
  rfc5444_reader_add_address_consumer(
      &_protocol->reader, &_nhdp_localif_address_consumer,
      _nhdp_localif_address_tlvs, ARRAYSIZE(_nhdp_localif_address_tlvs), RFC5444_MSGTYPE_HELLO, 0);
  rfc5444_reader_add_address_consumer(
      &_protocol->reader, &_nhdp_neigh_address_consumer,
      _nhdp_neigh_address_tlvs, ARRAYSIZE(_nhdp_neigh_address_tlvs), RFC5444_MSGTYPE_HELLO, 1);
}

/**
 * Cleanup nhdp reader
 */
void
nhdp_reader_cleanup(void) {
  rfc5444_reader_remove_address_consumer(
      &_protocol->reader, &_nhdp_neigh_address_consumer);
  rfc5444_reader_remove_address_consumer(
      &_protocol->reader, &_nhdp_localif_address_consumer);
  rfc5444_reader_remove_message_consumer(
      &_protocol->reader, &_nhdp_message_consumer);

  olsr_rfc5444_remove_protocol(_protocol);
}

/**
 * Helper functions that prepares a nhdp neighbor for HELLO processing
 * @param neigh nhdp neighbor
 */
static void
_initialize_neighbor_for_processing(struct nhdp_neighbor *neigh) {
  struct nhdp_addr *naddr;

  avl_for_each_element(&neigh->_addresses, naddr, _neigh_node) {
    if (!naddr->lost) {
      naddr->_might_be_removed = true;
    }
    naddr->_this_if = false;
  }
}

/**
 * Callback triggered at the start of a HELLO message.
 * @param consumer tlvblock consumer
 * @param context message context
 * @return
 */
static enum rfc5444_result
_cb_message_start_callback(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
    struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  memset(&_current, 0, sizeof(_current));

  /* remember local NHDP interface */
  _current.localif = nhdp_interface_get(_protocol->input_interface->name);

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
  struct netaddr_str buf;

  OLSR_DEBUG(LOG_NHDP_R, "Incoming message type %d from %s through %s, got message tlvs",
      context->msg_type, netaddr_socket_to_string(&buf, _protocol->input_address),
      _protocol->input_interface->name);

  _current.vtime = rfc5444_timetlv_decode(
      _nhdp_message_tlvs[IDX_TLV_VTIME].tlv->single_value[0]);

  return RFC5444_OKAY;
}

/**
 * Create a nhdp address for a RFC5444 address with a
 *   "local_if" TLV attached to it
 * @param addr network addres
 * @return nhdp address
 */
static struct nhdp_addr *
_process_localif(struct netaddr *addr) {
  struct netaddr_str buf;
  struct nhdp_addr *naddr;

  /* section 12.3: Updating Neighbor Set */
  naddr = nhdp_db_addr_get(addr);
  if (naddr == NULL) {
    OLSR_DEBUG(LOG_NHDP_R, "No neighbor found for address %s",
        netaddr_to_string(&buf, addr));

    /* address not present in database, create it */
    if (_current.neighbor == NULL) {
      /* create a new nhdp neighbor first */
      _current.neighbor = nhdp_db_neighbor_insert();
      if (_current.neighbor == NULL) {
        /* out of memory */
        return NULL;
      }
    }

    /* create an address entry */
    naddr = nhdp_db_addr_insert(addr);
    if (naddr == NULL) {
      return NULL;
    }

    /* attach entry to neighbor */
    nhdp_db_addr_attach_neigh(naddr, _current.neighbor);
    return naddr;
  }

  /* address already exists, handle overlapping neighbor references */
  if (_current.neighbor == NULL) {
    /* its just the first address, everything is fine */
    _initialize_neighbor_for_processing(naddr->neigh);
    _current.neighbor = naddr->neigh;
  }
  else if (_current.neighbor != naddr->neigh) {
    /* overlapping neighbors, join them */
    _initialize_neighbor_for_processing(naddr->neigh);

    nhdp_db_neighbor_join(_current.neighbor, naddr->neigh);
  }

  /* mark the current address */
  naddr->_might_be_removed = false;

  return naddr;
}

/**
 * Handle incoming messages and its localif address TLVs
 * @param consumer tlvblock consumer
 * @param context message context
 * @return see rfc5444_result enum
 */
static enum rfc5444_result
_cb_localif_addresstlvs(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  uint8_t local_if, link_status;
  struct nhdp_addr *naddr;
  struct nhdp_link *lnk;
  struct netaddr addr;
  struct netaddr_str buf;

  local_if = 255;
  link_status = 255;

  if (netaddr_from_binary(&addr, context->addr, context->addr_len, 0)) {
    OLSR_WARN(LOG_NHDP_R, "Could not read incoming address of length %u", context->addr_len);
    return RFC5444_DROP_ADDRESS;
  }

  if (_nhdp_localif_address_tlvs[IDX_ADDRTLV1_LOCAL_IF].tlv) {
    local_if = _nhdp_localif_address_tlvs[IDX_ADDRTLV1_LOCAL_IF].tlv->single_value[0];
  }
  if (_nhdp_localif_address_tlvs[IDX_ADDRTLV1_LINK_STATUS].tlv) {
    link_status = _nhdp_localif_address_tlvs[IDX_ADDRTLV1_LINK_STATUS].tlv->single_value[0];
  }

  OLSR_DEBUG(LOG_NHDP_R, "Incoming NHDP message type %d, address %s, local_if %u, link_status %u",
      context->msg_type, netaddr_to_string(&buf, &addr), local_if, link_status);

  if (local_if != 255) {
    naddr = _process_localif(&addr);
    if (naddr == NULL) {
      return RFC5444_DROP_MESSAGE;
    }

    naddr->_this_if = local_if == RFC5444_LOCALIF_THIS_IF;

    /* prepare for section 12.5 */
    if (naddr->_this_if && naddr->link != NULL) {
      lnk = naddr->link;

      /* detach address from link */
      nhdp_db_addr_detach_link(naddr);

      if (!_current.multiple_links && _current.link != lnk) {
        if (_current.link == NULL) {
          /* remember the first link */
          _current.link = lnk;
        }
        else {
          OLSR_DEBUG(LOG_NHDP_R, "Overlapping link data detected");

          /* multiple links, all must be removed */
          _current.multiple_links = true;

          /* remove stored link */
          nhdp_db_link_remove(_current.link);
          _current.link = NULL;
        }
      }
      if (_current.multiple_links) {
        /* remove link */
        nhdp_db_link_remove(lnk);
      }
    }
  }

  /* detect if our own node is seen by our neighbor */
  if (link_status != 255 && nhdp_interface_addr_if_get(_current.localif, &addr) != NULL) {
    if (link_status == RFC5444_LINKSTATUS_LOST) {
      OLSR_DEBUG(LOG_NHDP_R, "connection lost to ourself for address %s", netaddr_to_string(&buf, &addr));
      _current.link_lost = true;
    }
    else {
      OLSR_DEBUG(LOG_NHDP_R, "we heard ourself for address %s", netaddr_to_string(&buf, &addr));
      _current.link_heard = true;
    }
  }
  return RFC5444_OKAY;
}

/**
 * Handle end of message for localif processing
 * @param consumer
 * @param context
 * @param dropped
 * @return
 */
static enum rfc5444_result
_cb_localif_end_callback(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
    struct rfc5444_reader_tlvblock_context *context __attribute__((unused)),
    bool dropped) {
  struct nhdp_addr *naddr, *na_it;
  struct nhdp_2hop *twohop, *twohop_it;
  struct nhdp_link *lnk;
  struct netaddr addr;
  uint64_t t;
  struct netaddr_str buf;

  OLSR_DEBUG(LOG_NHDP_R, "Localif_end_callback");
  if (dropped) {
    /* error in message processing */
    if (_current.neighbor) {
      /* clean up */
      nhdp_db_neighbor_remove(_current.neighbor);
    }
    return RFC5444_DROP_MESSAGE;
  }

  if (_current.neighbor == NULL) {
    /* add source address of incoming message as this_if */
    netaddr_from_socket(&addr, _protocol->input_address);

    naddr = _process_localif(&addr);
    if (!naddr) {
      /* out of memory */
      return RFC5444_DROP_MESSAGE;
    }
    naddr->_this_if = true;
  }

  /* handle lost addressed, but at least one address is still present */
  avl_for_each_element_safe(&_current.neighbor->_addresses, naddr, _neigh_node, na_it) {
    if (naddr->_might_be_removed) {
      lnk = naddr->link;

      OLSR_DEBUG(LOG_NHDP_R, "Address %s is not part of link anymore",
          netaddr_to_string(&buf, &naddr->if_addr));

      if (_current.neighbor->symmetric > 0) {
        /* Section 12.4: symmetric neighbor address got lost */
        nhdp_db_addr_set_lost(naddr, _current.localif->n_hold_time);

        /* Section 12.5.1 */
        nhdp_db_addr_detach_link(naddr);
      }
      else {
        /* it was not symmetric, just forget the address */
        nhdp_db_addr_remove(naddr);
      }

      /* Section 12.5.2: remove link if no address left */
      if (lnk != NULL && lnk->_addresses.count == 0) {
        nhdp_db_link_remove(lnk);
      }

      /* section 12.6.1: remove all similar n2 addresses */
      // TODO: not nice, replace with new iteration macro
      twohop_it = avl_find_element(&nhdp_2hop_tree, &naddr->if_addr, twohop_it, _global_node);
      while (twohop_it) {
        twohop = twohop_it;
        twohop_it = avl_next_element_safe(&nhdp_2hop_tree, twohop_it, _global_node);
        if (twohop_it != NULL && !twohop_it->_global_node.follower) {
          twohop_it = NULL;
        }

        nhdp_db_2hop_remove(twohop);
      }
    }
  }

  if (_current.link == NULL) {
    /* Section 12.5.3: no link there, create empty link tuple */
    _current.link = nhdp_db_link_insert(_current.neighbor, _current.localif);
    if (_current.link == NULL) {
      nhdp_db_neighbor_remove(_current.neighbor);
      return RFC5444_DROP_MESSAGE;
    }
  }

  avl_for_each_element(&_current.neighbor->_addresses, naddr, _neigh_node) {
    if (naddr->_this_if) {
      /* attach addresses of this_if to link */
      nhdp_db_addr_attach_link(naddr, _current.link);
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

  /* update link status */
  nhdp_db_link_update_status(_current.link);

  return RFC5444_OKAY;
}

/**
 * Handle incoming messages and its link_status/other_neigh address TLVs
 * @param consumer tlvblock consumer
 * @param context message context
 * @return see rfc5444_result enum
 */
static enum rfc5444_result
_cb_neigh2_addresstlvs(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  uint8_t link_status, other_neigh;
  struct nhdp_2hop *twohop;
  struct netaddr addr;
  struct netaddr_str buf;

  if (_current.link->status != NHDP_LINK_SYMMETRIC) {
    return RFC5444_OKAY;
  }

  link_status = 255;
  other_neigh = 255;

  if (netaddr_from_binary(&addr, context->addr, context->addr_len, 0)) {
    OLSR_WARN(LOG_NHDP_R, "Could not read incoming address of length %u", context->addr_len);
    return RFC5444_DROP_ADDRESS;
  }

  if (_nhdp_neigh_address_tlvs[IDX_ADDRTLV2_LINK_STATUS].tlv) {
    link_status = _nhdp_neigh_address_tlvs[IDX_ADDRTLV2_LINK_STATUS].tlv->single_value[0];
  }
  if (_nhdp_neigh_address_tlvs[IDX_ADDRTLV2_OTHER_NEIGHB].tlv) {
    other_neigh = _nhdp_neigh_address_tlvs[IDX_ADDRTLV2_OTHER_NEIGHB].tlv->single_value[0];
  }

  OLSR_DEBUG(LOG_NHDP_R, "Incoming NHDP message type %d, address %s, link_status %u, other_neigh: %u",
      context->msg_type, netaddr_to_string(&buf, &addr), link_status, other_neigh);

  if (link_status == 255 && other_neigh == 255) {
    /* neither link_status nor other_neigh set */
    return RFC5444_OKAY;
  }

  /* Section 12.6.2.1 */
  if (nhdp_interface_addr_global_get(&addr) != NULL) {
    /* is local interface address */
    return RFC5444_OKAY;
  }

  if (avl_find(&_current.neighbor->_addresses, &addr)) {
    /* is address of this neighbor */
    return RFC5444_OKAY;
  }

  if (link_status == RFC5444_LINKSTATUS_SYMMETRIC
      || other_neigh == RFC5444_OTHERNEIGHB_SYMMETRIC){
    /* Section 12.6.2.1: new 2-hop neighbor */
    twohop  = nhdp_db_2hop_get(_current.link, &addr);
    if (twohop == NULL) {
      twohop = nhdp_db_2hop_insert(_current.link, &addr);
    }
    if (twohop != NULL) {
      nhdp_db_2hop_set_vtime(twohop, _current.vtime);
    }
  }
  else {
    /* Section 12.6.2.2: lost 2-hop neighbor */
    twohop = nhdp_db_2hop_get(_current.link, &addr);
    if (twohop) {
      nhdp_db_2hop_remove(twohop);
    }
  }

  return RFC5444_OKAY;
}
