
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

#include <errno.h>
#include <stdio.h>

#include "common/common_types.h"
#include "common/autobuf.h"
#include "rfc5444/rfc5444_iana.h"
#include "rfc5444/rfc5444.h"
#include "rfc5444/rfc5444_reader.h"

#include "core/oonf_cfg.h"
#include "core/oonf_logging.h"
#include "core/oonf_plugins.h"
#include "subsystems/oonf_class.h"
#include "subsystems/oonf_layer2.h"
#include "subsystems/oonf_linkconfig.h"
#include "subsystems/oonf_rfc5444.h"
#include "subsystems/oonf_packet_socket.h"
#include "subsystems/oonf_timer.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_domain.h"
#include "nhdp/nhdp_interfaces.h"

#include "ff_ett/ff_ett.h"

/* definitions and constants */
enum {
  ETTFF_LINKSPEED_MINIMUM = 1024 * 1024,
  ETTFF_LINKSPEED_MAXIMUM = ETTFF_LINKSPEED_MINIMUM * 256,

  ETTFF_ETXCOST_MINIMUM   = NHDP_METRIC_DEFAULT / 16,
  ETTFF_ETXCOST_MAXIMUM   = NHDP_METRIC_DEFAULT,

  ETTFF_LINKCOST_START    = NHDP_METRIC_DEFAULT,
  ETTFF_LINKCOST_MINIMUM  =
      ETTFF_ETXCOST_MINIMUM *
      (ETTFF_LINKSPEED_MAXIMUM / ETTFF_LINKSPEED_MINIMUM),
  ETTFF_LINKCOST_MAXIMUM  = ETTFF_ETXCOST_MAXIMUM,
};

/* Configuration settings of ETTFF Metric */
struct _config {
  /* Interval between two updates of the metric */
  uint64_t interval;

  /* length of history in 'interval sized' memory cells */
  int window;

  /* length of history window when a new link starts */
  int start_window;
};

/* a single history memory cell */
struct link_ettff_bucket {
  /* number of RFC5444 packets received in time interval */
  int received;

  /* sum of received and lost RFC5444 packets in time interval */
  int total;
};

/* Additional data for a nhdp_link for metric calculation */
struct link_ettff_data {
  /* current position in history ringbuffer */
  int activePtr;

  /* number of missed hellos based on timeouts since last received packet */
  int missed_hellos;

  /* current window size for this link */
  uint16_t window_size;

  /* last received packet sequence number */
  uint16_t last_seq_nr;

  /* timer for measuring lost hellos when no further packets are received */
  struct oonf_timer_entry hello_lost_timer;

  /* last known hello interval */
  uint64_t hello_interval;

  /* history ringbuffer */
  struct link_ettff_bucket buckets[0];
};

/* prototypes */
static int _init(void);
static void _cleanup(void);

static void _cb_link_added(void *);
static void _cb_link_changed(void *);
static void _cb_link_removed(void *);

static void _cb_ett_sampling(void *);
static void _cb_hello_lost(void *);

static enum rfc5444_result _cb_process_packet(
      struct rfc5444_reader_tlvblock_context *context);

static const char *_to_string(
    struct nhdp_metric_str *buf, uint32_t metric);

static int _cb_cfg_validate(const char *section_name,
    struct cfg_named_section *, struct autobuf *);
static void _cb_cfg_changed(void);

/* plugin declaration */
static struct cfg_schema_entry _ettff_entries[] = {
  CFG_MAP_CLOCK_MIN(_config, interval, "interval", "1.0",
      "Time interval between recalculations of metric", 100),
  CFG_MAP_INT_MINMAX(_config, window, "window", "64",
      "Number of intervals to calculate average ETT", 2, 65535),
  CFG_MAP_INT_MINMAX(_config, start_window, "start_window", "4",
      "Window sized used during startup, will be increased by 1"
      " for each interval. Smaller values allow quicker initial"
      " rise of metric value, it cannot be larger than the normal"
      " windows size.",
      1, 65535),
};

static struct cfg_schema_section _ettff_section = {
  .type = OONF_PLUGIN_GET_NAME(),
  .cb_validate = _cb_cfg_validate,
  .cb_delta_handler = _cb_cfg_changed,
  .entries = _ettff_entries,
  .entry_count = ARRAYSIZE(_ettff_entries),
};

static struct _config _ettff_config = { 0,0,0 };

struct oonf_subsystem olsrv2_ffett_subsystem = {
  .name = OONF_PLUGIN_GET_NAME(),
  .descr = "OONFD2 Funkfeuer ETT plugin",
  .author = "Henning Rogge",

  .cfg_section = &_ettff_section,

  .init = _init,
  .cleanup = _cleanup,
};
DECLARE_OONF_PLUGIN(olsrv2_ffett_subsystem);

/* RFC5444 packet listener */
static struct oonf_rfc5444_protocol *_protocol;

static struct rfc5444_reader_tlvblock_consumer _packet_consumer = {
  .order = RFC5444_LQ_PARSER_PRIORITY,
  .default_msg_consumer = true,
  .start_callback = _cb_process_packet,
};

/* storage extension and listeners */
static struct oonf_class_extension _link_extenstion = {
  .name = "ettff linkmetric",
  .class_name = NHDP_CLASS_LINK,
  .size = sizeof(struct link_ettff_data),

  .cb_add = _cb_link_added,
  .cb_change = _cb_link_changed,
  .cb_remove = _cb_link_removed,
};

/* timer for sampling in RFC5444 packets */
static struct oonf_timer_info _sampling_timer_info = {
  .name = "Sampling timer for ETTFF-metric",
  .callback = _cb_ett_sampling,
  .periodic = true,
};

static struct oonf_timer_entry _sampling_timer = {
  .info = &_sampling_timer_info,
};

/* timer class to measure interval between Hellos */
static struct oonf_timer_info _hello_lost_info = {
  .name = "Hello lost timer for ETTFF-metric",
  .callback = _cb_hello_lost,
};

/* nhdp metric handler */
static struct nhdp_domain_metric _ettff_handler = {
  .name = OONF_PLUGIN_GET_NAME(),

  .metric_minimum = ETTFF_LINKCOST_MINIMUM,
  .metric_maximum = ETTFF_LINKCOST_MAXIMUM,

  .incoming_link_start = ETTFF_LINKCOST_START,

  .to_string = _to_string,
};

/**
 * Initialize plugin
 * @return -1 if an error happened, 0 otherwise
 */
static int
_init(void) {
  if (nhdp_domain_metric_add(&_ettff_handler)) {
    return -1;
  }

  oonf_timer_add(&_sampling_timer_info);
  oonf_timer_add(&_hello_lost_info);

  _protocol = oonf_rfc5444_add_protocol(RFC5444_PROTOCOL, true);

  oonf_rfc5444_add_protocol_pktseqno(_protocol);
  rfc5444_reader_add_packet_consumer(&_protocol->reader, &_packet_consumer, NULL, 0);
  return 0;
}

/**
 * Cleanup plugin
 */
static void
_cleanup(void) {
  struct nhdp_link *lnk;
  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    _cb_link_removed(lnk);
  }

  rfc5444_reader_remove_packet_consumer(&_protocol->reader, &_packet_consumer);
  oonf_rfc5444_remove_protocol_pktseqno(_protocol);
  oonf_rfc5444_remove_protocol(_protocol);

  nhdp_domain_metric_remove(&_ettff_handler);

  oonf_class_extension_remove(&_link_extenstion);

  oonf_timer_stop(&_sampling_timer);

  oonf_timer_remove(&_sampling_timer_info);
  oonf_timer_remove(&_hello_lost_info);
}

/**
 * Callback triggered when a new nhdp link is added
 * @param ptr nhdp link
 */
static void
_cb_link_added(void *ptr) {
  struct link_ettff_data *data;
  struct nhdp_link *lnk;
  int i;

  lnk = ptr;
  data = oonf_class_get_extension(&_link_extenstion, lnk);

  memset(data, 0, sizeof(*data));
  data->window_size= _ettff_config.start_window;
  data->activePtr = -1;

  for (i = 0; i<_ettff_config.window; i++) {
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
  struct link_ettff_data *data;
  struct nhdp_link *lnk;

  lnk = ptr;
  data = oonf_class_get_extension(&_link_extenstion, lnk);

  if (lnk->itime_value > 0) {
    data->hello_interval = lnk->itime_value;
  }
  else {
    data->hello_interval = lnk->vtime_value;
  }

  oonf_timer_set(&data->hello_lost_timer, (data->hello_interval * 3) / 2);

  data->missed_hellos = 0;
}

/**
 * Callback triggered when a nhdp link is removed from the database
 * @param ptr nhdp link
 */
static void
_cb_link_removed(void *ptr) {
  struct link_ettff_data *data;

  data = oonf_class_get_extension(&_link_extenstion, ptr);

  oonf_timer_stop(&data->hello_lost_timer);
}

static uint64_t
_get_linkspeed(struct nhdp_link *lnk) {
  const struct oonf_linkconfig_data *linkdata;
  struct oonf_interface_data *ifdata;
  struct oonf_layer2_neighbor *l2neigh;

#ifdef OONF_LOG_DEBUG_INFO
  struct netaddr_str nbuf;
#endif

  OONF_DEBUG(LOG_FF_ETT, "Query linkspeed for link %s",
      netaddr_to_string(&nbuf, &lnk->if_addr));

  /* look for link configuration with originator address */
  linkdata = oonf_linkconfig_get(
      nhdp_interface_get_name(lnk->local_if), &lnk->neigh->originator);
  if (linkdata != NULL
      && linkdata->tx_bitrate != oonf_linkconfig_default.tx_bitrate) {
    OONF_DEBUG(LOG_FF_ETT, "Found IP configured linkspeed");
    return linkdata->tx_bitrate;
  }

  if (lnk->dualstack_partner) {
    linkdata = oonf_linkconfig_get(
        nhdp_interface_get_name(lnk->local_if),
        &lnk->dualstack_partner->neigh->originator);
    if (linkdata != NULL
        && linkdata->tx_bitrate != oonf_linkconfig_default.tx_bitrate) {
      OONF_DEBUG(LOG_FF_ETT, "Found IP configured linkspeed");
      return linkdata->tx_bitrate;
    }
  }

  /* if not found, try remote mac address */
  linkdata = oonf_linkconfig_get(
      nhdp_interface_get_name(lnk->local_if), &lnk->remote_mac);
  if (linkdata != NULL
      && linkdata->tx_bitrate != oonf_linkconfig_default.tx_bitrate) {
    OONF_DEBUG(LOG_FF_ETT, "Found MAC configured linkspeed");
    return linkdata->tx_bitrate;
  }

  /* get local interface data  */
  ifdata = oonf_interface_get_data(nhdp_interface_get_name(lnk->local_if), NULL);
  if (!ifdata) {
    return 0;
  }

  /* query layer2 database about neighbor */
  l2neigh = oonf_layer2_get_neighbor(&ifdata->mac, &lnk->remote_mac);
  if (l2neigh == NULL
          || !oonf_layer2_neighbor_has_tx_bitrate(l2neigh)) {
    return 0;
  }

  /* use linkspeed from measurement */
  OONF_DEBUG(LOG_FF_ETT, "Found layer2 linkspeed");
  return l2neigh->tx_bitrate;
}

/**
 * Timer callback to sample new ETT values into bucket
 * @param ptr nhdp link
 */
static void
_cb_ett_sampling(void *ptr __attribute__((unused))) {
  struct link_ettff_data *ldata;
  struct nhdp_link *lnk;
  uint32_t total, received;
  uint64_t metric;
  uint64_t tx_bitrate;
  int i;

#ifdef OONF_LOG_DEBUG_INFO
  struct nhdp_laddr *laddr;
  struct netaddr_str buf;
#endif

  OONF_DEBUG(LOG_FF_ETT, "Calculate ETT from sampled data");

  if (!_ettff_handler.domain) {
    /* metric not used */
    return;
  }

  list_for_each_element(&nhdp_link_list, lnk, _global_node) {
    ldata = oonf_class_get_extension(&_link_extenstion, lnk);

    if (ldata->activePtr == -1) {
      /* still no data for this link */
      continue;
    }

    /* initialize counter */
    total = 0;
    received = 0;

    /* enlarge windows size if we are still in quickstart phase */
    if (ldata->window_size < _ettff_config.window) {
      ldata->window_size++;
    }

    /* calculate ETT */
    for (i=0; i<ldata->window_size; i++) {
      received += ldata->buckets[i].received;
      total += ldata->buckets[i].total;
    }

    if (ldata->missed_hellos > 0) {
      total += (total * ldata->missed_hellos * ldata->hello_interval) /
          (_ettff_config.interval * _ettff_config.window);
    }

    /* calculate MIN(MIN * total / received, MAX) */
    if (received * (ETTFF_ETXCOST_MAXIMUM/ETTFF_ETXCOST_MINIMUM) < total) {
      metric = ETTFF_ETXCOST_MAXIMUM;
    }
    else {
      metric = (ETTFF_ETXCOST_MINIMUM * total) / received;
    }

    /* get link speed */
    tx_bitrate = _get_linkspeed(lnk);

    /* apply linkspeed to metric */
    if (tx_bitrate > ETTFF_LINKSPEED_MAXIMUM) {
      metric /= (ETTFF_LINKSPEED_MAXIMUM / ETTFF_LINKSPEED_MINIMUM);
    }
    else if (tx_bitrate > ETTFF_LINKSPEED_MINIMUM) {
      metric /= (tx_bitrate / ETTFF_LINKSPEED_MINIMUM);
    }

    /* convert into something that can be transmitted over the network */
    metric = rfc5444_metric_encode(metric);
    metric = rfc5444_metric_decode(metric);

    nhdp_domain_set_incoming_metric(_ettff_handler.domain, lnk, metric);

    OONF_DEBUG(LOG_FF_ETT, "New sampling rate for link %s (%s):"
        " %d/%d = %" PRIu64 " (w=%d, speed=%"PRIu64 ")\n",
        netaddr_to_string(&buf, &avl_first_element(&lnk->_addresses, laddr, _link_node)->link_addr),
        nhdp_interface_get_name(lnk->local_if),
        received, total, metric, ldata->window_size, tx_bitrate);

    /* update rolling buffer */
    ldata->activePtr++;
    if (ldata->activePtr >= _ettff_config.window) {
      ldata->activePtr = 0;
    }
    ldata->buckets[ldata->activePtr].received = 0;
    ldata->buckets[ldata->activePtr].total = 0;
  }

  /* update neighbor metrics */
  nhdp_domain_neighborhood_changed();
}

/**
 * Callback triggered when the next hellos should have been received
 * @param ptr nhdp link
 */
static void
_cb_hello_lost(void *ptr) {
  struct link_ettff_data *ldata;
  struct nhdp_link *lnk;

  lnk = ptr;
  ldata = oonf_class_get_extension(&_link_extenstion, lnk);

  if (ldata->activePtr != -1) {
    ldata->missed_hellos++;

    oonf_timer_set(&ldata->hello_lost_timer, ldata->hello_interval);

    OONF_DEBUG(LOG_FF_ETT, "Missed Hello: %d", ldata->missed_hellos);
  }
}

/**
 * Callback to process all in RFC5444 packets for metric calculation. The
 * Callback ignores all unicast packets.
 * @param consumer
 * @param context
 * @return
 */
static enum rfc5444_result
_cb_process_packet(struct rfc5444_reader_tlvblock_context *context) {
  struct link_ettff_data *ldata;
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

    OONF_WARN(LOG_FF_ETT, "Neighbor %s does not send packet sequence numbers, cannot collect ettff data!",
        netaddr_socket_to_string(&buf, _protocol->input_socket));
    return RFC5444_OKAY;
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

  /* get link and its ett data */
  lnk = laddr->link;
  ldata = oonf_class_get_extension(&_link_extenstion, lnk);

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
 * Convert ETT-ff metric into string representation
 * @param buf pointer to output buffer
 * @param metric metric value
 * @return pointer to output string
 */
static const char *
_to_string(struct nhdp_metric_str *buf, uint32_t metric) {
  uint32_t frac;

  frac = metric % ETTFF_LINKCOST_MINIMUM;
  frac *= 1000;
  frac /= ETTFF_LINKCOST_MINIMUM;
  snprintf(buf->buf, sizeof(*buf), "%u.%03u",
      metric / ETTFF_LINKCOST_MINIMUM, frac);

  return buf->buf;
}

/**
 * Callback triggered when configuration changes
 */
static void
_cb_cfg_changed(void) {
  bool first;

  first = _ettff_config.window == 0;

  if (cfg_schema_tobin(&_ettff_config, _ettff_section.post,
      _ettff_entries, ARRAYSIZE(_ettff_entries))) {
    OONF_WARN(LOG_FF_ETT, "Cannot convert configuration for %s",
        OONF_PLUGIN_GET_NAME());
    return;
  }

  if (first) {
    _link_extenstion.size +=
        sizeof(struct link_ettff_bucket) * _ettff_config.window;

    if (oonf_class_extension_add(&_link_extenstion)) {
      return;
    }
  }

  /* start/change sampling timer */
  oonf_timer_set(&_sampling_timer, _ettff_config.interval);
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
      _ettff_entries, ARRAYSIZE(_ettff_entries))) {
    cfg_append_printable_line(out, "Could not parse hysteresis configuration in section %s",
        section_name);
    return -1;
  }

  if (_ettff_config.window != 0 && cfg.window != _ettff_config.window) {
    cfg_append_printable_line(out, "%s: ETTff window cannot be changed during runtime",
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
