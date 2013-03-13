
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

#include <stdio.h>

#include "common/common_types.h"
#include "common/autobuf.h"

#include "core/olsr_class.h"
#include "core/olsr_logging.h"
#include "core/olsr_plugins.h"
#include "core/olsr_timer.h"
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444.h"
#include "rfc5444/rfc5444_reader.h"
#include "tools/olsr_rfc5444.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp_hysteresis.h"
#include "nhdp/nhdp_interfaces.h"

/* definitions and constants */
#define CFG_HYSTERESIS_OLSRV1_SECTION "hysteresis_olsrv1"

struct _config {
  int accept;
  int reject;
  int scaling;
};

struct link_hysteresis_data {
  uint64_t interval;
  int32_t quality;
  bool pending, lost;

  struct olsr_timer_entry interval_timer;
};

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static void _update_hysteresis(struct nhdp_link *,
    struct link_hysteresis_data *, bool);

static void _cb_link_added(void *);
static void _cb_link_removed(void *);

static void _cb_update_hysteresis(struct nhdp_link *,
    struct rfc5444_reader_tlvblock_context *context);
static bool _cb_is_pending(struct nhdp_link *);
static bool _cb_is_lost(struct nhdp_link *);
static const char *_cb_to_string(
    struct nhdp_hysteresis_str *, struct nhdp_link *);

static void _cb_timer_hello_lost(void *);
static void _cb_cfg_changed(void);
static int _cb_cfg_validate(const char *section_name,
    struct cfg_named_section *, struct autobuf *);

/* plugin declaration */
OLSR_PLUGIN7 {
  .descr = "OLSRD2 olsrV1 hysteresis plugin",
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
  .type = CFG_HYSTERESIS_OLSRV1_SECTION,
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
struct olsr_class_extension _link_extenstion = {
  .name = "hysteresis_olsrv1",
  .class_name = NHDP_CLASS_LINK,
  .size = sizeof(struct link_hysteresis_data),
};

struct olsr_class_listener _link_listener = {
  .name = "hysteresis listener",
  .class_name = NHDP_CLASS_LINK,
  .cb_add = _cb_link_added,
  .cb_remove = _cb_link_removed,
};

/* timer class to measure interval between Hellos */
struct olsr_timer_info _hello_timer_info = {
  .name = "Hello interval timeout for hysteresis",
  .callback = _cb_timer_hello_lost,
};

/* hysteresis handler */
struct nhdp_hysteresis_handler _hysteresis_handler = {
  .name = "hysteresis_olsrv1",
  .update_hysteresis = _cb_update_hysteresis,
  .is_pending = _cb_is_pending,
  .is_lost = _cb_is_lost,
  .to_string = _cb_to_string,
};

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
  if (olsr_class_is_extension_registered(&_link_extenstion)) {
    struct nhdp_link *lnk;

    /* add all custom extensions for link */
    list_for_each_element(&nhdp_link_list, lnk, _global_node) {
      _cb_link_added(lnk);
    }
  }
  else if (olsr_class_extend(&_link_extenstion)) {
    return -1;
  }

  if (olsr_class_listener_add(&_link_listener)) {
    return -1;
  }

  nhdp_hysteresis_set_handler(&_hysteresis_handler);
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  struct nhdp_link *lnk;

  /* remove all custom extensions for link */
  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    _cb_link_removed(lnk);
  }

  nhdp_hysteresis_set_handler(NULL);
  olsr_class_listener_remove(&_link_listener);
  return 0;
}

/**
 * Update the quality value of a link
 * @param lnk pointer to nhdp link
 * @param data pointer to link hysteresis data
 * @param lost true if hello was lost, false if hello was received
 */
static void
_update_hysteresis(struct nhdp_link *lnk,
    struct link_hysteresis_data *data, bool lost) {
  /* calculate exponential aging */
  data->quality = data->quality * (1000 - _hysteresis_config.scaling);
  data->quality = (data->quality + 999) / 1000;
  if (!lost) {
    data->quality += _hysteresis_config.scaling;
  }

  if (!data->pending && !data->lost) {
    if (data->quality < _hysteresis_config.reject) {
      data->lost = true;
      nhdp_db_link_update_status(lnk);
    }
  }
  else {
    if (data->quality > _hysteresis_config.accept) {
      data->pending = false;
      data->lost = false;
      nhdp_db_link_update_status(lnk);
    }
  }
}

/**
 * Callback triggered when a new nhdp link is added
 * @param ptr nhdp link
 */
static void
_cb_link_added(void *ptr) {
  struct link_hysteresis_data *data;
  data = olsr_class_get_extension(&_link_extenstion, ptr);

  memset(data, 0, sizeof(*data));
  data->pending = true;

  data->interval_timer.info = &_hello_timer_info;
  data->interval_timer.cb_context = ptr;
}

/**
 * Callback triggered when a nhdp link will be removed
 * @param ptr nhdp link
 */
static void
_cb_link_removed(void *ptr) {
  struct link_hysteresis_data *data;
  data = olsr_class_get_extension(&_link_extenstion, ptr);

  olsr_timer_stop(&data->interval_timer);
}

/**
 * Callback for hysteresis handler which is triggered to
 * update the hysteresis when a HELLO is received.
 * @param lnk nhdp link
 * @param context RFC5444 message context
 * @param vtime validity time
 * @param itime interval time, 0 if not set
 */
static void
_cb_update_hysteresis(struct nhdp_link *lnk,
    struct rfc5444_reader_tlvblock_context *context __attribute__((unused))) {
  struct link_hysteresis_data *data;

  data = olsr_class_get_extension(&_link_extenstion, lnk);

  /* update hysteresis because of received hello */
  _update_hysteresis(lnk, data, false);

  /* store interval */
  data->interval = lnk->itime_value;

  /* first timer gets a delay */
  if (data->interval == 0) {
    data->interval = lnk->vtime_value;
  }
  olsr_timer_set(&data->interval_timer, (data->interval * 3) / 2);
}

/**
 * Callback for hysteresis handler to check if link is pending
 * @param lnk nhdp link
 * @return true if link is pending, false otherwise
 */
static bool
_cb_is_pending(struct nhdp_link *lnk) {
  struct link_hysteresis_data *data;

  data = olsr_class_get_extension(&_link_extenstion, lnk);
  return data->pending;
}

/**
 * Callback for hysteresis handler to check if link is lost
 * @param lnk nhdp link
 * @return true if link is lost, false otherwise
 */
static bool
_cb_is_lost(struct nhdp_link *lnk) {
  struct link_hysteresis_data *data;

  data = olsr_class_get_extension(&_link_extenstion, lnk);
  return data->lost;
}

/**
 * Callback for hysteresis handler to get a human readable
 * from of the current hysteresis data.
 * @param buf output buffer
 * @param lnk nhdp link
 * @return pointer to output buffer
 */
static const char *
_cb_to_string(struct nhdp_hysteresis_str *buf, struct nhdp_link *lnk) {
  struct fraction_str fbuf;
  struct link_hysteresis_data *data;

  data = olsr_class_get_extension(&_link_extenstion, lnk);

  snprintf(buf->buf, sizeof(*buf), "quality=%s", cfg_fraction_to_string(&fbuf, data->quality, 3));

  return buf->buf;
}

/**
 * Timer callback triggered when Hello was lost
 * @param ptr nhdp link
 */
static void
_cb_timer_hello_lost(void *ptr) {
  struct link_hysteresis_data *data;

  data = olsr_class_get_extension(&_link_extenstion, ptr);

  /* update hysteresis because of lost Hello */
  _update_hysteresis(ptr, data, true);

  /* reactivate timer */
  olsr_timer_set(&data->interval_timer, data->interval);
}

/**
 * Callback triggered when configuration changes
 */
static void
_cb_cfg_changed(void) {
  cfg_schema_tobin(&_hysteresis_config, _hysteresis_section.post,
      _hysteresis_entries, ARRAYSIZE(_hysteresis_entries));
}

/**
 * Callback triggered to check validity of configuration section
 * @param section_name name of section
 * @param named configuration data of section
 * @param out output buffer for error messages
 * @return 0 if data is okay, -1 if an error happened
 */
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

  if (cfg.accept <= cfg.reject) {
    cfg_append_printable_line(out, "hysteresis accept (%s) is not smaller than reject (%s) value",
        cfg_fraction_to_string(&buf1, cfg.accept, 3),
        cfg_fraction_to_string(&buf2, cfg.reject, 3));
    return -1;
  }
  return 0;
}
