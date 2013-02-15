/*
 * nhdp_mpr.h
 *
 *  Created on: Feb 15, 2013
 *      Author: rogge
 */

#ifndef NHDP_MPR_H_
#define NHDP_MPR_H_

#include "common/common_types.h"

#include "nhdp/nhdp_db.h"

/* handler for generating MPR information of a link */
struct nhdp_mpr_handler {
  /* name of handler */
  const char *name;

  /* update mpr settings of link, update all mpr settings if link is NULL */
  void (* update_mpr)(struct nhdp_link *);
};

void nhdp_mpr_init(void);
void nhdp_mpr_cleanup(void);

EXPORT void nhdp_mpr_add(void);
EXPORT void nhdp_mpr_remove(void);
EXPORT bool nhdp_mpr_is_active(void);
EXPORT void nhdp_mpr_set_willingness(int);
EXPORT int nhdp_mpr_get_willingness(void);
EXPORT void nhdp_mpr_set_flooding_handler(struct nhdp_mpr_handler *);
EXPORT void nhdp_mpr_set_routing_handler(struct nhdp_mpr_handler *);
EXPORT void nhdp_mpr_update_flooding(struct nhdp_link *);
EXPORT void nhdp_db_mpr_update_routing(struct nhdp_link *);

#endif /* NHDP_MPR_H_ */
