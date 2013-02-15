/*
 * nhdp_mpr.c
 *
 *  Created on: Feb 15, 2013
 *      Author: rogge
 */

#include "common/common_types.h"
#include "config/cfg.h"
#include "config/cfg_schema.h"
#include "core/olsr_logging.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_mpr.h"

/* Prototypes */
struct _nhdp_config {
  int mpr_willingness;
};

static void _cb_cfg_changed(void);

/* MPR handlers */
static struct nhdp_mpr_handler *_flooding_mpr = NULL;
static struct nhdp_mpr_handler *_routing_mpr = NULL;
static int _mpr_active_counter = 0;
static int _mpr_willingness = RFC5444_WILLINGNESS_DEFAULT;

/* configuration section */
static struct cfg_schema_section _nhdp_section = {
  .type = "nhdp",
  .cb_delta_handler = _cb_cfg_changed,
};

static struct cfg_schema_entry _nhdp_entries[] = {
  CFG_MAP_INT(_nhdp_config, mpr_willingness, "willingness",
      "7", "Willingness for MPR calculation"),
};

void
nhdp_mpr_init(void) {
  /* add additional configuration for interface section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_nhdp_section,
      _nhdp_entries, ARRAYSIZE(_nhdp_entries));
}

void nhdp_mpr_cleanup(void) {
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_nhdp_section);
}

/**
 * Register a user of MPR TLVs in NHDP Hellos
 */
void
nhdp_mpr_add(void) {
  _mpr_active_counter++;
  if (_mpr_active_counter == 1) {
    nhdp_mpr_update_flooding(NULL);
    nhdp_db_mpr_update_routing(NULL);
  }
}
/**
 * Unregister a user of MPR TLVs in NHDP Hellos
 */
void
nhdp_mpr_remove(void) {
  _mpr_active_counter--;
  if (_mpr_active_counter == 0) {
    nhdp_mpr_update_flooding(NULL);
    nhdp_db_mpr_update_routing(NULL);
  }
}

/**
 * @return true if MPRs are in use at the moment
 */
bool
nhdp_mpr_is_active(void) {
  return _mpr_active_counter > 0;
}

/**
 * Set the MPR willingness parameter of NHDP messages
 * @param will MPR willingness (0-15)
 */
void
nhdp_mpr_set_willingness(int will) {
  _mpr_willingness = will;
}

/**
 * @return current MPR willingness (0-15)
 */
int
nhdp_mpr_get_willingness(void) {
  return _mpr_willingness;
}

/**
 * Sets a new NHDP flooding MPR handler
 * @param mprh pointer to handler, NULL if no handler
 */
void
nhdp_mpr_set_flooding_handler(struct nhdp_mpr_handler *mprh) {
  _flooding_mpr = mprh;
  nhdp_mpr_update_flooding(NULL);
}

/**
 * Sets a new NHDP routing MPR handler
 * @param mprh pointer to handler, NULL if no handler
 */
void
nhdp_mpr_set_routing_handler(struct nhdp_mpr_handler *mprh) {
  _routing_mpr = mprh;
  nhdp_db_mpr_update_routing(NULL);
}

/**
 * Update the set of flooding MPRs
 * @param lnk pointer to link that changed, NULL if a change
 *   on multiple links might have happened
 */
void
nhdp_mpr_update_flooding(struct nhdp_link *lnk) {
  bool active = _mpr_active_counter > 0;

  if (active && _flooding_mpr != NULL) {
    _flooding_mpr->update_mpr(lnk);
    return;
  }

  if (lnk) {
    lnk->mpr_flooding.mpr = active;
    return;
  }

  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    lnk->mpr_flooding.mpr = active;
  }
}

/**
 * Update the set of routing MPRs
 * @param lnk pointer to link that changed, NULL if a change
 *   on multiple links might have happened
 */
void
nhdp_db_mpr_update_routing(struct nhdp_link *lnk) {
  bool active = _mpr_active_counter > 0;

  if (active && _routing_mpr != NULL) {
    _routing_mpr->update_mpr(lnk);
    return;
  }

  if (lnk) {
    lnk->mpr_routing.mpr = active;
    return;
  }

  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    lnk->mpr_routing.mpr = active;
  }
}


/**
 * Configuration has changed, handle the changes
 */
static void
_cb_cfg_changed(void) {
  struct _nhdp_config config;

  if (cfg_schema_tobin(&config, _nhdp_section.post,
      _nhdp_entries, ARRAYSIZE(_nhdp_entries))) {
    OLSR_WARN(LOG_NHDP, "Cannot convert NHDP global settings.");
    return;
  }
}
