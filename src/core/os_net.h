
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

#ifndef OS_NET_H_
#define OS_NET_H_

#include <unistd.h>
#include <sys/select.h>

#include "common/common_types.h"
#include "common/netaddr.h"
#include "olsr_logging.h"
#include "olsr_interface.h"

/*
 * Set one of the following defines in the os specific os_net includes
 * to OS_SPECIFIC to define that the os code is implementing the function
 * itself and does not use the generic function
 * Set it to OS_GENERIC to define that the code use the default implementation.
 *
 * Example from os_net_linux.h:
 *
 * #define OS_NET_GETSOCKET    OS_GENERIC
 * #define OS_NET_CONFIGSOCKET OS_GENERIC
 * #define OS_NET_JOINMCAST    OS_GENERIC
 * #define OS_NET_SETNONBLOCK  OS_GENERIC
 * #define OS_NET_CLOSE        OS_GENERIC
 * #define OS_NET_SELECT       OS_GENERIC
 * #define OS_NET_RECVFROM     OS_GENERIC
 * #define OS_NET SENDTO       OS_GENERIC
 */

/* set the guard macro so we can include the os specific settings */
#define OS_NET_SPECIFIC_INCLUDE
#include "os_helper.h"

#ifdef OS_LINUX
#include "os_linux/os_net_linux.h"
#endif

#ifdef OS_BSD
#include "os_bsd/os_net_bsd.h"
#endif

#ifdef OS_WIN32
#include "os_win32/os_net_win32.h"
#endif

#undef OS_NET_SPECIFIC_INCLUDE

/* binary flags for os_net_getsocket */
enum olsr_socket_opt {
  OS_SOCKET_UDP = 0,
  OS_SOCKET_TCP = 1,
  OS_SOCKET_BLOCKING = 2,
  OS_SOCKET_MULTICAST = 4,
};

/* prototypes for all os_net functions */
EXPORT int os_net_init(void) __attribute__((warn_unused_result));
EXPORT void os_net_cleanup(void);
EXPORT int os_net_getsocket(union netaddr_socket *bindto,
    enum olsr_socket_opt flags, int recvbuf, enum log_source log_src);
EXPORT int os_net_configsocket(int sock, union netaddr_socket *bindto,
    enum olsr_socket_opt flags, int recvbuf, enum log_source log_src);
EXPORT int net_os_join_mcast(int sock, union netaddr_socket *multicast,
    struct olsr_interface *oif, enum log_source log_src);
EXPORT int os_net_set_nonblocking(int sock);
EXPORT int os_net_update_interface(struct olsr_interface_data *, const char *);
EXPORT int os_recvfrom(
    int fd, void *buf, size_t length, union netaddr_socket *source);
EXPORT int os_sendto(
    int fd, const void *buf, size_t length, union netaddr_socket *dst);
static INLINE int os_close(int fd);
static INLINE int os_select(
    int num, fd_set *r,fd_set *w,fd_set *e, struct timeval *timeout);

/*
 * INLINE implementations for generic os_net functions
 */

#if OS_NET_CLOSE == OS_GENERIC
/**
 * Close a file descriptor
 * @param fd filedescriptor
 */
static INLINE int
os_close(int fd) {
  return close(fd);
}
#endif

#if OS_NET_SELECT == OS_GENERIC
/**
 * polls a number of sockets for network events. If no even happens or
 * already has happened, function will return after timeout time.
 * see 'man select' for more details
 * @param num
 * @param r
 * @param w
 * @param e
 * @param timeout
 * @return
 */
static INLINE int
os_select(int num, fd_set *r,fd_set *w,fd_set *e, struct timeval *timeout) {
  return select(num, r, w, e, timeout);
}
#endif

#endif /* OS_NET_H_ */
