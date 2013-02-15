/*
 * nhdp_hysteresis.c
 *
 *  Created on: Feb 15, 2013
 *      Author: rogge
 */

#include "common/common_types.h"
#include "rfc5444/rfc5444_reader.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_hysteresis.h"

/* hysteresis handler */
static struct nhdp_hysteresis_handler *_hysteresis = NULL;

void
nhdp_hysteresis_set_handler(struct nhdp_hysteresis_handler *handler) {
  _hysteresis = handler;
}

void
nhdp_hysteresis_update(struct nhdp_link *lnk,
    struct rfc5444_reader_tlvblock_context *context,
    uint64_t vtime, uint64_t itime) {
  if (_hysteresis == NULL) {
    lnk->hysteresis.pending = false;
    return;
  }

  _hysteresis->update_hysteresis(lnk, context, vtime, itime);
}




