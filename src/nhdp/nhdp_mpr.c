
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
static int _mpr_willingness_default = RFC5444_WILLINGNESS_DEFAULT;

/* configuration section */
static struct cfg_schema_section _nhdp_section = {
  .type = CFG_NHDP_SECTION,
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
 * @param will MPR willingness (0-15) or -1 to use default willingness
 */
void
nhdp_mpr_set_willingness(int will) {
  if (will >= 0) {
    _mpr_willingness = will;
  }
  else {
    _mpr_willingness = _mpr_willingness_default;
  }
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

  _mpr_willingness_default = config.mpr_willingness;
}
