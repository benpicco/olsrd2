
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
#include "rfc5444/rfc5444_reader.h"
#include "core/olsr_logging.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_hysteresis.h"

/* prototypes */
static void _update_hysteresis(struct nhdp_link *lnk,
    struct rfc5444_reader_tlvblock_context *context, uint64_t, uint64_t);
static bool _is_pending(struct nhdp_link *);
static bool _is_lost(struct nhdp_link *);
static const char * _to_string(struct nhdp_hysteresis_str *buf,
    struct nhdp_link *lnk);

/* default handler */
struct nhdp_hysteresis_handler _handler = {
  .name = "No NHDP hysteresis",
  .update_hysteresis = _update_hysteresis,
  .is_pending = _is_pending,
  .is_lost = _is_lost,
  .to_string = _to_string,
};

/* hysteresis handler */
static struct nhdp_hysteresis_handler *nhdp_hysteresis = &_handler;

/**
 * Set new handler hysteresis handler
 * @param handler pointer to handler, NULL to reset to default
 */
void
nhdp_hysteresis_set_handler(struct nhdp_hysteresis_handler *handler) {
  if (handler == NULL) {
    nhdp_hysteresis = &_handler;
  }
  else {
    nhdp_hysteresis = handler;
  }
}

/**
 * @return current nhdp hysteresis handler
 */
struct nhdp_hysteresis_handler *
nhdp_hysteresis_get_handler(void) {
  return nhdp_hysteresis;
}


/**
 * Dummy function for hysteresis update (does nothing)
 * @param lnk
 * @param context
 * @param vtime
 * @param itime
 */
static void
_update_hysteresis(struct nhdp_link *lnk __attribute__((unused)),
    struct rfc5444_reader_tlvblock_context *context __attribute__((unused)),
    uint64_t vtime __attribute__((unused)), uint64_t itime __attribute__((unused))) {
  /* do nothing */
  OLSR_DEBUG(LOG_MAIN, "1");
  return;
}

/**
 * Dummy function for testing if link is pending
 * @param lnk pointer to link
 * @return always false
 */
static bool
_is_pending(struct nhdp_link *lnk __attribute((unused))) {
  return false;
}

/**
 * Dummy function for testing if link is lost
 * @param lnk pointer to link
 * @return always false
 */
static bool
_is_lost(struct nhdp_link *lnk __attribute__((unused))) {
  return false;
}

/**
 * Dummy function to create text representation of
 * hysteresis data.
 * @param buf pointer to output buffer
 * @param lnk pointer to link to describe in text
 * @return always returns pointer to empty string
 */
static const char *
_to_string(struct nhdp_hysteresis_str *buf,
    struct nhdp_link *lnk __attribute__((unused))) {
  buf->buf[0] = 0;
  return buf->buf;
}
