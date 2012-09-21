
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
#include "config/cfg_schema.h"
#include "rfc5444/rfc5444_writer.h"
#include "core/olsr_subsystem.h"
#include "tools/olsr_cfg.h"
#include "tools/olsr_rfc5444.h"
#include "nhdp/nhdp_interfaces.h"
#include "nhdp/nhdp_reader.h"
#include "nhdp/nhdp_writer.h"
#include "nhdp/nhdp.h"

#define _LOG_NHDP_NAME "nhdp"

struct _nhdp_config {
  struct netaddr originator;
};

static void _cb_cfg_nhdp_changed(void);

/* configuration options for nhdp section */
static struct cfg_schema_section _nhdp_section = {
  .type = CFG_NHDP_SECTION,
  .mode = CFG_SSMODE_UNNAMED,
  .cb_delta_handler = _cb_cfg_nhdp_changed,
};

static struct cfg_schema_entry _nhdp_entries[] = {
  CFG_MAP_NETADDR_V46(_nhdp_config, originator, "originator", "-",
      "Originator address for all NHDP messages", false, true),
};

static struct _nhdp_config _config;

OLSR_SUBSYSTEM_STATE(_nhdp_state);

enum log_source LOG_NHDP = LOG_MAIN;
static struct olsr_rfc5444_protocol *_protocol;

int
nhdp_init(void) {
  if (olsr_subsystem_is_initialized(&_nhdp_state)) {
    return 0;
  }

  LOG_NHDP = olsr_log_register_source(_LOG_NHDP_NAME);

  _protocol = olsr_rfc5444_add_protocol(RFC5444_PROTOCOL, true);
  if (_protocol == NULL) {
    return -1;
  }

  nhdp_reader_init(_protocol);
  if (nhdp_writer_init(_protocol)) {
    nhdp_reader_cleanup();
    olsr_rfc5444_remove_protocol(_protocol);
    return -1;
  }

  nhdp_interfaces_init(_protocol);
  nhdp_db_init();

  /* add additional configuration for interface section */
  cfg_schema_add_section(olsr_cfg_get_schema(), &_nhdp_section,
      _nhdp_entries, ARRAYSIZE(_nhdp_entries));

  memset(&_config, 0, sizeof(_config));

  olsr_subsystem_init(&_nhdp_state);
  return 0;
}

void
nhdp_cleanup(void) {
  if (olsr_subsystem_cleanup(&_nhdp_state)) {
    return;
  }

  cfg_schema_remove_section(olsr_cfg_get_schema(), &_nhdp_section);

  nhdp_db_cleanup();
  nhdp_interfaces_cleanup();

  nhdp_writer_cleanup();
  nhdp_reader_cleanup();
}

const struct netaddr *
nhdp_get_originator(void) {
  return &_config.originator;
}

/**
 * Configuration has changed, handle the changes
 */
static void
_cb_cfg_nhdp_changed(void) {
  if (cfg_schema_tobin(&_config, _nhdp_section.post,
      _nhdp_entries, ARRAYSIZE(_nhdp_entries))) {
    OLSR_WARN(LOG_NHDP, "Cannot convert NHDP settings.");
    return;
  }
}
