
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
#include "rfc5444/rfc5444_conversion.h"
#include "rfc5444/rfc5444_reader.h"
#include "tools/olsr_rfc5444.h"
#include "tools/olsr_cfg.h"

#include "nhdp/nhdp_linkmetric.h"
#include "nhdp/nhdp_interfaces.h"

/* definitions and constants */
#define CFG_HYSTERESIS_OLSRV1_SECTION "ff_etx"

struct _config {
  uint64_t interval;
  int window;
  int start_window;
};

struct link_etxff_bucket {
  int received;
  int total;
};

struct link_etxff_data {
  struct nhdp_metric metric;

  int activePtr;
  int missed_hellos;

  uint16_t window_size;
  uint16_t last_seq_nr;

  struct olsr_timer_entry hello_lost_timer;

  struct link_etxff_bucket buckets[0];
};

struct neighbor_etxff_data {
  struct nhdp_metric metric;
};

struct l2hop_etxff_data {
  struct nhdp_metric metric;
};

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static void _cb_link_added(void *);
static void _cb_link_removed(void *);

static void _cb_etx_sampling(void *);
static void _cb_hello_lost(void *);

static enum rfc5444_result _cb_process_packet(
    struct rfc5444_reader_tlvblock_consumer *,
      struct rfc5444_reader_tlvblock_context *context);

static int _cb_cfg_validate(const char *section_name,
    struct cfg_named_section *, struct autobuf *);
static void _cb_cfg_changed(void);

/* plugin declaration */
OLSR_PLUGIN7 {
  .descr = "OLSRD2 Funkfeuer ETX plugin",
  .author = "Henning Rogge",

  .load = _cb_plugin_load,
  .unload = _cb_plugin_unload,
  .enable = _cb_plugin_enable,
  .disable = _cb_plugin_disable,

  .can_disable = true,
  .can_unload = false,
};

/* configuration options */
static struct cfg_schema_section _etxff_section = {
  .type = CFG_HYSTERESIS_OLSRV1_SECTION,
  .cb_validate = _cb_cfg_validate,
  .cb_delta_handler = _cb_cfg_changed,
};

static struct cfg_schema_entry _etxff_entries[] = {
  CFG_MAP_CLOCK_MIN(_config, interval, "interval", "1.0",
      "Time interval between recalculations of metric", 100),
  CFG_MAP_INT_MINMAX(_config, window, "window", "64",
      "Number of intervals to calculate average ETX", 2, 65535),
  CFG_MAP_INT_MINMAX(_config, start_window, "start_window", "4",
      "Window sized used during startup, will be increased by 1"
      " for each interval. Smaller values allow quicker initial"
      " rise of metric value, it cannot be larger than the normal"
      " windows size.",
      1, 65535),
};

static struct _config _etxff_config = { 0,0,0 };

/* RFC5444 packet listener */
struct olsr_rfc5444_protocol *_protocol;

struct rfc5444_reader_tlvblock_consumer _packet_consumer = {
  .order = RFC5444_LQ_PARSER_PRIORITY,
  .default_msg_consumer = true,
  .start_callback = _cb_process_packet,
};

/* storage extension and listeners */
struct olsr_class_extension _link_extenstion = {
  .name = "etxff linkmetric",
  .class_name = NHDP_CLASS_LINK,
  .size = sizeof(struct link_etxff_data),
};

struct olsr_class_extension _neigh_extenstion = {
  .name = "etxff neighmetric",
  .class_name = NHDP_CLASS_NEIGHBOR,
  .size = sizeof(struct neighbor_etxff_data),
};

struct olsr_class_extension _twohop_extenstion = {
  .name = "etxff twohop-metric",
  .class_name = NHDP_CLASS_LINK_2HOP,
  .size = sizeof(struct l2hop_etxff_data),
};

struct olsr_class_listener _link_listener = {
  .name = "etxff link listener",
  .class_name = NHDP_CLASS_LINK,
  .cb_add = _cb_link_added,
  .cb_remove = _cb_link_removed,
};

/* timer class to measure interval between Hellos */
struct olsr_timer_info _sampling_timer_info = {
  .name = "Sampling timer for ETXFF-metric",
  .callback = _cb_etx_sampling,
};

struct olsr_timer_info _hello_lost_info = {
  .name = "Hello lost timer for ETXFF-metric",
  .callback = _cb_hello_lost,
};

/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  cfg_schema_add_section(olsr_cfg_get_schema(), &_etxff_section,
      _etxff_entries, ARRAYSIZE(_etxff_entries));

  return 0;
}

/**
 * Destructor of plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_unload(void) {
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_etxff_section);
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
  else {
    _link_extenstion.size +=
        sizeof(struct link_etxff_bucket) * _etxff_config.window;

    if (olsr_class_extend(&_link_extenstion)) {
      return -1;
    }
  }

  if (olsr_class_extend(&_neigh_extenstion)) {
    return -1;
  }

  if (olsr_class_extend(&_twohop_extenstion)) {
    return -1;
  }

  if (olsr_class_listener_add(&_link_listener)) {
    return -1;
  }

  olsr_timer_add(&_sampling_timer_info);
  olsr_timer_add(&_hello_lost_info);

  _protocol = olsr_rfc5444_add_protocol(RFC5444_PROTOCOL, true);

  // TODO: set metric handler
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  struct nhdp_link *lnk;
  // TODO: remove metric handler

  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    _cb_link_removed(lnk);
  }

  olsr_class_listener_remove(&_link_listener);

  olsr_timer_remove(&_sampling_timer_info);
  olsr_timer_remove(&_hello_lost_info);
  return 0;
}

/**
 * Callback triggered when a new nhdp link is added
 * @param ptr nhdp link
 */
static void
_cb_link_added(void *ptr) {
  struct link_etxff_data *data;
  struct nhdp_link *lnk;
  int i;

  lnk = ptr;
  data = olsr_class_get_extension(&_link_extenstion, lnk);

  memset(data, 0, sizeof(*data));
  data->window_size= _etxff_config.start_window;
  data->activePtr = -1;

  for (i = 0; i<_etxff_config.window; i++) {
    data->buckets[i].total = 1;
  }

  /* start 'hello lost' timer for link */
  data->hello_lost_timer.info = &_hello_lost_info;
  data->hello_lost_timer.cb_context = ptr;

  if (lnk->itime_value > 0) {
    olsr_timer_start(&data->hello_lost_timer, lnk->itime_value);
  }
  else {
    olsr_timer_start(&data->hello_lost_timer, lnk->vtime_value);
  }
}

static void
_cb_link_removed(void *ptr) {
  struct link_etxff_data *data;

  data = olsr_class_get_extension(&_link_extenstion, ptr);

  olsr_timer_stop(&data->hello_lost_timer);
}

/**
 * Timer callback to sample new ETX values into bucket
 * @param ptr nhdp link
 */
static void
_cb_etx_sampling(void *ptr __attribute__((unused))) {
  struct neighbor_etxff_data *ndata;
  struct link_etxff_data *ldata;
  struct nhdp_neighbor *neigh;
  struct nhdp_link *lnk;
  uint32_t total, received;
  uint64_t metric;
  int i,j;

  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    ldata = olsr_class_get_extension(&_link_extenstion, lnk);

    if (ldata->activePtr == -1) {
      /* still no data for this link */
      continue;
    }

    /* initialize counter */
    total = 0;
    received = 0;

    /* calculate ETX */
    for (i=0; i<ldata->window_size; i++) {
      j = (ldata->activePtr + i) % _etxff_config.window;

      received += ldata->buckets[j].received;
      total += ldata->buckets[j].total;
    }

    /* calculate MAX(0x1000 * total / received, 0x100000) */
    if ((received << 8) < total) {
      metric = 0x100000;
    }
    else {
      metric = (0x1000 * total) / received;
    }

    /* convert into incoming metric value */
    if (metric > RFC5444_METRIC_MAX) {
      /* metric overflow */
      ldata->metric.incoming = RFC5444_METRIC_MAX;
    }
    else {
      ldata->metric.incoming = (uint32_t)metric;
    }
  }

  list_for_each_element(&nhdp_neigh_list, neigh, _node) {
    ndata = olsr_class_get_extension(&_neigh_extenstion, neigh);

    nhdp_linkmetric_calculate_neighbor_metric(neigh, &ndata->metric);
  }
}

static void
_cb_hello_lost(void *ptr) {
  struct link_etxff_data *ldata;
  struct nhdp_link *lnk;

  lnk = ptr;
  ldata = olsr_class_get_extension(&_link_extenstion, lnk);

  if (ldata->activePtr != -1) {
    ldata->missed_hellos++;
    ldata->buckets[ldata->activePtr].total += ldata->missed_hellos * ldata->missed_hellos;
  }
}

static enum rfc5444_result
_cb_process_packet(struct rfc5444_reader_tlvblock_consumer *consumer __attribute__((unused)),
      struct rfc5444_reader_tlvblock_context *context) {
  struct link_etxff_data *ldata;
  struct nhdp_interface *interf;
  struct nhdp_laddr *laddr;
  struct nhdp_link *lnk;
  int total;

  if (!_protocol->input_is_multicast) {
    /* silently ignore unicasts */
    return RFC5444_OKAY;
  }

  if (!context->has_pktseqno) {
    struct netaddr_str buf;

    OLSR_WARN(LOG_PLUGINS, "Error, neighbor %s does not send packet sequence numbers!",
        netaddr_socket_to_string(&buf, _protocol->input_socket));
    return RFC5444_DROP_PACKET;
  }

  /* get interface and link */
  interf = nhdp_interface_get(_protocol->input_interface->name);
  if (interf == NULL) {
    /* silently ignore unknown interface */
    return RFC5444_OKAY;
  }
  laddr = nhdp_interface_get_link_addr(interf, _protocol->input_address);
  if (laddr == NULL) {
    /* silently ignore unknown link*/
    return RFC5444_OKAY;
  }

  /* get link and its etx data */
  lnk = laddr->link;
  ldata = olsr_class_get_extension(&_link_extenstion, lnk);

  if (ldata->activePtr == -1) {
    ldata->activePtr = 0;
    ldata->buckets[0].received = 1;
    ldata->buckets[0].total = 1;
    return RFC5444_OKAY;
  }

  total = ldata->last_seq_nr;
  if (ldata->last_seq_nr > context->pkt_seqno) {
    total += 65536;
  }
  total -= context->pkt_seqno;

  if (total > 255) {
    /* most likely a restart of the pkt seqno counter */
    total = 1;
  }

  ldata->buckets[ldata->activePtr].received++;
  ldata->buckets[ldata->activePtr].total += total;

  return RFC5444_OKAY;
}

/**
 * Callback triggered when configuration changes
 */
static void
_cb_cfg_changed(void) {
  cfg_schema_tobin(&_etxff_config, _etxff_section.post,
      _etxff_entries, ARRAYSIZE(_etxff_entries));
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

  if (cfg_schema_tobin(&_etxff_config, named,
      _etxff_entries, ARRAYSIZE(_etxff_entries))) {
    cfg_append_printable_line(out, "Could not parse hysteresis configuration in section %s",
        section_name);
    return -1;
  }

  if (cfg.window != 0) {
    cfg_append_printable_line(out, "%s: ETXff window cannot be changed during runtime",
        section_name);
    return -1;
  }

  if (cfg.window < cfg.start_window) {
    cfg_append_printable_line(out, "%s: Starting window must be smaller or equal than total window",
        section_name);
    return -1;
  }
  return 0;
}
