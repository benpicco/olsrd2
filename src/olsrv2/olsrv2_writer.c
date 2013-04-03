
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

#include "olsrv2/olsrv2.h"
#include "olsrv2/olsrv2_writer.h"

/* constants */
enum {
  IDX_ADDRTLV_NBR_ADDR_TYPE,
  IDX_ADDRTLV_GATEWAY,
};

/* Prototypes */
static void _cb_addMessageHeader(
    struct rfc5444_writer *, struct rfc5444_writer_message *);
static void _cb_addMessageTLVs(
    struct rfc5444_writer *, struct rfc5444_writer_content_provider *);
static void _cb_addAddresses(
    struct rfc5444_writer *, struct rfc5444_writer_content_provider *);

/* definition of NHDP writer */
static struct rfc5444_writer_message *_olsrv2_message = NULL;

static struct rfc5444_writer_content_provider _olsrv2_msgcontent_provider = {
  .msg_type = RFC5444_MSGTYPE_TC,
  .addMessageTLVs = _cb_addMessageTLVs,
  .addAddresses = _cb_addAddresses,
};

static struct rfc5444_writer_tlvtype _olsrv2_addrtlvs[] = {
  [IDX_ADDRTLV_NBR_ADDR_TYPE] =  { .type = RFC5444_ADDRTLV_NBR_ADDR_TYPE },
  [IDX_ADDRTLV_GATEWAY]       =  { .type = RFC5444_ADDRTLV_GATEWAY },
};

static struct olsr_rfc5444_protocol *_protocol;

static enum log_source LOG_OLSRV2_W = LOG_MAIN;

int
olsrv2_writer_init(struct olsr_rfc5444_protocol *protocol) {
  _protocol = protocol;

  LOG_OLSRV2_W = olsr_log_register_source("olsrv2_w");

  _olsrv2_message = rfc5444_writer_register_message(
      &_protocol->writer, RFC5444_MSGTYPE_HELLO, true, 4);
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

static void
_cb_addMessageHeader(struct rfc5444_writer *writer,
    struct rfc5444_writer_message *message) {
  const struct netaddr *orig;

  orig = olsrv2_get_originator();
  if (netaddr_get_address_family(orig) == AF_INET) {
    rfc5444_writer_set_msg_addrlen(writer, message, 4);
  }
  else  {
    rfc5444_writer_set_msg_addrlen(writer, message, 6);
  }
  rfc5444_writer_set_msg_header(writer, message, true, true, true, true);

  OLSR_DEBUG(LOG_OLSRV2_W, "Generate TC");
}

static void
_cb_addMessageTLVs(struct rfc5444_writer *writer,
    struct rfc5444_writer_content_provider *prv) {
  uint8_t vtime_encoded, itime_encoded;


}

static void
_cb_addAddresses(struct rfc5444_writer *writer,
    struct rfc5444_writer_content_provider *prv) {

}
