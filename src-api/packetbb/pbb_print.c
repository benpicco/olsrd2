
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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "packetbb/pbb_reader.h"
#include "packetbb/pbb_print.h"

static void _print_hexline(struct autobuf *out, void *buffer, size_t length);

static enum pbb_result _cb_print_pkt_start(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_print_pkt_tlv(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_entry *tlv, struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_print_pkt_end(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context, bool);
static enum pbb_result _cb_print_msg_start(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_print_msg_tlv(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_entry *tlv, struct pbb_reader_tlvblock_context *context) ;
static enum pbb_result _cb_print_msg_end(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context, bool);
static enum pbb_result _cb_print_addr_start(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_print_addr_tlv(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_entry *tlv, struct pbb_reader_tlvblock_context *context);
static enum pbb_result _cb_print_addr_end(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context, bool);

/**
 * Add a printer for a packetbb reader
 * @param session pointer to initialized pbb printer session
 * @param reader pointer to initialized reader
 */
void
pbb_print_add(struct pbb_print_session *session,
    struct pbb_reader *reader) {
  /* memorize reader */
  session->_reader = reader;

  pbb_reader_add_packet_consumer(reader, &session->_pkt, NULL, 0, 0);
  session->_pkt.start_callback = _cb_print_pkt_start;
  session->_pkt.tlv_callback = _cb_print_pkt_tlv;
  session->_pkt.end_callback = _cb_print_pkt_end;

  pbb_reader_add_defaultmsg_consumer(reader, &session->_msg, NULL, 0, 0);
  session->_msg.start_callback = _cb_print_msg_start;
  session->_msg.tlv_callback = _cb_print_msg_tlv;
  session->_msg.end_callback = _cb_print_msg_end;

  pbb_reader_add_defaultaddress_consumer(reader, &session->_addr, NULL, 0, 0);
  session->_addr.start_callback = _cb_print_addr_start;
  session->_addr.tlv_callback = _cb_print_addr_tlv;
  session->_addr.end_callback = _cb_print_addr_end;
}

/**
 * Remove printer from packetbb reader
 * @param session pointer to initialized pbb printer session
 */
void
pbb_print_remove(struct pbb_print_session *session) {
  pbb_reader_remove_message_consumer(session->_reader, &session->_addr);
  pbb_reader_remove_message_consumer(session->_reader, &session->_msg);
  pbb_reader_remove_packet_consumer(session->_reader, &session->_pkt);
}

/**
 * This function converts a packetbb buffer into a human readable
 * form and print it into an buffer. To do this it allocates its own
 * packetbb reader, hooks in the printer macros, parse the packet and
 * cleanes up the reader again.
 *
 * @param out pointer to output buffer
 * @param buffer pointer to packet to be printed
 * @param length length of packet in bytes
 * @return return code of reader, see pbb_result enum
 */
enum pbb_result
pbb_print_direct(struct autobuf *out, void *buffer, size_t length) {
  struct pbb_reader reader;
  struct pbb_print_session session;
  enum pbb_result result;

  memset(&reader, 0, sizeof(reader));
  memset(&session, 0, sizeof(session));

  session.output = out;

  pbb_reader_init(&reader);
  pbb_print_add(&session, &reader);

  result = pbb_reader_handle_packet(&reader, buffer, length);

  pbb_print_remove(&session);
  pbb_reader_cleanup(&reader);

  return result;
}

/**
 * Print a hexdump of a buffer to an autobuf and prepends a prefix string
 * to each line.
 * @param out output buffer
 * @param prefix string to prepend to each line
 * @param buffer buffer to be hexdumped
 * @param length length of buffer in bytes
 */
void
pbb_print_hexdump(struct autobuf *out, const char *prefix, void *buffer, size_t length) {
  uint8_t *buf;
  size_t j, l;

  buf = buffer;

  for (j = 0; j < length; j += 32) {
    abuf_appendf(out, "%s%04zx:", prefix, j);

    l = length - j;
    if (l > 32) {
      l = 32;
    }
    _print_hexline(out, &buf[j], l);
    abuf_puts(out, "\n");
  }
}

/**
 * Print a line for a hexdump
 * @param out output buffer
 * @param buffer buffer to be hexdumped
 * @param length length of buffer in bytes
 */
static void
_print_hexline(struct autobuf *out, void *buffer, size_t length) {
  size_t i;
  uint8_t *buf = buffer;

  for (i = 0; i < length; i++) {
    abuf_appendf(out, "%s%02x", ((i & 3) == 0) ? " " : "", (int) (buf[i]));
  }
}

/**
 * Clear output buffer and print start of packet
 * @param c
 * @param context
 * @return
 */
static enum pbb_result
_cb_print_pkt_start(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context) {
  struct pbb_print_session *session;

  assert (context->type == PBB_CONTEXT_PACKET);

  session = container_of(c, struct pbb_print_session, _pkt);

  /* clear output buffer */
  abuf_clear(session->output);

  abuf_puts(session->output, "\t,------------------\n");
  abuf_puts(session->output, "\t|  PACKET\n");
  abuf_puts(session->output, "\t|------------------\n");
  abuf_appendf(session->output, "\t| * Packet version:    %d\n", context->pkt_version);
  abuf_appendf(session->output, "\t| * Packet flags:      %d\n", context->pkt_flags);
  if (context->has_pktseqno) {
    abuf_appendf(session->output, "\t| * Packet seq number: %d\n", context->pkt_seqno);
  }

  return PBB_OKAY;
}

/**
 * Print packet TLVs
 * @param c
 * @param tlv
 * @param context
 * @return
 */
enum pbb_result
_cb_print_pkt_tlv(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_entry *tlv,
    struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  struct pbb_print_session *session;

  assert (context->type == PBB_CONTEXT_PACKET);

  session = container_of(c, struct pbb_print_session, _pkt);

  abuf_puts(session->output, "\t|    | - TLV\n");
  abuf_appendf(session->output, "\t|    |     Flags = %d\n", tlv->flags);
  abuf_appendf(session->output, "\t|    |     Type = %d", tlv->type);
  if (tlv->type_ext != 0) {
    abuf_appendf(session->output, "; Type ext. = %d", tlv->type_ext);
  }
  abuf_puts(session->output, "\n");
  if (tlv->length > 0) {
    abuf_appendf(session->output, "\t|    |     Value length: %d\n", tlv->length);
    pbb_print_hexdump(session->output, "\t|    |       ", tlv->single_value, tlv->length);
  }
  return PBB_OKAY;
}

/**
 * Print end of packet and call print callback if necessary
 * @param c
 * @param context
 * @param dropped
 * @return
 */
enum pbb_result
_cb_print_pkt_end(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context __attribute__ ((unused)),
    bool dropped __attribute__ ((unused))) {
  struct pbb_print_session *session;
  session = container_of(c, struct pbb_print_session, _pkt);

  abuf_puts(session->output, "\t`------------------\n");

  if (session->print_packet) {
    session->print_packet(session);
  }
  return PBB_OKAY;
}

/**
 * Print start of message
 * @param c
 * @param context
 * @return
 */
enum pbb_result
_cb_print_msg_start(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  struct pbb_print_session *session;

  assert (context->type == PBB_CONTEXT_MESSAGE);

  session = container_of(c, struct pbb_print_session, _msg);

  abuf_puts(session->output, "\t|    ,-------------------\n");
  abuf_puts(session->output, "\t|    |  MESSAGE\n");
  abuf_puts(session->output, "\t|    |-------------------\n");
  abuf_appendf(session->output, "\t|    | * Message type:       %d\n", context->msg_type);
  abuf_appendf(session->output, "\t|    | * Message flags:      %d\n", context->msg_flags);
  abuf_appendf(session->output, "\t|    | * Address length:     %d\n", context->addr_len);

  if (context->has_origaddr) {
    char buffer[100];

    abuf_puts(session->output, "\t|    | * Originator address: ");
    if (context->addr_len == 4) {
      inet_ntop(AF_INET, context->orig_addr, buffer, sizeof(buffer));
      abuf_appendf(session->output, "%s/32", buffer);
    } else if (context->addr_len == 16) {
      inet_ntop(AF_INET6, context->orig_addr, buffer, sizeof(buffer));
      abuf_appendf(session->output, "%s/128", buffer);
    } else {
      _print_hexline(session->output, context->orig_addr, context->addr_len);
    }
    abuf_puts(session->output, "\n");
  }
  if (context->has_hoplimit) {
    abuf_appendf(session->output, "\t|    | * Hop limit:          %d\n", context->hoplimit);
  }
  if (context->has_hopcount) {
    abuf_appendf(session->output, "\t|    | * Hop count:          %d\n", context->hopcount);
  }
  if (context->has_seqno) {
    abuf_appendf(session->output, "\t|    | * Message seq number: %d\n", context->seqno);
  }

  return PBB_OKAY;
}

/**
 * Print message TLV
 * @param c
 * @param tlv
 * @param context
 * @return
 */
enum pbb_result
_cb_print_msg_tlv(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_entry *tlv,
    struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  struct pbb_print_session *session;

  assert (context->type == PBB_CONTEXT_MESSAGE);

  session = container_of(c, struct pbb_print_session, _msg);

  abuf_puts(session->output, "\t|    |    | - TLV\n");
  abuf_appendf(session->output, "\t|    |    |     Flags = %d\n", tlv->flags);
  abuf_appendf(session->output, "\t|    |    |     Type = %d", tlv->type);
  if (tlv->type_ext != 0) {
    abuf_appendf(session->output, "; Type ext. = %d", tlv->type_ext);
  }
  abuf_puts(session->output, "\n");
  if (tlv->length > 0) {
    abuf_appendf(session->output, "\t|    |    |     Value length: %d\n", tlv->length);
    pbb_print_hexdump(session->output, "\t|    |    |       ", tlv->single_value, tlv->length);
  }
  return PBB_OKAY;
}

/**
 * Print end of message
 * @param c
 * @param context
 * @param dropped
 * @return
 */
enum pbb_result
_cb_print_msg_end(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context __attribute__((unused)),
    bool dropped __attribute__ ((unused))) {
  struct pbb_print_session *session;

  assert (context->type == PBB_CONTEXT_MESSAGE);

  session = container_of(c, struct pbb_print_session, _msg);

  abuf_puts(session->output, "\t|    `-------------------\n");
  return PBB_OKAY;
}

/**
 * Print start of address
 * @param c
 * @param context
 * @return
 */
enum pbb_result
_cb_print_addr_start(struct pbb_reader_tlvblock_consumer *c,
    struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  char buffer[100];
  struct pbb_print_session *session;

  assert (context->type == PBB_CONTEXT_ADDRESS);

  session = container_of(c, struct pbb_print_session, _addr);

  abuf_puts(session->output, "\t|    |    ,-------------------\n");
  abuf_puts(session->output, "\t|    |    |  Address: ");
  if (context->addr_len == 4) {
    inet_ntop(AF_INET, context->addr, buffer, sizeof(buffer));
    abuf_puts(session->output, buffer);
  } else if (context->addr_len == 16) {
    inet_ntop(AF_INET6, context->addr, buffer, sizeof(buffer));
    abuf_puts(session->output, buffer);
  } else {
    _print_hexline(session->output, context->addr, context->addr_len);
  }
  abuf_appendf(session->output, "/%d\n", context->prefixlen);
  return PBB_OKAY;
}

/**
 * Print address tlv
 * @param c
 * @param tlv
 * @param context
 * @return
 */
enum pbb_result
_cb_print_addr_tlv(struct pbb_reader_tlvblock_consumer *c __attribute__ ((unused)),
    struct pbb_reader_tlvblock_entry *tlv,
    struct pbb_reader_tlvblock_context *context __attribute__((unused))) {
  struct pbb_print_session *session;

  assert (context->type == PBB_CONTEXT_ADDRESS);

  session = container_of(c, struct pbb_print_session, _addr);

  abuf_puts(session->output, "\t|    |    |    | - TLV\n");
  abuf_appendf(session->output, "\t|    |    |    |     Flags = %d\n", tlv->flags);
  abuf_appendf(session->output, "\t|    |    |    |     Type = %d", tlv->type);
  if (tlv->type_ext != 0) {
    abuf_appendf(session->output, "; Type ext. = %d", tlv->type_ext);
  }
  abuf_puts(session->output, "\n");
  if (tlv->length > 0) {
    abuf_appendf(session->output, "\t|    |    |    |     Value length: %d\n", tlv->length);
    pbb_print_hexdump(session->output, "\t|    |    |    |       ", tlv->single_value, tlv->length);
  }
  return PBB_OKAY;
}

/**
 * Print end of address
 * @param c
 * @param context
 * @param dropped
 * @return
 */
enum pbb_result
_cb_print_addr_end(struct pbb_reader_tlvblock_consumer *c __attribute__ ((unused)),
    struct pbb_reader_tlvblock_context *context __attribute__((unused)),
    bool dropped __attribute__ ((unused))) {
  struct pbb_print_session *session;

  assert (context->type == PBB_CONTEXT_ADDRESS);

  session = container_of(c, struct pbb_print_session, _addr);

  abuf_puts(session->output, "\t|    |    `-------------------\n");
  return PBB_OKAY;
}
