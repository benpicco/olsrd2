
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
#include "common/avl.h"
#include "common/avl_comp.h"
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444_conversion.h"
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_logging.h"
#include "tools/olsr_rfc5444.h"

#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp.h"
#include "nhdp/nhdp_writer.h"

/* constants */
enum {
  IDX_TLV_LOCALIF,
  IDX_TLV_LINKSTATUS,
  IDX_TLV_OTHERNEIGHB,
};

/* prototypes */
static void _cb_addMessageHeader(
    struct rfc5444_writer *, struct rfc5444_writer_message *);
static void _cb_addMessageTLVs(
    struct rfc5444_writer *, struct rfc5444_writer_content_provider *);
static void _cb_addAddresses(
    struct rfc5444_writer *, struct rfc5444_writer_content_provider *);

static void _add_link_address(struct rfc5444_writer *writer,
    struct rfc5444_writer_content_provider *prv,
    struct nhdp_interface *interf, struct nhdp_addr *naddr);
static void _add_localif_address(struct rfc5444_writer *writer,
    struct rfc5444_writer_content_provider *prv,
    struct nhdp_interface *interf, struct nhdp_interface_addr *addr);
static struct rfc5444_writer_address *_add_rfc5444_address(
    struct rfc5444_writer *writer,   struct rfc5444_writer_message *creator,
    struct netaddr *addr);

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

/**
 * Callback to initialize the message header for a HELLO message
 * @param writer
 * @param message
 */
static void
_cb_addMessageHeader(struct rfc5444_writer *writer,
    struct rfc5444_writer_message *message) {
  struct olsr_rfc5444_target *target;
  struct nhdp_interface *interf;
  struct netaddr_str buf;

  if (!message->if_specific) {
    OLSR_WARN(LOG_NHDP_W, "non interface-specific NHDP message!");
    assert (0);
  }

  target = olsr_rfc5444_get_target_from_message(message);
  if (target != target->interface->multicast6
      && target != target->interface->multicast4) {
    OLSR_WARN(LOG_NHDP_W, "Cannot generate unicast nhdp message to %s",
        netaddr_to_string(&buf, &target->dst));
    return;
  }

  interf = nhdp_interface_get(target->interface->name);
  assert(interf);

  if (netaddr_get_address_family(&target->dst) == AF_INET) {
    rfc5444_writer_set_msg_addrlen(writer, message, 4);
  }
  else {
    rfc5444_writer_set_msg_addrlen(writer, message, 16);
  }

  rfc5444_writer_set_msg_header(writer, message, false, false, true, true);
  rfc5444_writer_set_msg_hoplimit(writer, message, 1);
  rfc5444_writer_set_msg_seqno(writer, message, olsr_rfc5444_next_target_seqno(target));

  OLSR_DEBUG(LOG_NHDP_W, "Generate Hello on interface %s",
      target->interface->name);
}

/**
 * Callback to add the message TLVs to a HELLO message
 * @param writer
 * @param prv
 */
static void
_cb_addMessageTLVs(struct rfc5444_writer *writer,
    struct rfc5444_writer_content_provider *prv) {
  uint8_t vtime_encoded, itime_encoded;
  struct olsr_rfc5444_target *target;
  struct nhdp_interface *interf;

  target = olsr_rfc5444_get_target_from_provider(prv);

  if (target != target->interface->multicast4
      && target != target->interface->multicast6) {
    struct netaddr_str buf;
    OLSR_WARN(LOG_NHDP_W, "target for NHDP is no interface multicast: %s",
        netaddr_to_string(&buf, &target->dst));
    assert(0);
  }

  interf = nhdp_interface_get(target->interface->name);
  if (interf == NULL) {
    OLSR_WARN(LOG_NHDP_W, "Unknown interface for nhdp message: %s", target->interface->name);
    assert(0);
  }
  itime_encoded = rfc5444_timetlv_encode(interf->refresh_interval);
  vtime_encoded = rfc5444_timetlv_encode(interf->h_hold_time);

  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_INTERVAL_TIME, 0,
      &itime_encoded, sizeof(itime_encoded));
  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_VALIDITY_TIME, 0,
      &vtime_encoded, sizeof(vtime_encoded));
}

/**
 * Create a rfc5444 addres object, embed IPv4 address if necessary
 * @param writer
 * @param creator
 * @param addr network address
 * @return pointer to rfc5444 address, NULL if an error happened
 */
static struct rfc5444_writer_address *
_add_rfc5444_address(struct rfc5444_writer *writer,   struct rfc5444_writer_message *creator,
    struct netaddr *addr) {
  struct netaddr embedded;

  if (netaddr_get_address_family(addr) == AF_INET
      && creator->addr_len == 16) {
    /* generate embedded address */
    netaddr_embed_ipv4(&embedded, addr);
    return rfc5444_writer_add_address(writer, creator,
        netaddr_get_binptr(&embedded),
        netaddr_get_prefix_length(&embedded), true);
  }
  else {
    /* address should already be the right length */
    assert (netaddr_get_prefix_length(addr) == creator->addr_len*8);

    return rfc5444_writer_add_address(writer, creator,
        netaddr_get_binptr(addr), netaddr_get_prefix_length(addr), true);
  }
}

/**
 * Add a rfc5444 address with localif TLV to the stream
 * @param writer
 * @param prv
 * @param interf
 * @param addr
 */
static void
_add_localif_address(struct rfc5444_writer *writer, struct rfc5444_writer_content_provider *prv,
    struct nhdp_interface *interf, struct nhdp_interface_addr *addr) {
  struct rfc5444_writer_address *address;
  struct netaddr_str buf;
  uint8_t value;
  bool this_if;

  /* check if address of local interface */
  this_if = NULL != avl_find_element(
      &interf->_if_addresses, &addr->if_addr, addr, _if_node);

  OLSR_DEBUG(LOG_NHDP_W, "Add %s (%s) to NHDP hello",
      netaddr_to_string(&buf, &addr->if_addr), this_if ? "this_if" : "other_if");

  /* generate RFC5444 address */
  address = _add_rfc5444_address(writer, prv->creator, &addr->if_addr);
  if (address == NULL) {
    OLSR_WARN(LOG_NHDP_W, "Could not add address %s to NHDP hello",
        netaddr_to_string(&buf, &addr->if_addr));
    return;
  }

  /* Add LOCALIF TLV */
  if (this_if) {
    value = RFC5444_LOCALIF_THIS_IF;
  }
  else {
    value = RFC5444_LOCALIF_OTHER_IF;
  }
  rfc5444_writer_add_addrtlv(writer, address,
      _nhdp_addrtlvs[IDX_TLV_LOCALIF]._tlvtype,
      &value, sizeof(value), true);
}

/**
 * Add a rfc5444 address with link_status or other_neigh TLV to the stream
 * @param writer
 * @param prv
 * @param interf
 * @param naddr
 */
static void
_add_link_address(struct rfc5444_writer *writer, struct rfc5444_writer_content_provider *prv,
    struct nhdp_interface *interf, struct nhdp_addr *naddr) {
  struct rfc5444_writer_address *address;
  struct netaddr_str buf;
  uint8_t linkstatus, otherneigh;
  uint8_t value;

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
    return;
  }

  /* generate RFC5444 address */
  address = _add_rfc5444_address(writer, prv->creator, &naddr->if_addr);
  if (address == NULL) {
    OLSR_WARN(LOG_NHDP_W, "Could not add address %s to NHDP hello",
        netaddr_to_string(&buf, &naddr->if_addr));
    return;
  }

  if (linkstatus != 255) {
    rfc5444_writer_add_addrtlv(writer, address,
          _nhdp_addrtlvs[IDX_TLV_LINKSTATUS]._tlvtype,
          &linkstatus, sizeof(linkstatus), true);

    OLSR_DEBUG(LOG_NHDP_W, "Add %s (linkstatus=%d) to NHDP hello",
        netaddr_to_string(&buf, &naddr->if_addr), naddr->link->status);
  }

  if (otherneigh != 255) {
    value = RFC5444_OTHERNEIGHB_SYMMETRIC;
    rfc5444_writer_add_addrtlv(writer, address,
        _nhdp_addrtlvs[IDX_TLV_OTHERNEIGHB]._tlvtype,
        &value, sizeof(value), true);

    OLSR_DEBUG(LOG_NHDP_W, "Add %s (otherneigh=%d) to NHDP hello",
        netaddr_to_string(&buf, &naddr->if_addr), otherneigh);
  }
}

/**
 * Callback to add the addresses and address TLVs to a HELLO message
 * @param writer
 * @param prv
 */
void
_cb_addAddresses(struct rfc5444_writer *writer,
    struct rfc5444_writer_content_provider *prv) {
  struct olsr_rfc5444_target *target;

  struct nhdp_interface *interf;
  struct nhdp_interface_addr *addr;
  struct nhdp_addr *naddr;

  /* have already be checked for message TLVs, so they cannot be NULL */
  target = olsr_rfc5444_get_target_from_provider(prv);
  interf = nhdp_interface_get(target->interface->name);

  addr = avl_first_element_safe(&nhdp_ifaddr_tree, addr, _global_node);
  naddr = avl_first_element_safe(&nhdp_addr_tree, naddr, _global_node);

  /* produce a sorted list of addr/naddr objects */
  while (addr != NULL || naddr != NULL) {
    /* if there is no naddr anymore or naddr>addr then... */
    if (naddr == NULL || (addr != NULL &&
        avl_comp_netaddr_socket(&naddr->if_addr, &addr->if_addr, NULL) > 0)) {
      /* add another addr with TLV to the steam */
      if (!addr->_global_node.follower && !addr->removed) {
        /* each address only once and only active ones */
        if (netaddr_get_address_family(&addr->if_addr) != AF_INET6
          || netaddr_get_address_family(&target->dst) != AF_INET) {
          /* do not send IPv6 addresses over IPv4 */
          _add_localif_address(writer, prv, interf, addr);
        }
      }

      /* move to next addr, NULL if end reached */
      addr = avl_next_element_safe(&nhdp_ifaddr_tree, addr, _global_node);
    }
    else {
      /* otherwise add another naddr with TLVs to the stream */
      if (netaddr_get_address_family(&naddr->if_addr) != AF_INET6
          || netaddr_get_address_family(&target->dst) != AF_INET) {
        _add_link_address(writer, prv, interf, naddr);
      }

      /* move to next naddr, NULL if end reached */
      naddr = avl_next_element_safe(&nhdp_addr_tree, naddr, _global_node);
    }
  }
}
