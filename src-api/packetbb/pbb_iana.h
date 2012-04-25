/*
 * PacketBB handler library (see RFC 5444)
 * Copyright (c) 2010 Henning Rogge <hrogge@googlemail.com>
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
 * Visit http://www.olsr.org/git for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 */

#ifndef PBB_IANA_H_
#define PBB_IANA_H_

#include "common/common_types.h"
#include "common/netaddr.h"

/*
 * IANA registered IP/UDP-port number
 * and multicast groups for MANET (RFC 5498)
 */

enum pbb_iana {
  PBB_MANET_IPPROTO = 138,
  PBB_MANET_UDP_PORT = 269,
};

EXPORT extern const struct netaddr PBB_MANET_MULTICAST_V4;
EXPORT extern const struct netaddr PBB_MANET_MULTICAST_V6;

/*
 * text variants of the constants above for defaults in
 * configuration sections
 */
EXPORT extern const char *PBB_MANET_IPPROTO_TXT;
EXPORT extern const char *PBB_MANET_UDP_PORT_TXT;
EXPORT extern const char *PBB_MANET_MULTICAST_V4_TXT;
EXPORT extern const char *PBB_MANET_MULTICAST_V6_TXT;

/*
 * this is a list of all globally defined IANA
 * message TLVs and their allocated values
 */

enum pbb_msgtlvs_iana {
  /* RFC 5497 */
  PBB_MSGTLV_VALIDITY_TIME = 0,
  PBB_MSGTLV_INTERVAL_TIME = 1,
};

/*
 * this is a list of all globally defined IANA
 * address TLVs and their allocated values
 */

enum pbb_addrtlv_iana {
  /* RFC 5497 (timetlv) */
  PBB_ADDRTLV_VALIDITY_TIME = 0,
  PBB_ADDRTLV_INTERVAL_TIME = 1,

  /* RFC 6130 (NHDP) */
  PBB_ADDRTLV_LOCAL_IF      = 2,
  PBB_ADDRTLV_LINK_STATUS   = 3,
  PBB_ADDRTLV_OTHER_NEIGHB  = 4,
};

/* values for LOCAL_IF address TLV */
static const uint8_t PBB_LOCALIF_THIS_IF       = 0;
static const uint8_t PBB_LOCALIF_OTHER_IF      = 1;

/* values for LINK_STATUS address TLV */
static const uint8_t PBB_LINKSTATUS_LOST       = 0;
static const uint8_t PBB_LINKSTATUS_SYMMETRIC  = 1;
static const uint8_t PBB_LINKSTATUS_HEARD      = 2;

/* values for OTHER_NEIGHB address TLV */
static const uint8_t PBB_OTHERNEIGHB_LOST      = 0;
static const uint8_t PBB_OTHERNEIGHB_SYMMETRIC = 1;

#endif /* PBB_IANA_H_ */
