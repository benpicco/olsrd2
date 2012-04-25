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

#include "common/common_types.h"
#include "common/netaddr.h"

#include "packetbb/pbb_iana.h"

const struct netaddr PBB_MANET_MULTICAST_V4 = {
  .addr = { 224,0,0,109, 0,0,0,0,0,0,0,0,0,0,0,0 },
  .type = AF_INET,
  .prefix_len = 0,
};

const struct netaddr PBB_MANET_MULTICAST_V6 = {
  .addr = { 0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0,0x6D },
  .type = AF_INET6,
  .prefix_len = 0,
};

const char *PBB_MANET_IPPROTO_TXT = "138";
const char *PBB_MANET_UDP_PORT_TXT = "269";
const char *PBB_MANET_MULTICAST_V4_TXT = "224.0.0.109";
const char *PBB_MANET_MULTICAST_V6_TXT = "ff02::6d";
