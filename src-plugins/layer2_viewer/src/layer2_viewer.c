
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
#include "common/string.h"

#include "config/cfg_schema.h"
#include "olsr_cfg.h"
#include "olsr_clock.h"
#include "olsr_interface.h"
#include "olsr_layer2.h"
#include "olsr_logging.h"
#include "olsr_plugins.h"
#include "olsr_telnet.h"
#include "olsr.h"

/* constants */
#define _CFG_SECTION "layer2_viewer"

/* definitions */
struct _l2viewer_config {
  struct olsr_netaddr_acl acl;
};

struct _routing_filter {
  struct netaddr mac;
  unsigned if_index;
};

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static void _cb_config_changed(void);

static enum olsr_telnet_result _cb_handle_layer2(struct olsr_telnet_data *data);

/* plugin declaration */
OLSR_PLUGIN7 {
  .descr = "OLSRD layer2 viewer plugin",
  .author = "Henning Rogge",

  .load = _cb_plugin_load,
  .unload = _cb_plugin_unload,
  .enable = _cb_plugin_enable,
  .disable = _cb_plugin_disable,

  .deactivate = true,
};

/* configuration */
static struct cfg_schema_section _layer2_section = {
  .type = _CFG_SECTION,
  .cb_delta_handler = _cb_config_changed,
};

static struct cfg_schema_entry _layer2_entries[] = {
  CFG_MAP_ACL(_l2viewer_config, acl, "acl", "default_accept", "acl for layer2 telnet command"),
};

static struct _l2viewer_config _config;

/* telnet command */
static struct olsr_telnet_command _telnet_cmd =
  TELNET_CMD("layer2", _cb_handle_layer2,
      "\"layer2 list net\": list all connected wlan networks\n"
      "\"layer2 list neigh\": list all known wlan neighbors\n"
      "\"layer2 list neigh <if-index>\": list all known wlan"
               " neighbors on interface with specified index\n"
      "\"layer2 net\": show data of all known wlan networks\n"
      "\"layer2 net <if-index>\": show data of a wlan network\n"
      "\"layer2 neigh\": show data of all known wlan neighbors\n"
      "\"layer2 neigh <if-index>\": show data of all known wlan"
               " neighbors on specified interface\n"
      "\"layer2 neigh <ssid>\": show data of a wlan neighbor\n",
      .acl = &_config.acl);

/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  cfg_schema_add_section(olsr_cfg_get_schema(), &_layer2_section,
      _layer2_entries, ARRAYSIZE(_layer2_entries));
  return 0;
}

/**
 * Destructor of plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_unload(void) {
  cfg_schema_remove_section(olsr_cfg_get_schema(), &_layer2_section);
  return 0;
}

/**
 * Enable plugin
 * @return -1 if netlink socket could not be opened, 0 otherwise
 */
static int
_cb_plugin_enable(void) {
  olsr_telnet_add(&_telnet_cmd);
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  olsr_telnet_remove(&_telnet_cmd);
  return 0;
}

/**
 * Print the data of a layer2 network to the telnet stream
 * @param out pointer to output stream
 * @param net pointer to layer2 network data
 * @return -1 if an error happened, 0 otherwise
 */
static int
_print_network(struct autobuf *out, struct olsr_layer2_network *net) {
  struct netaddr_str netbuf;
  struct timeval_buf tvbuf;
  struct human_readable_str numbuf;

  if (0 > abuf_appendf(out,
      "Radio-ID: %s\n"
      "Active: %s\n",
      netaddr_to_string(&netbuf, &net->radio_id),
      net->active ? "true" : "false")) {
    return -1;
  }

  if (net->if_index) {
    if (0 > abuf_appendf(out, "If-Index: %u\n", net->if_index)) {
      return -1;
    }
  }

  if (olsr_layer2_network_has_ssid(net)) {
    if (0 > abuf_appendf(out, "SSID: %s\n", netaddr_to_string(&netbuf, &net->ssid))) {
      return -1;
    }
  }

  if (olsr_layer2_network_has_last_seen(net)) {
    int64_t relative;

    relative = olsr_clock_get_relative(net->last_seen);
    if (0 > abuf_appendf(out, "Last seen: %s seconds ago\n",
        olsr_clock_toIntervalString(&tvbuf, -relative))) {
      return -1;
    }
  }
  if (olsr_layer2_network_has_frequency(net)) {
    if (0 > abuf_appendf(out, "Frequency: %s\n",
        str_get_human_readable_number(&numbuf, net->frequency, "Hz", 3, false))) {
      return -1;
    }
  }
  if (olsr_layer2_network_has_supported_rates(net)) {
    size_t i;

    for (i=0; i<net->rate_count; i++) {
      if (0 > abuf_appendf(out, "Supported rate: %s\n",
          str_get_human_readable_number(&numbuf, net->supported_rates[i], "bit/s", 3, true))) {
        return -1;
      }
    }
  }
  return 0;
}

/**
 * Print the data of a layer2 neighbor to the telnet stream
 * @param out pointer to output stream
 * @param net pointer to layer2 neighbor data
 * @return -1 if an error happened, 0 otherwise
 */
static int
_print_neighbor(struct autobuf *out, struct olsr_layer2_neighbor *neigh) {
  struct netaddr_str netbuf1, netbuf2;
  struct timeval_buf tvbuf;
  struct human_readable_str numbuf;

  if (0 > abuf_appendf(out,
      "Neighbor MAC: %s\n"
      "Active: %s\n"
      "Radio Mac: %s",
      netaddr_to_string(&netbuf1, &neigh->key.neighbor_mac),
      neigh->active ? "true" : "false",
      netaddr_to_string(&netbuf2, &neigh->key.radio_mac))) {
    return -1;
  }

  if (neigh->if_index) {
    if (0 > abuf_appendf(out, " (index: %u)", neigh->if_index)) {
      return -1;
    }
  }
  if (0 > abuf_puts(out, "\n")) {
    return -1;
  }

  if (olsr_layer2_neighbor_has_last_seen(neigh)) {
    int64_t relative;

    relative = olsr_clock_get_relative(neigh->last_seen);
    if (0 > abuf_appendf(out, "Last seen: %s seconds ago\n",
        olsr_clock_toIntervalString(&tvbuf, -relative))) {
      return -1;
    }
  }

  if (olsr_layer2_neighbor_has_signal(neigh)) {
    if (0 > abuf_appendf(out, "Sginal strength: %d dBm\n", neigh->signal_dbm)) {
      return -1;
    }
  }
  if (olsr_layer2_neighbor_has_rx_bitrate(neigh)) {
    if (0 > abuf_appendf(out, "RX bitrate: %s\n",
        str_get_human_readable_number(&numbuf, neigh->rx_bitrate, "bit/s", 1, true))) {
      return -1;
    }
  }
  if (olsr_layer2_neighbor_has_rx_bytes(neigh)) {
    if (0 > abuf_appendf(out, "RX traffic: %s\n",
        str_get_human_readable_number(&numbuf, neigh->rx_bytes, "Byte", 1, true))) {
      return -1;
    }
  }
  if (olsr_layer2_neighbor_has_rx_packets(neigh)) {
    if (0 > abuf_appendf(out, "RX packets: %s\n",
        str_get_human_readable_number(&numbuf, neigh->rx_packets, "", 0, true))) {
      return -1;
    }
  }
  if (olsr_layer2_neighbor_has_tx_bitrate(neigh)) {
    if (0 > abuf_appendf(out, "TX bitrate: %s\n",
        str_get_human_readable_number(&numbuf, neigh->tx_bitrate, "bit/s", 1, true))) {
      return -1;
    }
  }
  if (olsr_layer2_neighbor_has_tx_bytes(neigh)) {
    if (0 > abuf_appendf(out, "TX traffic: %s\n",
        str_get_human_readable_number(&numbuf, neigh->tx_bytes, "Byte", 1, true))) {
      return -1;
    }
  }
  if (olsr_layer2_neighbor_has_tx_packets(neigh)) {
    if (0 > abuf_appendf(out, "TX packets: %s\n",
        str_get_human_readable_number(&numbuf, neigh->tx_packets, "", 0, true))) {
      return -1;
    }
  }
  if (olsr_layer2_neighbor_has_tx_packets(neigh)) {
    if (0 > abuf_appendf(out, "TX retries: %s\n",
        str_get_human_readable_number(&numbuf, neigh->tx_retries, "", 3, true))) {
      return -1;
    }
  }
  if (olsr_layer2_neighbor_has_tx_packets(neigh)) {
    if (0 > abuf_appendf(out, "TX failed: %s\n",
        str_get_human_readable_number(&numbuf, neigh->tx_failed, "", 3, true))) {
      return -1;
    }
  }
  return 0;
}

/**
 * Parse an input parameter which can either contain a network interface
 * name or a mac address.
 * @param filter pointer to filter to be initialized
 * @param ptr pointer to input parameter
 * @return -1 if parameter wasn't valid, 0 otherwise
 */
static int
_parse_routing_filter(struct _routing_filter *filter, const char *ptr) {
  memset(filter, 0, sizeof(*filter));
  if ((filter->if_index = if_nametoindex(ptr)) != 0) {
    return 0;
  }

  if (netaddr_from_string(&filter->mac, ptr) != 0) {
    return -1;
  }

  if (filter->mac.type != AF_MAC48) {
    filter->mac.type = AF_UNSPEC;
    return -1;
  }
  return 0;
}

/**
 * Check if a combination of mac address and interface matchs
 * a routing filter
 * @param filter pointer to routing filter to be matched
 * @param mac pointer to mac address
 * @param if_index interface index
 * @return -1 if an error happened, 0 otherwise
 */
static int
_match_routing_filter(struct _routing_filter *filter,
    struct netaddr *mac, unsigned if_index) {
  if (filter->if_index != 0 && filter->if_index != if_index) {
    return -1;
  }

  if (filter->mac.type != AF_UNSPEC &&
      netaddr_cmp(&filter->mac, mac) != 0) {
    return -1;
  }
  return 0;
}

/**
 * Implementation of 'layer2' telnet command
 * @param data pointer to telnet data
 * @return return code for telnet server
 */
static enum olsr_telnet_result
_cb_handle_layer2(struct olsr_telnet_data *data) {
  const char *next = NULL, *ptr = NULL;
  struct olsr_layer2_network *net, *net_it;
  struct olsr_layer2_neighbor *neigh, *neigh_it;
  struct netaddr_str buf1, buf2;
  struct _routing_filter filter;
  bool first = true;
  char if_buffer[IF_NAMESIZE];

  memset(&filter, 0, sizeof(filter));

  if (data->parameter == NULL || *data->parameter == 0) {
    abuf_puts(data->out, "Error, 'layer2' needs a parameter\n");
    return TELNET_RESULT_ACTIVE;
  }

  if ((next = str_hasnextword(data->parameter, "list"))) {
    if ((ptr = str_hasnextword(next, "net"))) {
      abuf_appendf(data->out, "Radio-id\tInterf.\n");
      OLSR_FOR_ALL_LAYER2_NETWORKS(net, net_it) {
        abuf_appendf(data->out, "%c%s\t%s\n",
            net->active ? ' ' : '-',
            netaddr_to_string(&buf1, &net->radio_id),
            net->if_index == 0 ? "" : if_indextoname(net->if_index, if_buffer));
      }
      return TELNET_RESULT_ACTIVE;
    }
    else if ((ptr = str_hasnextword(next, "neigh"))) {
      if (*ptr != 0 && _parse_routing_filter(&filter, ptr) != 0) {
        abuf_appendf(data->out, "Unknown parameter: %s", ptr);
        return TELNET_RESULT_ACTIVE;
      }

      abuf_appendf(data->out, "Radio-Id\tInterface\tMAC\n");
      OLSR_FOR_ALL_LAYER2_NEIGHBORS(neigh, neigh_it) {
        if (_match_routing_filter(&filter, &neigh->key.radio_mac, neigh->if_index) == 0) {
          abuf_appendf(data->out, "%s\t%s\t%s\n",
              netaddr_to_string(&buf1, &neigh->key.radio_mac),
              neigh->if_index == 0 ? "" : if_indextoname(neigh->if_index, if_buffer),
              netaddr_to_string(&buf2, &neigh->key.neighbor_mac));
        }
      }
      return TELNET_RESULT_ACTIVE;
    }
  }
  else if ((next = str_hasnextword(data->parameter, "net"))) {
    if (*next && _parse_routing_filter(&filter, next) != 0) {
      abuf_appendf(data->out, "Unknown parameter: %s", next);
      return TELNET_RESULT_ACTIVE;
    }

    OLSR_FOR_ALL_LAYER2_NETWORKS(net, net_it) {
      if (_match_routing_filter(&filter, &net->radio_id, net->if_index) == 0) {
        if (first) {
          first = false;
        }
        else {
          abuf_puts(data->out, "\n");
        }
        if (_print_network(data->out, net)) {
          return TELNET_RESULT_INTERNAL_ERROR;
        }
      }
    }
    return TELNET_RESULT_ACTIVE;
  }
  else if ((next = str_hasnextword(data->parameter, "neigh"))) {
    if (*next && _parse_routing_filter(&filter, next) != 0) {
      abuf_appendf(data->out, "Unknown parameter: %s", next);
      return TELNET_RESULT_ACTIVE;
    }

    OLSR_FOR_ALL_LAYER2_NEIGHBORS(neigh, neigh_it) {
      if (_match_routing_filter(&filter,
          &neigh->key.neighbor_mac, neigh->if_index) != 0) {
        continue;
      }

      if (first) {
        first = false;
      }
      else {
        abuf_puts(data->out, "\n");
      }
      if (_print_neighbor(data->out, neigh)) {
        return TELNET_RESULT_INTERNAL_ERROR;
      }
    }
    return TELNET_RESULT_ACTIVE;
  }
  abuf_appendf(data->out, "Error, unknown parameters for %s command: %s\n",
      data->command, data->parameter);
  return TELNET_RESULT_ACTIVE;
}

/**
 * Update configuration of layer2-viewer plugin
 */
static void
_cb_config_changed(void) {
  if (cfg_schema_tobin(&_config, _layer2_section.post,
      _layer2_entries, ARRAYSIZE(_layer2_entries))) {
    OLSR_WARN(LOG_CONFIG, "Could not convert layer2_listener config to bin");
    return;
  }
}
