
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

#include "common/common_types.h"
#include "common/netaddr.h"
#include "core/olsr_subsystem.h"

#include "olsrv2/olsrv2.h"
#include "olsrv2/olsrv2_originator_set.h"

static struct netaddr _originator, _configured_originator;

OLSR_SUBSYSTEM_STATE(_olsrv2_state);

/**
 * Initialize OLSRv2 subsystem
 */
void
olsrv2_init(void) {
  if (olsr_subsystem_init(&_olsrv2_state)) {
    return;
  }

  memset(&_originator, 0, sizeof(_originator));
}

/**
 * Cleanup OLSRv2 subsystem
 */
void
olsrv2_cleanup(void) {
  if (olsr_subsystem_cleanup(&_olsrv2_state)) {
    return;
  }

  olsrv2_originatorset_cleanup();
}

/**
 * @return current originator address
 */
const struct netaddr *
olsrv2_get_originator(void) {
  return &_originator;
}

/**
 * Sets a new originator address
 * @param originator originator address, NULL to return to configured one
 */
void
olsrv2_set_originator(const struct netaddr *originator) {
  if (originator == NULL) {
    memcpy(&_originator, &_configured_originator, sizeof(_originator));
  }
  else {
    memcpy(&_originator, originator, sizeof(_originator));
  }
}
