
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

#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "common/avl.h"
#include "common/avl_comp.h"
#include "olsr_clock.h"
#include "olsr_logging.h"
#include "olsr_memcookie.h"
#include "os_net.h"
#include "olsr_socket.h"
#include "olsr.h"

/* List of all active sockets in scheduler */
struct list_entity socket_head;

static struct olsr_memcookie_info *socket_memcookie;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_socket_state);

/* helper function to free socket entry */
static inline void
olsr_socket_intfree(struct olsr_socket_entry *sock) {
  list_remove(&sock->node);
  olsr_memcookie_free(socket_memcookie, sock);
}

/**
 * Initialize olsr socket scheduler
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_socket_init(void) {
  if (olsr_subsystem_init(&olsr_socket_state))
    return 0;

  list_init_head(&socket_head);

  socket_memcookie = olsr_memcookie_add("socket entry", sizeof(struct olsr_socket_entry));
  if (socket_memcookie == NULL) {
    OLSR_WARN_OOM(LOG_SOCKET);
    olsr_socket_state--;
    return -1;
  }
  return 0;
}

/**
 * Cleanup olsr socket scheduler.
 * This will close and free all sockets.
 */
void
olsr_socket_cleanup(void)
{
  struct olsr_socket_entry *entry, *iterator;

  if (olsr_subsystem_cleanup(&olsr_socket_state))
    return;

  OLSR_FOR_ALL_SOCKETS(entry, iterator) {
    os_close(entry->fd);
    olsr_socket_intfree(entry);
  }

  olsr_memcookie_remove(socket_memcookie);
}

// TODO: remove malloc by giving function initialized socket entry
/**
 * Add a socket handler to the list of sockets
 * being used in the main select(2) loop
 *
 * @param fd file descriptor for socket
 * @param pf_imm processing callback
 * @param data custom data
 * @param event_read true if socket is waiting for read-event
 * @param event_write true if socket is waiting for write-event
 * @return pointer to socket_entry
 */
struct olsr_socket_entry *
olsr_socket_add(int fd, socket_handler_func pf_imm, void *data,
    bool event_read, bool event_write)
{
  struct olsr_socket_entry *new_entry;

  assert (fd);
  assert (pf_imm);

  OLSR_DEBUG(LOG_SOCKET, "Adding socket entry %d to scheduler\n", fd);

  new_entry = olsr_memcookie_malloc(socket_memcookie);
  if (new_entry) {
    new_entry->fd = fd;
    new_entry->process = pf_imm;
    new_entry->data = data;
    new_entry->event_read = event_read;
    new_entry->event_write = event_write;

    /* Queue */
    list_add_before(&socket_head, &new_entry->node);
  }

  return new_entry;
}

/**
 * Mark a socket and handler for removal from the socket scheduler
 * @param entry pointer to socket entry
 */
void
olsr_socket_remove(struct olsr_socket_entry *entry)
{
  OLSR_DEBUG(LOG_SOCKET, "Trigger removing socket entry %d\n", entry->fd);

  entry->process = NULL;
  entry->event_read = false;
  entry->event_write = false;
}

/**
 * Handle all incoming socket events until a certain time
 * @param until_time timestamp when the function should return
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_socket_handle(uint32_t until_time)
{
  struct olsr_socket_entry *entry, *iterator;
  struct timeval tvp;
  int32_t remaining;
  int n = 0;
  bool fd_read;
  bool fd_write;

  /* Update time since this is much used by the parsing functions */
  if (olsr_clock_update()) {
    return -1;
  }

  remaining = olsr_clock_getRelative(until_time);
  if (remaining <= 0) {
    /* we are already over the interval */
    if (list_is_empty(&socket_head)) {
      /* If there are no registered sockets we do not call select(2) */
      return 0;
    }
    tvp.tv_sec = 0;
    tvp.tv_usec = 0;
  } else {
    /* we need an absolute time - milliseconds */
    tvp.tv_sec = remaining / MSEC_PER_SEC;
    tvp.tv_usec = (remaining % MSEC_PER_SEC) * USEC_PER_MSEC;
  }

  /* do at least one select */
  for (;;) {
    fd_set ibits, obits;
    int hfd = 0;

    fd_read = false;
    fd_write = false;

    FD_ZERO(&ibits);
    FD_ZERO(&obits);

    /* Adding file-descriptors to FD set */
    OLSR_FOR_ALL_SOCKETS(entry, iterator) {
      if (entry->process == NULL) {
        continue;
      }
      if (entry->event_read) {
        fd_read = true;
        FD_SET((unsigned int)entry->fd, &ibits);        /* And we cast here since we get a warning on Win32 */
      }
      if (entry->event_write) {
        fd_write = true;
        FD_SET((unsigned int)entry->fd, &obits);        /* And we cast here since we get a warning on Win32 */
      }
      if ((entry->event_read || entry->event_write) != 0 && entry->fd >= hfd) {
        hfd = entry->fd + 1;
      }
    }

    if (hfd == 0 && (long)remaining <= 0) {
      /* we are over the interval and we have no fd's. Skip the select() etc. */
      return 0;
    }

    do {
      n = os_select(hfd,
          fd_read ? &ibits : NULL,
          fd_write ? &obits : NULL,
          NULL, &tvp);
    } while (n == -1 && errno == EINTR);

    if (n == 0) {               /* timeout! */
      break;
    }
    if (n < 0) {              /* Did something go wrong? */
      OLSR_WARN(LOG_SOCKET, "select error: %s (%d)", strerror(errno), errno);
      break;
    }

    /* Update time since this is much used by the parsing functions */
    if (olsr_clock_update()) {
      n = -1;
      break;
    }
    OLSR_FOR_ALL_SOCKETS(entry, iterator) {
      if (entry->process == NULL) {
        continue;
      }

      fd_read = FD_ISSET(entry->fd, &ibits) != 0;
      fd_write = FD_ISSET(entry->fd, &obits) != 0;
      if (fd_read || fd_write) {
        entry->process(entry->fd, entry->data, fd_read, fd_write);
      }
    }

    /* calculate the next timeout */
    remaining = olsr_clock_getRelative(until_time);
    if (remaining <= 0) {
      /* we are already over the interval */
      break;
    }
    /* we need an absolute time - milliseconds */
    tvp.tv_sec = remaining / MSEC_PER_SEC;
    tvp.tv_usec = (remaining % MSEC_PER_SEC) * USEC_PER_MSEC;
  }

  OLSR_FOR_ALL_SOCKETS(entry, iterator) {
    if (entry->process == NULL) {
      olsr_socket_intfree(entry);
    }
  }

  if (n<0)
    return -1;
  return 0;
}
