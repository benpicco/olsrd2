/*
 * nhdp_hysteresis.h
 *
 *  Created on: Feb 15, 2013
 *      Author: rogge
 */

#ifndef NHDP_HYSTERESIS_H_
#define NHDP_HYSTERESIS_H_

#include "common/common_types.h"
#include "rfc5444/rfc5444_reader.h"

#include "nhdp/nhdp_db.h"

struct nhdp_hysteresis_handler {
  /* name of the handler */
  const char *name;

  /* update pending/Lost (and maybe quality) of links hysteresis */
  void (*update_hysteresis)(struct nhdp_link *,
      struct rfc5444_reader_tlvblock_context *context, uint64_t, uint64_t);
};

EXPORT void nhdp_hysteresis_set_handler(struct nhdp_hysteresis_handler *);
EXPORT void nhdp_hysteresis_update(struct nhdp_link *,
    struct rfc5444_reader_tlvblock_context *context, uint64_t, uint64_t);

#endif /* NHDP_HYSTERESIS_H_ */
