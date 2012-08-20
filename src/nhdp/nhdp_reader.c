
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
#include "nhdp/nhdp_reader.h"

/* DLEP TLV array index */
enum dlep_tlv_idx {
  IDX_TLV_ITIME,
  IDX_TLV_VTIME,
};

enum dlep_addrtlv_idx {
  IDX_ADDRTLV_LOCAL_IF,
  IDX_ADDRTLV_LINK_STATUS,
  IDX_ADDRTLV_OTHER_NEIGHB,
};

/* prototypes */
static enum rfc5444_result
_cb_nhdp_messagetlvs(struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result
_cb_nhdp_addresstlvs(struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);

/* definition of the RFC5444 reader components */
static struct rfc5444_reader_tlvblock_consumer _nhdp_message_consumer = {
  .block_callback = _cb_nhdp_messagetlvs,
  .block_callback_failed_constraints = NULL,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_message_tlvs[] = {
  [IDX_TLV_ITIME] = { .type = RFC5444_MSGTLV_INTERVAL_TIME, .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_VTIME] = { .type = RFC5444_MSGTLV_VALIDITY_TIME, .mandatory = true, .min_length = 1, .match_length = true },
};

static struct rfc5444_reader_tlvblock_consumer _nhdp_address_consumer = {
  .block_callback = _cb_nhdp_addresstlvs,
  .block_callback_failed_constraints = NULL,
};

static struct rfc5444_reader_tlvblock_consumer_entry _nhdp_address_tlvs[] = {
  [IDX_ADDRTLV_LOCAL_IF] = { .type = RFC5444_ADDRTLV_LOCAL_IF, .min_length = 1, .match_length = true },
  [IDX_ADDRTLV_LINK_STATUS] = { .type = RFC5444_ADDRTLV_LINK_STATUS, .min_length = 1, .match_length = true },
  [IDX_ADDRTLV_OTHER_NEIGHB] = { .type = RFC5444_ADDRTLV_OTHER_NEIGHB, .min_length = 1, .match_length = true },
};

/* nhdp multiplexer/protocol */
struct olsr_rfc5444_protocol *_protocol = NULL;

/**
 * Initialize nhdp reader
 */
void
nhdp_reader_init(struct olsr_rfc5444_protocol *p) {
  _protocol = p;

  rfc5444_reader_add_message_consumer(
      &_protocol->reader, &_nhdp_message_consumer,
      _nhdp_message_tlvs, ARRAYSIZE(_nhdp_message_tlvs), RFC5444_MSGTYPE_HELLO, 0);
  rfc5444_reader_add_address_consumer(
      &_protocol->reader, &_nhdp_address_consumer,
      _nhdp_address_tlvs, ARRAYSIZE(_nhdp_address_tlvs), RFC5444_MSGTYPE_HELLO, 1);
}

/**
 * Cleanup nhdp reader
 */
void
nhdp_reader_cleanup(void) {
  rfc5444_reader_remove_address_consumer(
      &_protocol->reader, &_nhdp_address_consumer);
  rfc5444_reader_remove_message_consumer(
      &_protocol->reader, &_nhdp_message_consumer);

  olsr_rfc5444_remove_protocol(_protocol);
}

/**
 * Handle incoming messages and its TLVs
 * @param consumer tlvblock consumer
 * @param context message context
 * @return see rfc5444_result enum
 */
static enum rfc5444_result
_cb_nhdp_messagetlvs(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {

  OLSR_DEBUG(LOG_NHDP, "Incoming message type %d, got message tlvs", context->msg_type);
  return RFC5444_OKAY;
}

/**
 * Handle incoming messages and its address TLVs
 * @param consumer tlvblock consumer
 * @param context message context
 * @return see rfc5444_result enum
 */
static enum rfc5444_result
_cb_nhdp_addresstlvs(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  uint8_t local_if, link_status, other_neigh;

  local_if = 255;
  link_status = 255;
  other_neigh = 255;

  if (_nhdp_address_tlvs[IDX_ADDRTLV_LOCAL_IF].tlv) {
    local_if = _nhdp_address_tlvs[IDX_ADDRTLV_LOCAL_IF].tlv->single_value[0];
  }
  if (_nhdp_address_tlvs[IDX_ADDRTLV_LINK_STATUS].tlv) {
    link_status = _nhdp_address_tlvs[IDX_ADDRTLV_LINK_STATUS].tlv->single_value[0];
  }
  if (_nhdp_address_tlvs[IDX_ADDRTLV_OTHER_NEIGHB].tlv) {
    other_neigh = _nhdp_address_tlvs[IDX_ADDRTLV_OTHER_NEIGHB].tlv->single_value[0];
  }

  OLSR_DEBUG(LOG_NHDP, "Incoming message type %d, address %s, tlvs (%u/%u/%u)",
      context->msg_type, /* TODO: */ "", local_if, link_status, other_neigh);

  if (local_if == RFC5444_LOCALIF_THIS_IF) {
    /* add local neighbors interface */
    return RFC5444_OKAY;
  }
  else if (local_if == RFC5444_LOCALIF_OTHER_IF){
    /* add local neighbors remote interface */
    return RFC5444_OKAY;
  }



  return RFC5444_OKAY;
}
