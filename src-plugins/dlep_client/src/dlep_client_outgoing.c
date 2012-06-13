
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

#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444_conversion.h"
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_logging.h"
#include "core/olsr_timer.h"
#include "core/olsr.h"

#include "dlep_iana.h"
#include "dlep_client.h"
#include "dlep_client_outgoing.h"

static void _cb_addMessageHeader(struct rfc5444_writer *,
    struct rfc5444_writer_message *);
static void _cb_addMessageTLVs(struct rfc5444_writer *,
    struct rfc5444_writer_content_provider *);
static void _cb_router_connect(void *);

/* DLEP writer data */
static uint8_t _msg_buffer[1500];
static uint8_t _msg_addrtlvs[5000];

static enum dlep_orders _msg_order;
static struct _dlep_service_session *_msg_session;
static bool _msg_explicit_connectto;

static struct rfc5444_writer _dlep_writer = {
  .msg_buffer = _msg_buffer,
  .msg_size = sizeof(_msg_buffer),
  .addrtlv_buffer = _msg_addrtlvs,
  .addrtlv_size = sizeof(_msg_addrtlvs),
};

static struct rfc5444_writer_message *_dlep_message = NULL;

static struct rfc5444_writer_content_provider _dlep_msgcontent_provider = {
  .msg_type = DLEP_MESSAGE_ID,
  .addMessageTLVs = _cb_addMessageTLVs,
};

#if 0
static struct rfc5444_writer_addrtlv_block _dlep_addrtlvs[] = {
  { .type = DLEP_ADDRTLV_CUR_RATE },
};
#endif

/* infrastructure */
static struct olsr_timer_info _tinfo_router_connect = {
  .name = "dlep interface discovery",
  .callback = _cb_router_connect,
  .periodic = true,
};
static struct olsr_timer_entry _tentry_router_connect = {
  .info = &_tinfo_router_connect,
};

/* outgoing subsystem */
OLSR_SUBSYSTEM_STATE(_dlep_client_outgoing);

/**
 * Initialize DLEP Client RFC5444 generation
 * @return -1 if an error happened, 0 otherwise
 */
int
dlep_client_outgoing_init(void) {
  if (olsr_subsystem_init(&_dlep_client_outgoing))
    return 0;

  rfc5444_writer_init(&_dlep_writer);

  _dlep_message = rfc5444_writer_register_message(&_dlep_writer, DLEP_MESSAGE_ID, true, 6);
  if (_dlep_message == NULL) {
    OLSR_WARN(LOG_DLEP_CLIENT, "Could not register DLEP message");
    rfc5444_writer_cleanup(&_dlep_writer);
    return -1;
  }
  _dlep_message->addMessageHeader = _cb_addMessageHeader;

#if 0
  if (rfc5444_writer_register_msgcontentprovider(&_dlep_writer,
      &_dlep_msgcontent_provider, _dlep_addrtlvs, ARRAYSIZE(_dlep_addrtlvs))) {
#endif
  if (rfc5444_writer_register_msgcontentprovider(&_dlep_writer,
      &_dlep_msgcontent_provider, NULL, 0)) {

    OLSR_WARN(LOG_DLEP_CLIENT, "Count not register DLEP msg contentprovider");
    rfc5444_writer_unregister_message(&_dlep_writer, _dlep_message);
    rfc5444_writer_cleanup(&_dlep_writer);
    return -1;
  }

  olsr_timer_add(&_tinfo_router_connect);

  return 0;
}

/**
 * Cleanup all data allocated for RFC 5444 generation
 */
void
dlep_client_outgoing_cleanup(void) {
  if (olsr_subsystem_cleanup(&_dlep_client_outgoing))
    return;

  /* remove validity timer */
  olsr_timer_remove(&_tinfo_router_connect);

  /* remove pbb writer */
#if 0
  rfc5444_writer_unregister_content_provider(&_dlep_writer, &_dlep_msgcontent_provider,
      _dlep_addrtlvs, ARRAYSIZE(_dlep_addrtlvs));
#endif
  rfc5444_writer_unregister_content_provider(&_dlep_writer, &_dlep_msgcontent_provider,
      NULL, 0);
  rfc5444_writer_unregister_message(&_dlep_writer, _dlep_message);
  rfc5444_writer_cleanup(&_dlep_writer);
}

/**
 * Add a packetbb interface to the writer instance
 * @param pbbif pointer to packetbb interface
 */
void
dlep_client_registerif(struct rfc5444_writer_interface *pbbif) {
  rfc5444_writer_register_interface(&_dlep_writer, pbbif);
}

/**
 * Remove a packetbb interface to the writer instance
 * @param pbbif pointer to packetbb interface
 */
void
dlep_client_unregisterif(struct rfc5444_writer_interface *pbbif) {
  rfc5444_writer_unregister_interface(&_dlep_writer, pbbif);
}

/**
 * Reset timer settings according to configuration
 */
void
dlep_client_outgoing_reconfigure(void) {
  olsr_timer_set(&_tentry_router_connect, _client_config.connect_interval);
}

/**
 * Add message TLVs for Connect Router DLEP messages
 */
static void
_add_connectrouter_msgtlvs(void) {
  uint8_t encoded_vtime;

  /* encode vtime according to RFC 5497 */
  encoded_vtime = rfc5444_timetlv_encode(_client_config.connect_validity);

  rfc5444_writer_add_messagetlv(&_dlep_writer, RFC5444_MSGTLV_VALIDITY_TIME, 0,
      &encoded_vtime, sizeof(encoded_vtime));

  if (_msg_session->explicit) {
    rfc5444_writer_add_messagetlv(&_dlep_writer, DLEP_TLV_UNICAST, 0, NULL, 0);
  }

  if (_client_config.peer_type[0]) {
    rfc5444_writer_add_messagetlv(&_dlep_writer, DLEP_TLV_PEER_TYPE, 0,
        _client_config.peer_type, strlen(_client_config.peer_type));
  }
}

/**
 * Add message header to outgoing DLEP messages
 * @param writer
 * @param msg
 */
static void
_cb_addMessageHeader(struct rfc5444_writer *writer, struct rfc5444_writer_message *msg) {
  rfc5444_writer_set_msg_header(writer, msg, false, false, false, true);
  rfc5444_writer_set_msg_seqno(writer, msg, _msg_session->seqno++);
}

/**
 * Callback for adding message TLVs to outgoing DLEP messages
 * @param writer
 * @param prv
 */
static void
_cb_addMessageTLVs(struct rfc5444_writer *writer,
    struct rfc5444_writer_content_provider *prv __attribute__((unused))) {

  rfc5444_writer_add_messagetlv(writer,
      DLEP_TLV_ORDER, _msg_order, NULL, 0);

  switch (_msg_order) {
    case DLEP_ORDER_CONNECT_ROUTER:
      _add_connectrouter_msgtlvs();
      break;
    default:
      OLSR_WARN(LOG_DLEP_CLIENT, "DLEP Message order %d not implemented yet", _msg_order);
      break;
  }
}

/**
 * Callback for periodic connect router message generation
 * @param ptr
 */
static void
_cb_router_connect(void *ptr __attribute__((unused))) {
  _msg_order = DLEP_ORDER_CONNECT_ROUTER;

  avl_for_each_element(&_service_tree, _msg_session, _node) {
    _msg_explicit_connectto = false;
    rfc5444_writer_create_message_singleif(&_dlep_writer,
        DLEP_MESSAGE_ID, &_msg_session->out_if);
    rfc5444_writer_flush(&_dlep_writer, &_msg_session->out_if, false);
  }
}
