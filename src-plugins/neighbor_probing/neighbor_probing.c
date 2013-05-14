
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
#include "config/cfg_schema.h"
#include "core/oonf_logging.h"
#include "core/oonf_plugins.h"
#include "core/oonf_subsystem.h"
#include "subsystems/oonf_class.h"
#include "subsystems/oonf_timer.h"
#include "nhdp/nhdp_db.h"

#include "neighbor_probing/neighbor_probing.h"

/* definitions and constants */
struct _config {
  /* Interval between two link probes */
  uint64_t interval;

  /* size of probe */
  uint16_t probe_size;
};

struct _probing_link_data {
  /* absolute timestamp of last check if probing is necessary */
  uint64_t last_probe_check;

  /*
   * number of bytes that had been sent to neighbor during last
   * probe check.
   */
  uint64_t last_tx_traffic;
};

/* prototypes */
static int _init(void);
static void _cleanup(void);
static void _cb_probe_link(void *);
static void _cb_cfg_changed(void);

/* plugin declaration */
static struct cfg_schema_entry _probing_entries[] = {
  CFG_MAP_CLOCK_MIN(_config, interval, "interval", "1.0",
      "Time interval between link probing", 100),
  CFG_MAP_INT_MINMAX(_config, probe_size, "size", "512",
      "Number of bytes used for neighbor probe",
      1, 1500),
};

static struct cfg_schema_section _probing_section = {
  .type = OONF_PLUGIN_GET_NAME(),
  .cb_delta_handler = _cb_cfg_changed,
  .entries = _probing_entries,
  .entry_count = ARRAYSIZE(_probing_entries),
};

struct oonf_subsystem olsrv2_ffett_subsystem = {
  .name = OONF_PLUGIN_GET_NAME(),
  .descr = "OONFD2 Funkfeuer ETT plugin",
  .author = "Henning Rogge",

  .cfg_section = &_probing_section,

  .init = _init,
  .cleanup = _cleanup,
};
DECLARE_OONF_PLUGIN(olsrv2_ffett_subsystem);

struct _config _probe_config;

/* storage extension and listeners */
static struct oonf_class_extension _link_extenstion = {
  .name = "probing linkmetric",
  .class_name = NHDP_CLASS_LINK,
  .size = sizeof(struct _probing_link_data),
};

/* timer class to measure interval between probes */
static struct oonf_timer_info _probe_info = {
  .name = "Link probing timer",
  .callback = _cb_probe_link,
  .periodic = true,
};

static struct oonf_timer_entry _probe_timer = {
  .info = &_probe_info,
};

/**
 * Initialize plugin
 * @return -1 if an error happened, 0 otherwise
 */
static int
_init(void) {
  if (oonf_class_extend(&_link_extenstion)) {
    return -1;
  }

  oonf_timer_add(&_probe_info);
  return 0;
}

/**
 * Cleanup plugin
 */
static void
_cleanup(void) {
  oonf_timer_remove(&_probe_info);
}

static void
_cb_probe_link(void *ptr __attribute__((unused))) {

}

/**
 * Callback triggered when configuration changes
 */
static void
_cb_cfg_changed(void) {
  if (cfg_schema_tobin(&_probe_config, _probing_section.post,
      _probing_entries, ARRAYSIZE(_probing_entries))) {
    OONF_WARN(LOG_PLUGINS, "Cannot convert configuration for %s plugin",
        OONF_PLUGIN_GET_NAME());
    return;
  }

  oonf_timer_set(&_probe_timer, _probe_config.interval);
}
