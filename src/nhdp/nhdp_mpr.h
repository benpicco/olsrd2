
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

#ifndef NHDP_MPR_H_
#define NHDP_MPR_H_

#include "common/common_types.h"
#include "rfc5444/rfc5444_iana.h"

#include "nhdp/nhdp_db.h"

/* handler for generating MPR information of a link */
struct nhdp_mpr_handler {
  /* name of handler */
  const char *name;

  /* update mpr settings */
  void (*update_mpr)(struct nhdp_interface *);

  /* set MPR selector choice of neighbor */
  void (*set_mprs)(struct nhdp_link *, bool);

  /* get MPR state of a link */
  bool (*is_mpr)(struct nhdp_link *);

  /* true if nhdp message for interface should contain willingness */
  bool (*use_willingness)(struct nhdp_interface *);
};

EXPORT void nhdp_mpr_set_flooding_handler(struct nhdp_mpr_handler *);
EXPORT void nhdp_mpr_set_routing_handler(struct nhdp_mpr_handler *);
EXPORT struct nhdp_mpr_handler *nhdp_mpr_get_flooding_handler(void);
EXPORT struct nhdp_mpr_handler *nhdp_mpr_get_routing_handler(void);

/**
 * Update the MPR settings of an interface
 * @param h pointer to MPR handler
 * @param interf pointer to local nhdp interface
 */
static INLINE void
nhdp_mpr_update(struct nhdp_mpr_handler *h, struct nhdp_interface *interf) {
  h->update_mpr(interf);
}

/**
 * Stores the MPR selectors of a neighbor
 * @param h pointer to MPR handler
 * @param lnk pointer to nhdp link
 * @param selected true if neighbor selected us as a MPR
 */
static INLINE void
nhdp_mpr_set_mprs(struct nhdp_mpr_handler *h, struct nhdp_link *lnk,
    bool selected) {
  h->set_mprs(lnk, selected);
}

/**
 * @param h pointer to MPR handler
 * @param lnk pointer to nhdp link
 * @return true if neighbor is our MPR
 */
static INLINE bool
nhdp_mpr_is_mpr(struct nhdp_mpr_handler *h, struct nhdp_link *lnk) {
  return h->is_mpr(lnk);
}

/**
 * @param h pointer to MPR handler
 * @param interf pointer to local nhdp interface
 * @return true if NHDP Hello message should contain Willingness TLV
 */
static INLINE bool
nhdp_mpr_use_willingness(struct nhdp_mpr_handler *h, struct nhdp_interface *interf) {
  return h->use_willingness(interf);
}
#endif /* NHDP_MPR_H_ */
