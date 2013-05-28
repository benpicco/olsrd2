
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2013, the olsr.org team - see HISTORY file
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

#ifndef NHDP_H_
#define NHDP_H_

#include "common/common_types.h"
#include "common/netaddr.h"

#include "core/oonf_subsystem.h"

#define CFG_NHDP_SECTION        "nhdp"

#define CFG_NHDP_DOMAIN_SECTION "domain"
#define CFG_NHDP_DEFAULT_DOMAIN "0"
#define CFG_DOMAIN_NO_METRIC    "-"
#define CFG_DOMAIN_ANY_METRIC   "*"
#define CFG_DOMAIN_NO_MPR       "-"
#define CFG_DOMAIN_ANY_MPR      "*"


enum {
  /* default metric value */
  NHDP_METRIC_DEFAULT = 0x10000,

  /* maximum number of metric domains */
  NHDP_MAXIMUM_DOMAINS = 4,

  /* message tlv for transporting IPv4 originator in ipv6 messages */
  NHDP_MSGTLV_IPV4ORIGINATOR = 226,

  /* message tlv for transporting mac address */
  NHDP_MSGTLV_MAC = 227,
};

#define LOG_NHDP nhdp_subsystem.logging
EXPORT struct oonf_subsystem nhdp_subsystem;

EXPORT extern enum oonf_log_source LOG_NHDP_R;
EXPORT extern enum oonf_log_source LOG_NHDP_W;

int nhdp_init(void)  __attribute__((warn_unused_result));
void nhdp_cleanup(void);

EXPORT void nhdp_set_originator(const struct netaddr *);
EXPORT void nhdp_reset_originator(int af_type);
EXPORT const struct netaddr *nhdp_get_originator(int af_type);

#endif /* NHDP_H_ */
