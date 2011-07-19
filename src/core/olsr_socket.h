
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

#ifndef _OLSR_SCHEDULER
#define _OLSR_SCHEDULER

#include "common/common_types.h"
#include "common/list.h"
#include "common/avl.h"

/* flags for socket handler */
enum olsr_sockethandler_flags {
  OLSR_SOCKET_READ = 0x04,
  OLSR_SOCKET_WRITE = 0x08,
};

/* prototype for socket handler */
typedef void (*socket_handler_func) (int fd, void *data,
    enum olsr_sockethandler_flags);

/* This struct represents a single registered socket handler */
struct olsr_socket_entry {
  /* list of socket handlers */
  struct list_entity node;

  /* file descriptor of the socket */
  int fd;

  /* socket handler */
  socket_handler_func process;

  /* custom data pointer for sockets */
  void *data;

  /* flags (OLSR_SOCKET_READ and OLSR_SOCKET_WRITE) */
  enum olsr_sockethandler_flags flags;
};

/* deletion safe macro for socket list traversal */
EXPORT extern struct list_entity socket_head;
#define OLSR_FOR_ALL_SOCKETS(socket, iterator) list_for_each_element_safe(&socket_head, socket, node, iterator)

int olsr_socket_init(void) __attribute__((warn_unused_result));
void olsr_socket_cleanup(void);
int olsr_socket_handle(uint32_t until_time) __attribute__((warn_unused_result));


EXPORT struct olsr_socket_entry *olsr_socket_add(int fd,
    socket_handler_func pf_imm, void *data, unsigned int flags);
EXPORT void olsr_socket_remove(struct olsr_socket_entry *);

/**
 * Enable one or both flags of a socket handler
 * @param sock pointer to socket entry
 */
static inline void
olsr_socket_enable(struct olsr_socket_entry *entry, unsigned int flags)
{
  entry->flags |= flags;
}

/**
 * Disable one or both flags of a socket handler
 * @param sock pointer to socket entry
 */
static inline void
olsr_socket_disable(struct olsr_socket_entry *entry, unsigned int flags)
{
  entry->flags &= ~flags;
}

#endif

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
