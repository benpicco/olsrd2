
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

#ifndef DLEP_IANA_H_
#define DLEP_IANA_H_

enum { DLEP_MESSAGE_ID = 42 };

enum dlep_orders {
  DLEP_ORDER_INTERFACE_DISCOVERY = 1,
  DLEP_ORDER_CONNECT_ROUTER      = 2,
  DLEP_ORDER_NEIGHBOR_UPDATE     = 3,
};

/* DLEP TLV types */
enum dlep_msgtlv_types {
  DLEP_TLV_ORDER           = 192,
  DLEP_TLV_PEER_TYPE       = 193,
  DLEP_TLV_UNICAST         = 194,

  DLEP_TLV_SSID            = 195,
  DLEP_TLV_LAST_SEEN       = 196,
  DLEP_TLV_FREQUENCY       = 197,
  DLEP_TLV_SUPPORTED_RATES = 198,
};

enum dlep_addrtlv_types {
  DLEP_ADDRTLV_SIGNAL     = 192,
  DLEP_ADDRTLV_LAST_SEEN  = 193,
  DLEP_ADDRTLV_RX_BITRATE = 194,
  DLEP_ADDRTLV_RX_BYTES   = 195,
  DLEP_ADDRTLV_RX_PACKETS = 196,
  DLEP_ADDRTLV_TX_BITRATE = 197,
  DLEP_ADDRTLV_TX_BYTES   = 198,
  DLEP_ADDRTLV_TX_PACKETS = 199,
  DLEP_ADDRTLV_TX_RETRIES = 200,
  DLEP_ADDRTLV_TX_FAILED  = 201,
};

#endif /* DLEP_IANA_H_ */
