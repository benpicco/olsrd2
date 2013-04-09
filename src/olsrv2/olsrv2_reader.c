
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

#include "olsrv2/olsrv2_reader.h"

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

static enum rfc5444_result
_cb_messagetlvs(struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);

static enum rfc5444_result
_cb_addresstlvs(struct rfc5444_reader_tlvblock_consumer *consumer,
      struct rfc5444_reader_tlvblock_context *context);
static enum rfc5444_result _cb_addresstlvs_end(struct rfc5444_reader_tlvblock_consumer *,
    struct rfc5444_reader_tlvblock_context *context, bool dropped);

/* definition of the RFC5444 reader components */
static struct rfc5444_reader_tlvblock_consumer _olsrv2_message_consumer = {
  .order = RFC5444_MAIN_PARSER_PRIORITY,
  .msg_id = RFC5444_MSGTYPE_TC,
  .block_callback = _cb_messagetlvs,
  .end_callback = _cb_addresstlvs_end,
};

static struct rfc5444_reader_tlvblock_consumer_entry _olsrv2_message_tlvs[] = {
  [IDX_TLV_ITIME] = { .type = RFC5444_MSGTLV_INTERVAL_TIME, .type_ext = 0, .match_type_ext = true,
      .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_VTIME] = { .type = RFC5444_MSGTLV_VALIDITY_TIME, .type_ext = 0, .match_type_ext = true,
      .mandatory = true, .min_length = 1, .match_length = true },
  [IDX_TLV_CONT_SEQ_NUM] = { .type = RFC5444_MSGTLV_CONT_SEQ_NUM,
      .mandatory = true, .min_length = 1, .max_length = 512, .match_length = true },
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

static enum log_source LOG_NHDP_R = LOG_MAIN;

/**
 * Initialize nhdp reader
 */
void
olsrv2_reader_init(struct olsr_rfc5444_protocol *p) {
  _protocol = p;

  LOG_NHDP_R = olsr_log_register_source("nhdp_r");

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
_cb_messagetlvs(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  return RFC5444_OKAY;
}

static enum rfc5444_result
_cb_addresstlvs(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  return RFC5444_OKAY;
}

static enum rfc5444_result
_cb_addresstlvs_end(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
    struct rfc5444_reader_tlvblock_context *context __attribute__((unused)), bool dropped __attribute__((unused))) {
  return RFC5444_OKAY;
}
