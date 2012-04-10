
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

#ifndef NETADDR_H_
#define NETADDR_H_


#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <assert.h>
#include <string.h>

#include "common/common_types.h"
#include "common/autobuf.h"


#define AF_MAC48 ((AF_MAX) + 1)
#define AF_EUI64 ((AF_MAX) + 2)

/**
 * Representation of an address including address type
 * At the moment we support AF_INET, AF_INET6 and AF_MAC48
 */
struct netaddr {
  /* 16 bytes of memory for address */
  uint8_t addr[16];

  /* address type */
  uint8_t type;

  /* address prefix length */
  uint8_t prefix_len;
};

/**
 * Representation of a sockaddr object. Allows access
 * to all variants without casting and compiler warnings.
 */
union netaddr_socket {
  struct sockaddr_in v4;
  struct sockaddr_in6 v6;
  struct sockaddr std;
  struct sockaddr_storage storage;
};

/**
 * Buffer for writing string representation of netaddr
 * and netaddr_socket objects
 */
struct netaddr_str {
  char buf[INET6_ADDRSTRLEN+16];
};

EXPORT extern const struct netaddr NETADDR_IPV4_ANY;
EXPORT extern const struct netaddr NETADDR_IPV6_ANY;

EXPORT int netaddr_from_binary(struct netaddr *dst, const void *binary, size_t len, uint8_t addr_type);
EXPORT int netaddr_to_binary(void *dst, const struct netaddr *src, size_t len);
EXPORT int netaddr_from_socket(struct netaddr *dst, const union netaddr_socket *src);
EXPORT int netaddr_to_socket(union netaddr_socket *dst, const struct netaddr *src);
EXPORT int netaddr_to_autobuf(struct autobuf *, const struct netaddr *src);
EXPORT int netaddr_create_host_bin(struct netaddr *host, const struct netaddr *netmask,
    const void *number, size_t num_length);
EXPORT int netaddr_socket_init(union netaddr_socket *combined,
    const struct netaddr *addr, uint16_t port);
EXPORT uint16_t netaddr_socket_get_port(const union netaddr_socket *sock);

EXPORT const char *netaddr_to_prefixstring(
    struct netaddr_str *dst, const struct netaddr *src, bool forceprefix);
EXPORT int netaddr_from_string(struct netaddr *, const char *) __attribute__((warn_unused_result));
EXPORT const char *netaddr_socket_to_string(struct netaddr_str *, const union netaddr_socket *);

EXPORT int netaddr_cmp_to_socket(const struct netaddr *, const union netaddr_socket *);
EXPORT bool netaddr_isequal_binary(const struct netaddr *addr,
    const void *bin, size_t len, uint16_t af, uint8_t prefix_len);
EXPORT bool netaddr_is_in_subnet(const struct netaddr *subnet, const struct netaddr *addr);
EXPORT bool netaddr_binary_is_in_subnet(const struct netaddr *subnet,
    const void *bin, size_t len, uint8_t af_family);

EXPORT uint8_t netaddr_get_maxprefix(const struct netaddr *);

EXPORT int netaddr_avlcmp(const void *, const void *, void *);
EXPORT int netaddr_socket_avlcmp(const void *, const void *, void *);

#ifdef WIN32
EXPORT const char *inet_ntop(int af, const void* src, char* dst, int cnt);
EXPORT int inet_pton(int af, const char *cp, void * buf);
#endif

/**
 * Converts a netaddr object into a string.
 * Prefix will be added if necessary.
 * @param dst target buffer
 * @param src netaddr source
 * @return pointer to target buffer, NULL if an error happened
 */
static INLINE const char *
netaddr_to_string(struct netaddr_str *dst, const struct netaddr *src) {
  return netaddr_to_prefixstring(dst, src, false);
}

/**
 * Creates a host address from a netmask and a host number part. This function
 * will copy the netmask and then overwrite the bits after the prefix length
 * with the one from the host number.
 * @param host target buffer
 * @param netmask prefix of result
 * @param host_number postfix of result
 * @return -1 if an error happened, 0 otherwise
 */
static INLINE int
netaddr_create_host(struct netaddr *host, const struct netaddr *netmask,
    const struct netaddr *host_number) {
  return netaddr_create_host_bin(host, netmask, host_number->addr,
      netaddr_get_maxprefix(host_number));
}

/**
 * Compares two addresses.
 * Address type will be compared last.
 * @param a1 address 1
 * @param a2 address 2
 * @return >0 if a1>a2, <0 if a1<a2, 0 otherwise
 */
static INLINE int
netaddr_cmp(const struct netaddr *a1, const struct netaddr *a2) {
  return memcmp(a1, a2, sizeof(*a1));
}

/**
 * Compares two sockets.
 * @param a1 address 1
 * @param a2 address 2
 * @return >0 if a1>a2, <0 if a1<a2, 0 otherwise
 */
static INLINE int
netaddr_socket_cmp(const union netaddr_socket *s1, const union netaddr_socket *s2) {
  return memcmp(s1, s2, sizeof(*s1));
}

#endif /* NETADDR_H_ */
