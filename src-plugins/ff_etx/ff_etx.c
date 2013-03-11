
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

#define ETXFF_LINKCOST_MINIMUM 0x1000
#define ETXFF_LINKCOST_START   0x10000
#define ETXFF_LINKCOST_MAXIMUM 0x10000

/* Configuration settings of ETXFF Metric */
struct _config {
  /* Interval between two updates of the metric */
  uint64_t interval;

  /* length of history in 'interval sized' memory cells */
  int window;

  /* length of history window when a new link starts */
  int start_window;
};

/* a single history memory cell */
struct link_etxff_bucket {
  /* number of RFC5444 packets received in time interval */
  int received;

  /* sum of received and lost RFC5444 packets in time interval */
  int total;
};

/* Additional data for a nhdp_link for metric calculation */
struct link_etxff_data {
  /* current position in history ringbuffer */
  int activePtr;

  /* number of missed hellos based on timeouts since last received packet */
  int missed_hellos;

  /* current window size for this link */
  uint16_t window_size;

  /* last received packet sequence number */
  uint16_t last_seq_nr;

  /* timer for measuring lost hellos when no further packets are received */
  struct olsr_timer_entry hello_lost_timer;

  /* last known hello interval */
  uint64_t hello_interval;

  /* history ringbuffer */
  struct link_etxff_bucket buckets[0];
};

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static void _cb_link_added(void *);
static void _cb_link_changed(void *);
static void _cb_link_removed(void *);

static void _cb_etx_sampling(void *);
static void _cb_hello_lost(void *);

static enum rfc5444_result _cb_process_packet(
    struct rfc5444_reader_tlvblock_consumer *,
      struct rfc5444_reader_tlvblock_context *context);

static const char *_to_string(
    struct nhdp_linkmetric_str *buf, uint32_t metric);

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

  .can_disable = false,
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

struct olsr_class_listener _link_listener = {
  .name = "etxff link listener",
  .class_name = NHDP_CLASS_LINK,
  .cb_add = _cb_link_added,
  .cb_change = _cb_link_changed,
  .cb_remove = _cb_link_removed,
};

/* timer for sampling incoming RFC5444 packets */
struct olsr_timer_info _sampling_timer_info = {
  .name = "Sampling timer for ETXFF-metric",
  .callback = _cb_etx_sampling,
  .periodic = true,
};

struct olsr_timer_entry _sampling_timer = {
  .info = &_sampling_timer_info,
};

/* timer class to measure interval between Hellos */
struct olsr_timer_info _hello_lost_info = {
  .name = "Hello lost timer for ETXFF-metric",
  .callback = _cb_hello_lost,
};

/* nhdp metric handler */
struct nhdp_linkmetric_handler _etxff_handler = {
  .name = "ETXFF metric handler",

  .metric_minimum = ETXFF_LINKCOST_MINIMUM,
  .metric_start = ETXFF_LINKCOST_START,
  .metric_maximum = ETXFF_LINKCOST_MAXIMUM,

  .to_string = _to_string,
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

  if (olsr_class_listener_add(&_link_listener)) {
    return -1;
  }

  if (nhdp_linkmetric_handler_add(&_etxff_handler)) {
    olsr_class_listener_remove(&_link_listener);
    return -1;
  }

  olsr_timer_add(&_sampling_timer_info);
  olsr_timer_add(&_hello_lost_info);

  _protocol = olsr_rfc5444_add_protocol(RFC5444_PROTOCOL, true);

  olsr_rfc5444_add_protocol_pktseqno(_protocol);
  rfc5444_reader_add_packet_consumer(&_protocol->reader, &_packet_consumer, NULL, 0);
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  struct nhdp_link *lnk;
  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    _cb_link_removed(lnk);
  }

  rfc5444_reader_remove_packet_consumer(&_protocol->reader, &_packet_consumer);
  olsr_rfc5444_remove_protocol_pktseqno(_protocol);
  olsr_rfc5444_remove_protocol(_protocol);

  nhdp_linkmetric_handler_remove(&_etxff_handler);

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
}

/**
 * Callback triggered when a new nhdp link is changed
 * @param ptr nhdp link
 */
static void
_cb_link_changed(void *ptr) {
  struct link_etxff_data *data;
  struct nhdp_link *lnk;

  lnk = ptr;
  data = olsr_class_get_extension(&_link_extenstion, lnk);

  if (lnk->itime_value > 0) {
    data->hello_interval = lnk->itime_value;
  }
  else {
    data->hello_interval = lnk->vtime_value;
  }

  olsr_timer_set(&data->hello_lost_timer, (data->hello_interval * 3) / 2);

  data->missed_hellos = 0;
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
  struct link_etxff_data *ldata;
  struct nhdp_neighbor *neigh;
  struct nhdp_link *lnk;
  uint32_t total, received;
  uint64_t metric;
  int i;

  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    ldata = olsr_class_get_extension(&_link_extenstion, lnk);

    if (ldata->activePtr == -1) {
      /* still no data for this link */
      continue;
    }

    /* initialize counter */
    total = 0;
    received = 0;

    /* enlarge windows size if we are still in quickstart phase */
    if (ldata->window_size < _etxff_config.window) {
      ldata->window_size++;
    }

    /* calculate ETX */
    for (i=0; i<ldata->window_size; i++) {
      received += ldata->buckets[i].received;
      total += ldata->buckets[i].total;
    }

    if (ldata->missed_hellos > 0) {
      total += (total * ldata->missed_hellos * ldata->hello_interval) /
          (_etxff_config.interval * _etxff_config.window);
    }

    /* calculate MIN(MIN * total / received, MAX) */
    if (received * (ETXFF_LINKCOST_MAXIMUM/ETXFF_LINKCOST_MINIMUM) < total) {
      metric = ETXFF_LINKCOST_MAXIMUM;
    }
    else {
      metric = (ETXFF_LINKCOST_MINIMUM * total) / received;
    }

    /* convert into incoming metric value */
    if (metric > RFC5444_METRIC_MAX) {
      /* metric overflow */
      metric = RFC5444_METRIC_MAX;
    }

    /* convert into something that can be transmitted over the network */
    metric = rfc5444_metric_encode(metric);
    metric = rfc5444_metric_decode(metric);

    lnk->_metric[_etxff_handler._index].incoming = (uint32_t)metric;

    OLSR_DEBUG(LOG_PLUGINS, "New sampling rate: %d/%d = %" PRIu64 " (w=%d)\n",
        received, total, metric, ldata->window_size);


    /* update rolling buffer */
    ldata->activePtr++;
    if (ldata->activePtr >= _etxff_config.window) {
      ldata->activePtr = 0;
    }
    ldata->buckets[ldata->activePtr].received = 0;
    ldata->buckets[ldata->activePtr].total = 0;
  }

  /* update neighbor metrics */
  list_for_each_element(&nhdp_neigh_list, neigh, _node) {
    nhdp_linkmetric_calculate_neighbor_metric(&_etxff_handler, neigh);
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

    olsr_timer_set(&ldata->hello_lost_timer, ldata->hello_interval);

    OLSR_DEBUG(LOG_PLUGINS, "Missed Hello: %d", ldata->missed_hellos);
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
    ldata->last_seq_nr = context->pkt_seqno;

    return RFC5444_OKAY;
  }

  total = (int)(context->pkt_seqno) - (int)(ldata->last_seq_nr);
  if (total < 0) {
    total += 65536;
  }

  if (total > 255) {
    /* most likely a restart of the pkt seqno counter */
    total = 1;
  }

  ldata->buckets[ldata->activePtr].received++;
  ldata->buckets[ldata->activePtr].total += total;
  ldata->last_seq_nr = context->pkt_seqno;

  return RFC5444_OKAY;
}

/**
 * Convert ETX-ff metric into string representation
 * @param buf pointer to output buffer
 * @param metric metric value
 * @return pointer to output string
 */
static const char *
_to_string(struct nhdp_linkmetric_str *buf, uint32_t metric) {
  uint32_t frac;

  frac = metric % ETXFF_LINKCOST_MINIMUM;
  frac *= 1000;
  frac /= ETXFF_LINKCOST_MINIMUM;
  snprintf(buf->buf, sizeof(*buf), "%u.%03u",
      metric / ETXFF_LINKCOST_MINIMUM, frac);

  return buf->buf;
}

/**
 * Callback triggered when configuration changes
 */
static void
_cb_cfg_changed(void) {
  bool first;

  first = _etxff_config.window == 0;

  if (cfg_schema_tobin(&_etxff_config, _etxff_section.post,
      _etxff_entries, ARRAYSIZE(_etxff_entries))) {
    OLSR_WARN(LOG_PLUGINS, "Cannot convert configuration for %s",
        OLSR_PLUGIN7_GET_NAME());
    return;
  }

  if (first) {
    _link_extenstion.size +=
        sizeof(struct link_etxff_bucket) * _etxff_config.window;

    if (olsr_class_extend(&_link_extenstion)) {
      return;
    }
  }

  /* start/change sampling timer */
  olsr_timer_set(&_sampling_timer, _etxff_config.interval);
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

  /* clear temporary buffer */
  memset(&cfg, 0, sizeof(cfg));

  /* convert configuration to binary */
  if (cfg_schema_tobin(&cfg, named,
      _etxff_entries, ARRAYSIZE(_etxff_entries))) {
    cfg_append_printable_line(out, "Could not parse hysteresis configuration in section %s",
        section_name);
    return -1;
  }

  if (_etxff_config.window != 0 && cfg.window != _etxff_config.window) {
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
