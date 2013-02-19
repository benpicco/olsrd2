
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
#include "common/autobuf.h"

#include "core/olsr_logging.h"
#include "core/olsr_plugins.h"
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444_conversion.h"
#include "rfc5444/rfc5444_reader.h"
#include "tools/olsr_rfc5444.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp_interfaces.h"

/* definitions and constants */
#define CFG_DEFAULTHYSTERESIS_SECTION "defaulthysteresis"

struct _config {
  int accept;
  int reject;
  int scaling;
};

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static void _cb_cfg_changed(void);
static int _cb_cfg_validate(const char *section_name,
    struct cfg_named_section *, struct autobuf *);

/* plugin declaration */
OLSR_PLUGIN7 {
  .descr = "OLSRD2 default hysteresis plugin",
  .author = "Henning Rogge",

  .load = _cb_plugin_load,
  .unload = _cb_plugin_unload,
  .enable = _cb_plugin_enable,
  .disable = _cb_plugin_disable,

  .can_disable = true,
  .can_unload = false,
};

/* configuration options */
static struct cfg_schema_section _hysteresis_section = {
  .type = CFG_DEFAULTHYSTERESIS_SECTION,
  .cb_delta_handler = _cb_cfg_changed,
  .cb_validate = _cb_cfg_validate,
};

static struct cfg_schema_entry _hysteresis_entries[] = {
  CFG_MAP_FRACTIONAL_MINMAX(_config, accept, "accept", "0.7",
      "link quality to consider a link up", 3, 0, 1000),
  CFG_MAP_FRACTIONAL_MINMAX(_config, reject, "reject", "0.3",
      "link quality to consider a link down", 3, 0, 1000),
  CFG_MAP_FRACTIONAL_MINMAX(_config, scaling, "scaling", "0.25",
      "exponential aging to control speed of link hysteresis", 3, 1, 1000),
};

static struct _config _hysteresis_config;

/* storage extension for nhdp_link */


/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  cfg_schema_add_section(olsr_cfg_get_schema(), &_hysteresis_section,
      _hysteresis_entries, ARRAYSIZE(_hysteresis_entries));

  return 0;
}

/**
 * Destructor of plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_unload(void) {
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_hysteresis_section);
  return 0;
}

/**
 * Enable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_enable(void) {
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  return 0;
}

static void
_cb_cfg_changed(void) {
  cfg_schema_tobin(&_hysteresis_config, _hysteresis_section.post,
      _hysteresis_entries, ARRAYSIZE(_hysteresis_entries));
}

static int
_cb_cfg_validate(const char *section_name,
    struct cfg_named_section *named, struct autobuf *out) {
  struct _config cfg;
  struct fraction_str buf1, buf2;

  if (cfg_schema_tobin(&cfg, named, _hysteresis_entries, ARRAYSIZE(_hysteresis_entries))) {
    cfg_append_printable_line(out, "Could not parse hysteresis configuration in section %s",
        section_name);
    return -1;
  }

  if (cfg.accept >= cfg.reject) {
    cfg_append_printable_line(out, "hysteresis accept %s is not smaller than reject %s value",
        cfg_fraction_to_string(&buf1, cfg.accept, 3),
        cfg_fraction_to_string(&buf2, cfg.reject, 3));
    return -1;
  }
  return 0;
}
