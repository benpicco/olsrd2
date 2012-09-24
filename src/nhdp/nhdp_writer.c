
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
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_logging.h"
#include "tools/olsr_rfc5444.h"

#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp.h"
#include "nhdp/nhdp_writer.h"

enum {
  IDX_TLV_LOCALIF,
  IDX_TLV_LINKSTATUS,
  IDX_TLV_OTHERNEIGHB,
};

static void _cb_addMessageHeader(
    struct rfc5444_writer *, struct rfc5444_writer_message *);
static void _cb_addMessageTLVs(
    struct rfc5444_writer *, struct rfc5444_writer_content_provider *);
static void _cb_addAddresses(
    struct rfc5444_writer *, struct rfc5444_writer_content_provider *);

/* definition of NHDP writer */
static struct rfc5444_writer_message *_nhdp_message = NULL;

static struct rfc5444_writer_content_provider _nhdp_msgcontent_provider = {
  .msg_type = RFC5444_MSGTYPE_HELLO,
  .addMessageTLVs = _cb_addMessageTLVs,
  .addAddresses = _cb_addAddresses,
};

static struct rfc5444_writer_addrtlv_block _nhdp_addrtlvs[] = {
  [IDX_TLV_LOCALIF] =     { .type = RFC5444_ADDRTLV_LOCAL_IF },
  [IDX_TLV_LINKSTATUS] =  { .type = RFC5444_ADDRTLV_LINK_STATUS },
  [IDX_TLV_OTHERNEIGHB] = { .type = RFC5444_ADDRTLV_OTHER_NEIGHB },
};

static struct olsr_rfc5444_protocol *_protocol;

static enum log_source LOG_NHDP_W = LOG_MAIN;

/**
 * Initialize nhdp writer
 */
int
nhdp_writer_init(struct olsr_rfc5444_protocol *p) {
  _protocol = p;

  LOG_NHDP_W = olsr_log_register_source("nhdp_w");

  _nhdp_message = rfc5444_writer_register_message(
      &_protocol->writer, RFC5444_MSGTYPE_HELLO, true, 4);
  if (_nhdp_message == NULL) {
    OLSR_WARN(LOG_NHDP_W, "Could not register NHDP Hello message");
    return -1;
  }

  _nhdp_message->addMessageHeader = _cb_addMessageHeader;

  if (rfc5444_writer_register_msgcontentprovider(
      &_protocol->writer, &_nhdp_msgcontent_provider,
      _nhdp_addrtlvs, ARRAYSIZE(_nhdp_addrtlvs))) {

    OLSR_WARN(LOG_NHDP_W, "Count not register NHDP msg contentprovider");
    rfc5444_writer_unregister_message(&_protocol->writer, _nhdp_message);
    return -1;
  }
  return 0;
}

/**
 * Cleanup nhdp writer
 */
void
nhdp_writer_cleanup(void) {
  /* remove pbb writer */
  rfc5444_writer_unregister_content_provider(
      &_protocol->writer, &_nhdp_msgcontent_provider,
      _nhdp_addrtlvs, ARRAYSIZE(_nhdp_addrtlvs));
  rfc5444_writer_unregister_message(&_protocol->writer, _nhdp_message);
}

static void
_cb_addMessageHeader(struct rfc5444_writer *writer,
    struct rfc5444_writer_message *message) {
  struct olsr_rfc5444_target *target;
  const struct netaddr *originator;
  struct netaddr_str buf;
  bool ipv6;

  if (!message->if_specific) {
    OLSR_WARN(LOG_NHDP_W, "non interface-specific NHDP message!");
    return;
  }

  target = olsr_rfc5444_get_target_from_message(message);

  ipv6 = target == target->interface->multicast6;
  if (!ipv6 && target != target->interface->multicast4) {
    OLSR_WARN(LOG_NHDP_W, "Cannot generate unicast nhdp message to %s",
        netaddr_to_string(&buf, &target->dst));
    return;
  }

  originator = nhdp_get_originator();

  if (netaddr_get_address_family(originator) != AF_UNSPEC) {
    rfc5444_writer_set_msg_header(writer, message, true, false, true, true);
    rfc5444_writer_set_msg_addrlen(writer, message, netaddr_get_binlength(originator));
    rfc5444_writer_set_msg_originator(writer, message, netaddr_get_binptr(originator));
  }
  else {
    rfc5444_writer_set_msg_header(writer, message, false, false, true, true);
    rfc5444_writer_set_msg_addrlen(writer, message, ipv6 ? 16 : 4);
  }

  rfc5444_writer_set_msg_hoplimit(writer, message, 1);
  rfc5444_writer_set_msg_seqno(writer, message, olsr_rfc5444_next_target_seqno(target));

  OLSR_DEBUG(LOG_NHDP_W, "Generate Hello for originator %s on interface %s",
      netaddr_to_string(&buf, originator), target->interface->name);
}

static void
_cb_addMessageTLVs(struct rfc5444_writer *writer,
    struct rfc5444_writer_content_provider *prv) {
  uint8_t vtime_encoded, itime_encoded;
  struct olsr_rfc5444_target *target;
  struct nhdp_interface *interf;

  target = olsr_rfc5444_get_target_from_provider(prv);

  if (target != target->interface->multicast4
      && target != target->interface->multicast6) {
    return;
  }

  interf = nhdp_interface_get(target->interface->name);
  if (interf == NULL) {
    OLSR_WARN(LOG_NHDP_W, "Unknown interface for nhdp message: %s", target->interface->name);
    return;
  }
  itime_encoded = rfc5444_timetlv_encode(interf->refresh_interval);
  vtime_encoded = rfc5444_timetlv_encode(interf->h_hold_time);

  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_INTERVAL_TIME, 0,
      &itime_encoded, sizeof(itime_encoded));
  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_VALIDITY_TIME, 0,
      &vtime_encoded, sizeof(vtime_encoded));
}

void
_cb_addAddresses(struct rfc5444_writer *writer __attribute__((unused)),
    struct rfc5444_writer_content_provider *prv) {
  struct olsr_rfc5444_target *target;

  struct rfc5444_writer_address *address;
  struct nhdp_interface *interf;
  struct nhdp_interface_addr *addr;
  struct nhdp_addr *naddr;
  struct netaddr_str buf;
  bool this_if;
  uint8_t linkstatus, otherneigh;

  target = olsr_rfc5444_get_target_from_provider(prv);
  if (target != target->interface->multicast4
      && target != target->interface->multicast6) {
    return;
  }

  interf = nhdp_interface_get(target->interface->name);
  if (interf == NULL) {
    OLSR_WARN(LOG_NHDP_W, "Unknown interface for nhdp message: %s", target->interface->name);
    return;
  }

  avl_for_each_element(&nhdp_ifaddr_tree, addr, _global_node) {
    /* check if address of local interface */
    this_if = NULL != avl_find_element(
        &interf->_if_addresses, &addr->if_addr, addr, _if_node);

    OLSR_DEBUG(LOG_NHDP_W, "Add %s (%s) to NHDP hello",
        netaddr_to_string(&buf, &addr->if_addr), this_if ? "this_if" : "other_if");

    /* generate RFC5444 address */
    address = rfc5444_writer_add_address(writer, prv->_creator,
        netaddr_get_binptr(&addr->if_addr),
        netaddr_get_prefix_length(&addr->if_addr), true);
    if (address == NULL) {
      OLSR_WARN(LOG_NHDP_W, "Could not add address %s to NHDP hello",
          netaddr_to_string(&buf, &addr->if_addr));
      continue;
    }

    /* Add LOCALIF TLV */
    if (this_if) {
      rfc5444_writer_add_addrtlv(writer, address,
          _nhdp_addrtlvs[IDX_TLV_LOCALIF]._tlvtype,
          &RFC5444_LOCALIF_THIS_IF, sizeof(RFC5444_LOCALIF_THIS_IF), true);
    }
    else {
      rfc5444_writer_add_addrtlv(writer, address,
          _nhdp_addrtlvs[IDX_TLV_LOCALIF]._tlvtype,
          &RFC5444_LOCALIF_OTHER_IF, sizeof(RFC5444_LOCALIF_OTHER_IF), true);
    }
  }

  avl_for_each_element(&nhdp_addr_tree, naddr, _global_node) {
    /* initialize flags for this address */
    linkstatus = 255;
    otherneigh = 255;

    if (naddr->link != NULL && naddr->link->local_if == interf
        && naddr->link->status != NHDP_LINK_PENDING) {
      linkstatus = naddr->link->status;
    }

    if (naddr->neigh != NULL && naddr->neigh->symmetric > 0
        && linkstatus != RFC5444_LINKSTATUS_SYMMETRIC) {
      otherneigh = RFC5444_OTHERNEIGHB_SYMMETRIC;
    }

    if (naddr->lost && linkstatus == 255 && otherneigh == 255) {
      otherneigh = RFC5444_OTHERNEIGHB_LOST;
    }

    if (linkstatus == 255 && otherneigh == 255) {
      continue;

    }
    /* generate RFC5444 address */
    address = rfc5444_writer_add_address(writer, prv->_creator,
        netaddr_get_binptr(&naddr->if_addr),
        netaddr_get_prefix_length(&naddr->if_addr), true);
    if (address == NULL) {
      OLSR_WARN(LOG_NHDP_W, "Could not add address %s to NHDP hello",
          netaddr_to_string(&buf, &addr->if_addr));
      continue;
    }

    if (linkstatus != 255) {
      rfc5444_writer_add_addrtlv(writer, address,
            _nhdp_addrtlvs[IDX_TLV_LINKSTATUS]._tlvtype,
            &linkstatus, sizeof(linkstatus), true);

      OLSR_DEBUG(LOG_NHDP_W, "Add %s (linkstatus=%d) to NHDP hello",
          netaddr_to_string(&buf, &addr->if_addr), naddr->link->status);
    }

    if (otherneigh != 255) {
      rfc5444_writer_add_addrtlv(writer, address,
          _nhdp_addrtlvs[IDX_TLV_OTHERNEIGHB]._tlvtype,
          &RFC5444_OTHERNEIGHB_SYMMETRIC, sizeof(RFC5444_OTHERNEIGHB_SYMMETRIC), true);

      OLSR_DEBUG(LOG_NHDP_W, "Add %s (otherneigh=%d) to NHDP hello",
          netaddr_to_string(&buf, &naddr->if_addr), otherneigh);
    }
  }
}
