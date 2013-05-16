
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

#include "common/avl.h"
#include "common/common_types.h"
#include "common/list.h"
#include "config/cfg_schema.h"
#include "core/oonf_logging.h"
#include "core/oonf_plugins.h"
#include "core/oonf_subsystem.h"
#include "subsystems/oonf_class.h"
#include "subsystems/oonf_clock.h"
#include "subsystems/oonf_interface.h"
#include "subsystems/oonf_layer2.h"
#include "subsystems/oonf_rfc5444.h"
#include "subsystems/oonf_timer.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_interfaces.h"

#include "neighbor_probing/neighbor_probing.h"

/* definitions and constants */
struct _config {
  /* Interval between two link probes */
  uint64_t interval;

  /* size of probe */
  uint16_t probe_size;

  /* only probe neighbors with layer2 data ? */
  bool only_layer2;
};

struct _probing_link_data {
  /* absolute timestamp of last check if probing is necessary */
  uint64_t last_probe_check;

  /*
   * number of bytes that had been sent to neighbor during last
   * probe check.
   */
  uint64_t last_tx_traffic;

  struct oonf_rfc5444_target *target;
};

/* prototypes */
static int _init(void);
static void _cleanup(void);
static void _cb_link_removed(void *);
static void _cb_probe_link(void *);
static void _cb_addMessageHeader(struct rfc5444_writer *writer,
    struct rfc5444_writer_message *msg);
static void _cb_addMessageTLVs(struct rfc5444_writer *);
static void _cb_cfg_changed(void);

/* plugin declaration */
static struct cfg_schema_entry _probing_entries[] = {
  CFG_MAP_CLOCK_MIN(_config, interval, "interval", "1.0",
      "Time interval between link probing", 100),
  CFG_MAP_INT_MINMAX(_config, probe_size, "size", "512",
      "Number of bytes used for neighbor probe",
      1, 1500),
  CFG_MAP_BOOL(_config, only_layer2, "only_layer2", "true",
      "Only probe link ends which have a layer2 entry in the database?"),
};

static struct cfg_schema_section _probing_section = {
  .type = OONF_PLUGIN_GET_NAME(),
  .cb_delta_handler = _cb_cfg_changed,
  .entries = _probing_entries,
  .entry_count = ARRAYSIZE(_probing_entries),
};

struct oonf_subsystem olsrv2_neighbor_probing_subsystem = {
  .name = OONF_PLUGIN_GET_NAME(),
  .descr = "OONFD2 Funkfeuer ETT plugin",
  .author = "Henning Rogge",

  .cfg_section = &_probing_section,

  .init = _init,
  .cleanup = _cleanup,
};
DECLARE_OONF_PLUGIN(olsrv2_neighbor_probing_subsystem);

struct _config _probe_config;

enum log_source LOG_PROBING = LOG_MAIN;

/* storage extension and listeners */
static struct oonf_class_extension _link_extenstion = {
  .name = "probing linkmetric",
  .class_name = NHDP_CLASS_LINK,
  .size = sizeof(struct _probing_link_data),
  .cb_remove = _cb_link_removed,
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

/* rfc5444 message handing for probing */
struct oonf_rfc5444_protocol *_protocol;
struct rfc5444_writer_message *_probing_message;

struct rfc5444_writer_content_provider _probing_msg_provider = {
  .msg_type = RFC5444_MSGTYPE_PROBING,
  .addMessageTLVs = _cb_addMessageTLVs,
};

/**
 * Initialize plugin
 * @return -1 if an error happened, 0 otherwise
 */
static int
_init(void) {
  LOG_PROBING = oonf_log_register_source(OONF_PLUGIN_GET_NAME());

  if (oonf_class_extension_add(&_link_extenstion)) {
    return -1;
  }

  _protocol = oonf_rfc5444_add_protocol(RFC5444_PROTOCOL, true);
  if (_protocol == NULL) {
    oonf_class_extension_remove(&_link_extenstion);
    return -1;
  }

  _probing_message = rfc5444_writer_register_message(
      &_protocol->writer, RFC5444_MSGTYPE_PROBING, true, 4);
  if (_probing_message == NULL) {
    oonf_rfc5444_remove_protocol(_protocol);
    oonf_class_extension_remove(&_link_extenstion);
    OONF_WARN(LOG_PROBING, "Could not register Probing message");
    return -1;
  }

  _probing_message->addMessageHeader = _cb_addMessageHeader;

  if (rfc5444_writer_register_msgcontentprovider(
      &_protocol->writer, &_probing_msg_provider, NULL, 0)) {

    OONF_WARN(LOG_PROBING, "Count not register Probing msg contentprovider");
    rfc5444_writer_unregister_message(&_protocol->writer, _probing_message);
    oonf_rfc5444_remove_protocol(_protocol);
    oonf_class_extension_remove(&_link_extenstion);
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
  rfc5444_writer_unregister_content_provider(
      &_protocol->writer, &_probing_msg_provider, NULL, 0);
  rfc5444_writer_unregister_message(
      &_protocol->writer, _probing_message);
  oonf_rfc5444_remove_protocol(_protocol);
  oonf_timer_remove(&_probe_info);
  oonf_class_extension_remove(&_link_extenstion);
}

static void
_cb_link_removed(void *ptr) {
  struct _probing_link_data *ldata;

  ldata = oonf_class_get_extension(&_link_extenstion, ptr);
  if (ldata->target) {
    oonf_rfc5444_remove_target(ldata->target);
  }
}

static void
_cb_probe_link(void *ptr __attribute__((unused))) {
  struct nhdp_link *lnk, *best_lnk;
  struct _probing_link_data *ldata, *best_ldata;
  struct nhdp_interface *ninterf;

  struct oonf_interface *interf;
  struct oonf_layer2_neighbor *l2neigh;

  uint64_t points, best_points;
  uint64_t last_tx_packets;

  struct netaddr_str nbuf;

  best_ldata = NULL;
  best_points = 0;

  OONF_DEBUG(LOG_PROBING, "Start looking for probe candidate");

  avl_for_each_element(&nhdp_interface_tree, ninterf, _node) {
    interf = nhdp_interface_get_coreif(ninterf);

    OONF_DEBUG(LOG_PROBING, "Start looking for probe candidate in interface '%s'",
        interf->data.name);

    list_for_each_element(&ninterf->_links, lnk, _if_node) {
      if (_probe_config.only_layer2) {
        /* get layer2 data */
        l2neigh = oonf_layer2_get_neighbor(&interf->data.mac, &lnk->remote_mac);
        if (l2neigh == NULL || !oonf_layer2_neighbor_has_tx_packets(l2neigh)) {
          OONF_DEBUG(LOG_PROBING, "Drop link (missing l2 data)");
          continue;
        }
      }

      /* get link extension for probing */
      ldata = oonf_class_get_extension(&_link_extenstion, lnk);

      if (_probe_config.only_layer2) {
        /* fix tx-packets */
        last_tx_packets = ldata->last_tx_traffic;
        ldata->last_tx_traffic = l2neigh->tx_packets;

        /* check if link had traffic since last probe check */
        if (last_tx_packets != l2neigh->tx_packets) {
          /* advance timestamp */
          ldata->last_probe_check = oonf_clock_getNow();
          OONF_DEBUG(LOG_PROBING, "Drop link (already traffic on");
          continue;
        }
      }

      points = oonf_clock_getNow() - ldata->last_probe_check;

      OONF_DEBUG(LOG_PROBING, "Link %s has %" PRIu64 " points",
          netaddr_to_string(&nbuf, &lnk->if_addr), points);

      if (points > best_points) {
        best_points = points;
        best_lnk = lnk;
        best_ldata = ldata;
      }
    }
  }

  if (best_ldata != NULL) {
    best_ldata->last_probe_check = oonf_clock_getNow();

    if (best_ldata->target == NULL
        && netaddr_get_address_family(&best_lnk->if_addr) != AF_UNSPEC) {
      best_ldata->target = oonf_rfc5444_add_target(
          best_lnk->local_if->rfc5444_if.interface, &best_lnk->if_addr);
    }

    if (best_ldata->target) {
      OONF_DEBUG(LOG_PROBING, "Send probing to %s",
          netaddr_to_string(&nbuf, &best_ldata->target->dst));

      oonf_rfc5444_send_if(best_ldata->target, RFC5444_MSGTYPE_PROBING);
    }
  }
}

static void
_cb_addMessageHeader(struct rfc5444_writer *writer,
    struct rfc5444_writer_message *msg) {
  rfc5444_writer_set_msg_header(writer, msg, false, false, false, false);
}

static void
_cb_addMessageTLVs(struct rfc5444_writer *writer) {
  uint8_t data[1500];

  memset(data, 0, _probe_config.probe_size);
  rfc5444_writer_add_messagetlv(writer, RFC5444_MSGTLV_PROBING, 0,
      data, _probe_config.probe_size);
}

/**
 * Callback triggered when configuration changes
 */
static void
_cb_cfg_changed(void) {
  if (cfg_schema_tobin(&_probe_config, _probing_section.post,
      _probing_entries, ARRAYSIZE(_probing_entries))) {
    OONF_WARN(LOG_PROBING, "Cannot convert configuration for %s plugin",
        OONF_PLUGIN_GET_NAME());
    return;
  }

  oonf_timer_set(&_probe_timer, _probe_config.interval);
}
