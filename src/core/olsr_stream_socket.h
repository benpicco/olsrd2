
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2011, the olsr.org team - see HISTORY file
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

#ifndef OLSR_STREAM_SOCKET_H_
#define OLSR_STREAM_SOCKET_H_

#include "common/common_types.h"
#include "common/autobuf.h"
#include "common/list.h"
#include "common/netaddr.h"

#include "olsr_memcookie.h"

enum olsr_stream_session_state {
  COMPORT_SESSION_ACTIVE,
  COMPORT_SESSION_SEND_AND_QUIT,
  COMPORT_SESSION_CLEANUP
};

enum olsr_stream_errors {
  REQUEST_TOO_LARGE = 413,
  SERVICE_UNAVAILABLE = 503
};

struct olsr_stream_session {
  /*
   * public part of the session data
   *
   * variables marked RW might be written from txt commands, those with
   * an "R" mark are read only
   */

  /* ip addr of peer (R) */
  union netaddr_socket peer_addr;

  /* output buffer, anything inside will be written to the peer as
   * soon as possible */
  struct autobuf out;

  /*
   * internal part of the server
   */
  struct list_entity node;
  struct olsr_stream_socket *comport;

  struct olsr_socket_entry *scheduler_entry;

  struct olsr_timer_entry *timeout;
  struct autobuf in;
  bool send_first, wait_for_connect;

  enum olsr_stream_session_state state;
};

struct olsr_stream_socket {
  struct list_entity node;

  union netaddr_socket local_socket;

  struct list_entity session;
  int allowes_sessions;

  struct olsr_memcookie_info *memcookie;
  uint32_t session_timeout;
  size_t maximum_input_buffer;
  bool send_first;

  /* NULL for outgoing streams */
  struct olsr_socket_entry *scheduler_entry;

  void (*init)(struct olsr_stream_session *);
  void (*cleanup)(struct olsr_stream_session *);

  void (*create_error)(struct olsr_stream_session *, enum olsr_stream_errors);
  enum olsr_stream_session_state (*parse_data)(struct olsr_stream_session *);
};

int olsr_stream_init(void) __attribute__((warn_unused_result));
void olsr_stream_cleanup(void);

int olsr_stream_add(struct olsr_stream_socket *,
    union netaddr_socket *local);
void olsr_stream_remove(struct olsr_stream_socket *);
struct olsr_stream_session *olsr_stream_connect_to(
    struct olsr_stream_socket *, union netaddr_socket *remote);
void olsr_stream_flush(struct olsr_stream_session *con);

#endif /* OLSR_STREAM_SOCKET_H_ */
