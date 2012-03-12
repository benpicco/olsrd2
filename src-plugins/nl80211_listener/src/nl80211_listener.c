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

/* must be first because of a problem with linux/netlink.h */
#include <sys/socket.h>

/* and now the rest of the includes */
#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include "nl80211.h"
#include <netlink/attr.h>
#include <netlink/msg.h>

#include "common/common_types.h"
#include "common/autobuf.h"
#include "common/string.h"

#include "config/cfg_schema.h"
#include "olsr_cfg.h"
#include "olsr_clock.h"
#include "olsr_logging.h"
#include "olsr_plugins.h"
#include "olsr_timer.h"
#include "os_system.h"
#include "olsr.h"
#include "olsr_layer2.h"

/* constants */
#define _CFG_SECTION "nl80211"

/* definitions */
struct _nl80211_config {
  struct strarray interf;
  uint64_t interval;
};

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static void _cb_config_changed(void);

static void _cb_nl_message(struct nlmsghdr *hdr);
static void _cb_transmission_event(void *);

/* plugin declaration */
OLSR_PLUGIN7 {
  .descr = "OLSRD nl80211 listener plugin",
  .author = "Henning Rogge",

  .load = _cb_plugin_load,
  .unload = _cb_plugin_unload,
  .enable = _cb_plugin_enable,
  .disable = _cb_plugin_disable,

  .deactivate = true,
};

/* configuration */
static struct cfg_schema_section _nl80211_section = {
  .type = _CFG_SECTION,
  .cb_delta_handler = _cb_config_changed,
};

static struct cfg_schema_entry _nl80211_entries[] = {
  CFG_MAP_STRINGLIST(_nl80211_config, interf, "if", "wlan0", "TODO"),
  CFG_MAP_CLOCK_MIN(_nl80211_config, interval, "interval", "1.0", "TODO", 100),
};

static struct _nl80211_config _config;

/* netlink specific data */
static struct os_system_netlink _netlink_handler = {
  .cb_message = _cb_nl_message,
};

static struct olsr_timer_info _transmission_timer_info = {
  .name = "nl80211 listener timer",
  .callback = _cb_transmission_event,
  .periodic = true,
};

static struct nlmsghdr *_msgbuf;

static int _nl80211_id = -1;

/* timer for generating netlink requests */
struct olsr_timer_entry _transmission_timer = {
  .info = &_transmission_timer_info
};

/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  cfg_schema_add_section(olsr_cfg_get_schema(), &_nl80211_section,
      _nl80211_entries, ARRAYSIZE(_nl80211_entries));

  strarray_init(&_config.interf);

  _msgbuf = calloc(1, UIO_MAXIOV);
  if (_msgbuf == NULL) {
    OLSR_WARN_OOM(LOG_PLUGINS);
    return -1;
  }
  return 0;
}

/**
 * Destructor of plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_unload(void) {
  strarray_free(&_config.interf);
  free (_msgbuf);

  cfg_schema_remove_section(olsr_cfg_get_schema(), &_nl80211_section);
  return 0;
}

/**
 * Enable plugin
 * @return -1 if netlink socket could not be opened, 0 otherwise
 */
static int
_cb_plugin_enable(void) {
  if (os_system_netlink_add(&_netlink_handler, NETLINK_GENERIC, 0)) {
    return -1;
  }

  olsr_timer_add(&_transmission_timer_info);
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  olsr_timer_remove(&_transmission_timer_info);

  os_system_netlink_remove(&_netlink_handler);
  return 0;
}

static void
_parse_cmd_newfamily(struct nlmsghdr *hdr) {
  struct nlattr *attrs[CTRL_ATTR_MAX+1];
  static struct nla_policy ctrl_policy[CTRL_ATTR_MAX+1] = {
    [CTRL_ATTR_FAMILY_ID] = { .type = NLA_U16 },
    [CTRL_ATTR_FAMILY_NAME] = { .type = NLA_STRING,
              .maxlen = GENL_NAMSIZ },
    [CTRL_ATTR_VERSION] = { .type = NLA_U32 },
    [CTRL_ATTR_HDRSIZE] = { .type = NLA_U32 },
    [CTRL_ATTR_MAXATTR] = { .type = NLA_U32 },
    [CTRL_ATTR_OPS]   = { .type = NLA_NESTED },
    [CTRL_ATTR_MCAST_GROUPS] = { .type = NLA_NESTED },
  };

  if (nlmsg_parse(hdr, sizeof(struct genlmsghdr),
      attrs, CTRL_ATTR_MAX, ctrl_policy) < 0) {
    OLSR_WARN(LOG_PLUGINS, "Cannot parse netlink CTRL_CMD_NEWFAMILY message");
    return;
  }

  if (attrs[CTRL_ATTR_FAMILY_ID] == NULL) {
    OLSR_WARN(LOG_PLUGINS, "Missing Family ID in CTRL_CMD_NEWFAMILY");
    return;
  }
  if (attrs[CTRL_ATTR_FAMILY_NAME] == NULL) {
    OLSR_WARN(LOG_PLUGINS, "Missing Family Name in CTRL_CMD_NEWFAMILY");
    return;
  }

  if (strcmp(nla_get_string(attrs[CTRL_ATTR_FAMILY_NAME]), "nl80211") == 0) {
    _nl80211_id = nla_get_u32(attrs[CTRL_ATTR_FAMILY_ID]);
  }
}

static int
_parse_cmd_new_station(struct nlmsghdr *hdr, struct olsr_layer2_neighbor *neigh) {
  static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {
    [NL80211_STA_INFO_INACTIVE_TIME] = { .type = NLA_U32 },
    [NL80211_STA_INFO_RX_BYTES] = { .type = NLA_U32 },
    [NL80211_STA_INFO_TX_BYTES] = { .type = NLA_U32 },
    [NL80211_STA_INFO_RX_PACKETS] = { .type = NLA_U32 },
    [NL80211_STA_INFO_TX_PACKETS] = { .type = NLA_U32 },
    [NL80211_STA_INFO_SIGNAL] = { .type = NLA_U8 },
    [NL80211_STA_INFO_RX_BITRATE] = { .type = NLA_NESTED },
    [NL80211_STA_INFO_TX_BITRATE] = { .type = NLA_NESTED },
    [NL80211_STA_INFO_LLID] = { .type = NLA_U16 },
    [NL80211_STA_INFO_PLID] = { .type = NLA_U16 },
    [NL80211_STA_INFO_PLINK_STATE] = { .type = NLA_U8 },
    [NL80211_STA_INFO_TX_RETRIES] = { .type = NLA_U32 },
    [NL80211_STA_INFO_TX_FAILED] = { .type = NLA_U32 },
  };

  static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
    [NL80211_RATE_INFO_BITRATE] = { .type = NLA_U16 },
    [NL80211_RATE_INFO_MCS] = { .type = NLA_U8 },
    [NL80211_RATE_INFO_40_MHZ_WIDTH] = { .type = NLA_FLAG },
    [NL80211_RATE_INFO_SHORT_GI] = { .type = NLA_FLAG },
  };

  struct nlattr *tb[NL80211_ATTR_MAX + 1];
  struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
  struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];

  if (nlmsg_parse(hdr, sizeof(struct genlmsghdr),
      tb, NL80211_ATTR_MAX, NULL) < 0) {
    OLSR_WARN(LOG_PLUGINS, "Cannot parse netlink NL80211_CMD_NEW_STATION message");
    return -1;
  }

  if (!tb[NL80211_ATTR_STA_INFO]) {
    OLSR_WARN(LOG_PLUGINS, "Cannot find station info attribute");
    return -1;
  }
  if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX,
           tb[NL80211_ATTR_STA_INFO], stats_policy)) {
    OLSR_WARN(LOG_PLUGINS, "Cannot parse station info attribute");
    return -1;
  }

  neigh->_available_data = 0;

  netaddr_from_binary(&neigh->mac_address, nla_data(tb[NL80211_ATTR_MAC]), 6, AF_MAC48);
  neigh->if_index = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);

  if (sinfo[NL80211_STA_INFO_INACTIVE_TIME]) {
    neigh->_available_data |= OLSR_L2NEIGH_INACTIVE_TIME;
    neigh->last_seen =
        olsr_clock_get_absolute(nla_get_u32(sinfo[NL80211_STA_INFO_INACTIVE_TIME]));
  }
  if (sinfo[NL80211_STA_INFO_RX_BYTES]) {
    neigh->_available_data |= OLSR_L2NEIGH_RX_BYTES;
    neigh->rx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_RX_BYTES]);
  }
  if (sinfo[NL80211_STA_INFO_RX_PACKETS]) {
    neigh->_available_data |= OLSR_L2NEIGH_RX_PACKETS;
    neigh->rx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_RX_PACKETS]);
  }
  if (sinfo[NL80211_STA_INFO_TX_BYTES]) {
    neigh->_available_data |= OLSR_L2NEIGH_TX_BYTES;
    neigh->tx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_TX_BYTES]);
  }
  if (sinfo[NL80211_STA_INFO_TX_PACKETS]) {
    neigh->_available_data |= OLSR_L2NEIGH_TX_PACKETS;
    neigh->tx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_TX_PACKETS]);
  }
  if (sinfo[NL80211_STA_INFO_TX_RETRIES])  {
    neigh->_available_data |= OLSR_L2NEIGH_TX_RETRIES;
    neigh->tx_retries = nla_get_u32(sinfo[NL80211_STA_INFO_TX_RETRIES]);
  }
  if (sinfo[NL80211_STA_INFO_TX_FAILED]) {
    neigh->_available_data |= OLSR_L2NEIGH_TX_FAILED;
    neigh->tx_failed = nla_get_u32(sinfo[NL80211_STA_INFO_TX_FAILED]);
  }
  if (sinfo[NL80211_STA_INFO_SIGNAL])  {
    neigh->_available_data |= OLSR_L2NEIGH_SIGNAL;
    neigh->signal = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);
  }
  if (sinfo[NL80211_STA_INFO_TX_BITRATE]) {
    if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX,
             sinfo[NL80211_STA_INFO_TX_BITRATE], rate_policy) == 0) {
      if (rinfo[NL80211_RATE_INFO_BITRATE]) {
        uint64_t rate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
        neigh->tx_bitrate = rate << ( 7ull + 10ull );
      }
      /* TODO: do we need the rest of the data ? */
#if 0
      if (rinfo[NL80211_RATE_INFO_MCS])
        printf(" MCS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_MCS]));
      if (rinfo[NL80211_RATE_INFO_40_MHZ_WIDTH])
        printf(" 40Mhz");
      if (rinfo[NL80211_RATE_INFO_SHORT_GI])
        printf(" short GI");
#endif
    }
  }
  if (sinfo[NL80211_STA_INFO_RX_BITRATE]) {
    if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX,
             sinfo[NL80211_STA_INFO_RX_BITRATE], rate_policy) == 0) {
      if (rinfo[NL80211_RATE_INFO_BITRATE]) {
        uint64_t rate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
        neigh->rx_bitrate = rate << ( 7ull + 10ull );
      }
      /* TODO: do we need the rest of the data ? */
#if 0
      if (rinfo[NL80211_RATE_INFO_MCS])
        printf(" MCS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_MCS]));
      if (rinfo[NL80211_RATE_INFO_40_MHZ_WIDTH])
        printf(" 40Mhz");
      if (rinfo[NL80211_RATE_INFO_SHORT_GI])
        printf(" short GI");
#endif
    }
  }
  return 0;
}

#define WLAN_CAPABILITY_ESS   (1<<0)
#define WLAN_CAPABILITY_IBSS    (1<<1)
#define WLAN_CAPABILITY_CF_POLLABLE (1<<2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST (1<<3)
#define WLAN_CAPABILITY_PRIVACY   (1<<4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE  (1<<5)
#define WLAN_CAPABILITY_PBCC    (1<<6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY (1<<7)
#define WLAN_CAPABILITY_SPECTRUM_MGMT (1<<8)
#define WLAN_CAPABILITY_QOS   (1<<9)
#define WLAN_CAPABILITY_SHORT_SLOT_TIME (1<<10)
#define WLAN_CAPABILITY_APSD    (1<<11)
#define WLAN_CAPABILITY_DSSS_OFDM (1<<13)

static int
_parse_cmd_new_scan_result(struct nlmsghdr *msg, struct olsr_layer2_network *net) {
  static struct nla_policy bss_policy[NL80211_BSS_MAX + 1] = {
    [NL80211_BSS_TSF] = { .type = NLA_U64 },
    [NL80211_BSS_FREQUENCY] = { .type = NLA_U32 },
//    [NL80211_BSS_BSSID] = { },
    [NL80211_BSS_BEACON_INTERVAL] = { .type = NLA_U16 },
    [NL80211_BSS_CAPABILITY] = { .type = NLA_U16 },
//    [NL80211_BSS_INFORMATION_ELEMENTS] = { },
    [NL80211_BSS_SIGNAL_MBM] = { .type = NLA_U32 },
    [NL80211_BSS_SIGNAL_UNSPEC] = { .type = NLA_U8 },
    [NL80211_BSS_STATUS] = { .type = NLA_U32 },
    [NL80211_BSS_SEEN_MS_AGO] = { .type = NLA_U32 },
//    [NL80211_BSS_BEACON_IES] = { },
  };

  struct nlattr *tb[NL80211_ATTR_MAX + 1];
  struct nlattr *bss[NL80211_BSS_MAX + 1];

  if (nlmsg_parse(msg, sizeof(struct genlmsghdr),
      tb, NL80211_ATTR_MAX, NULL) < 0) {
    OLSR_WARN(LOG_PLUGINS, "Cannot parse netlink NL80211_CMD_NEW_SCAN_RESULT message");
    return -1;
  }

  if (!tb[NL80211_ATTR_BSS]) {
    OLSR_WARN(LOG_PLUGINS, "bss info missing!\n");
    return -1;
  }
  if (nla_parse_nested(bss, NL80211_BSS_MAX,
           tb[NL80211_ATTR_BSS],
           bss_policy)) {
    OLSR_WARN(LOG_PLUGINS, "failed to parse nested attributes!\n");
    return -1;
  }

  if (!bss[NL80211_BSS_BSSID]) {
    OLSR_WARN(LOG_PLUGINS, "No BSSID found");
    return -1;
  }

  if ((net->available_data & OLSR_L2NET_SUPPORTED_RATES) != 0) {
    free (net->supported_rates);
  }
  net->available_data = 0;

  netaddr_from_binary(&net->id, nla_data(bss[NL80211_BSS_BSSID]), 6, AF_MAC48);
  net->if_index = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);

#if 0
  if (bss[NL80211_BSS_STATUS]) {
    switch (nla_get_u32(bss[NL80211_BSS_STATUS])) {
    case NL80211_BSS_STATUS_AUTHENTICATED:
      printf(" -- authenticated");
      break;
    case NL80211_BSS_STATUS_ASSOCIATED:
      printf(" -- associated");
      break;
    case NL80211_BSS_STATUS_IBSS_JOINED:
      printf(" -- joined");
      break;
    default:
      printf(" -- unknown status: %d",
        nla_get_u32(bss[NL80211_BSS_STATUS]));
      break;
    }
  }
  printf("\n");

  if (bss[NL80211_BSS_TSF]) {
    unsigned long long tsf;
    tsf = (unsigned long long)nla_get_u64(bss[NL80211_BSS_TSF]);
    printf("\tTSF: %llu usec (%llud, %.2lld:%.2llu:%.2llu)\n",
      tsf, tsf/1000/1000/60/60/24, (tsf/1000/1000/60/60) % 24,
      (tsf/1000/1000/60) % 60, (tsf/1000/1000) % 60);
  }
#endif

  if (bss[NL80211_BSS_FREQUENCY]) {
    net->available_data |= OLSR_L2NET_FREQUENCY;
    net->frequency = nla_get_u32(bss[NL80211_BSS_FREQUENCY]) * 1000000ull;
  }
#if 0
  if (bss[NL80211_BSS_BEACON_INTERVAL])
    printf("\tbeacon interval: %d\n",
      nla_get_u16(bss[NL80211_BSS_BEACON_INTERVAL]));
  if (bss[NL80211_BSS_CAPABILITY]) {
    __u16 capa = nla_get_u16(bss[NL80211_BSS_CAPABILITY]);
    printf("\tcapability:");
    if (capa & WLAN_CAPABILITY_ESS)
      printf(" ESS");
    if (capa & WLAN_CAPABILITY_IBSS)
      printf(" IBSS");
    if (capa & WLAN_CAPABILITY_PRIVACY)
      printf(" Privacy");
    if (capa & WLAN_CAPABILITY_SHORT_PREAMBLE)
      printf(" ShortPreamble");
    if (capa & WLAN_CAPABILITY_PBCC)
      printf(" PBCC");
    if (capa & WLAN_CAPABILITY_CHANNEL_AGILITY)
      printf(" ChannelAgility");
    if (capa & WLAN_CAPABILITY_SPECTRUM_MGMT)
      printf(" SpectrumMgmt");
    if (capa & WLAN_CAPABILITY_QOS)
      printf(" QoS");
    if (capa & WLAN_CAPABILITY_SHORT_SLOT_TIME)
      printf(" ShortSlotTime");
    if (capa & WLAN_CAPABILITY_APSD)
      printf(" APSD");
    if (capa & WLAN_CAPABILITY_DSSS_OFDM)
      printf(" DSSS-OFDM");
    printf(" (0x%.4x)\n", capa);
  }
  if (bss[NL80211_BSS_SIGNAL_MBM]) {
    int s = nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]);
    printf("\tsignal: %d.%.2d dBm\n", s/100, s%100);
  }
  if (bss[NL80211_BSS_SIGNAL_UNSPEC]) {
    unsigned char s = nla_get_u8(bss[NL80211_BSS_SIGNAL_UNSPEC]);
    printf("\tsignal: %d/100\n", s);
  }
#endif
  if (bss[NL80211_BSS_SEEN_MS_AGO]) {
    net->available_data |= OLSR_L2NET_INACTIVE_TIME;
    net->last_seen = olsr_clock_get_absolute(nla_get_u32(bss[NL80211_BSS_SEEN_MS_AGO]));
  }
  if (bss[NL80211_BSS_INFORMATION_ELEMENTS] != NULL ||
      bss[NL80211_BSS_BEACON_IES] != NULL) {
    int len,i;
    uint8_t *data;
    uint8_t *rate1, *rate2;
    uint8_t rate1_count, rate2_count;

    if (bss[NL80211_BSS_INFORMATION_ELEMENTS]) {
      len = nla_len(bss[NL80211_BSS_INFORMATION_ELEMENTS]);
      data = nla_data(bss[NL80211_BSS_INFORMATION_ELEMENTS]);
    }
    else {
      len = nla_len(bss[NL80211_BSS_BEACON_IES]);
      data = nla_data(bss[NL80211_BSS_BEACON_IES]);
    }

    /* collect pointers to datarates */
    rate1_count = 0;
    rate2_count = 0;
    while (len > 0) {
      if (data[0] == 1) {
        /* supported rates */
        rate1 = &data[2];
        rate1_count = data[1];
      }
      else if (data[0] == 50) {
        /* extended supported rates */
        rate2 = &data[2];
        rate2_count = data[1];
      }
      len -= data[1] + 2;
      data += data[1] + 2;
    }

    if (rate1_count + rate2_count > 0) {
      net->supported_rates = calloc(rate1_count + rate2_count, sizeof(uint64_t));
      if (net->supported_rates) {
        net->available_data |= OLSR_L2NET_SUPPORTED_RATES;

        len = 0;
        for (i=0; i<rate1_count; i++) {
          net->supported_rates[len++] = (uint64_t)rate1[i] << 19;
        }
        for (i=0; i<rate2_count; i++) {
          net->supported_rates[len++] = (uint64_t)rate2[i] << 19;
        }
        net->rate_count = len;
      }
    }
  }
  return 0;
}

static void
_cb_nl_message(struct nlmsghdr *hdr) {
  struct genlmsghdr *gen_hdr;

  gen_hdr = NLMSG_DATA(hdr);
  if (hdr->nlmsg_type == GENL_ID_CTRL && gen_hdr->cmd == CTRL_CMD_NEWFAMILY) {
    _parse_cmd_newfamily(hdr);
    return;
  }
  if (hdr->nlmsg_type == _nl80211_id) {
    if (gen_hdr->cmd == NL80211_CMD_NEW_STATION) {
      struct olsr_layer2_neighbor neigh;

      memset(&neigh, 0, sizeof(neigh));
      _parse_cmd_new_station(hdr, &neigh);
      // TODO print function !
      return;
    }
    if (gen_hdr->cmd == NL80211_CMD_NEW_SCAN_RESULTS) {
      struct olsr_layer2_network net;

      memset(&net, 0, sizeof(net));
      _parse_cmd_new_scan_result(hdr, &net);
      // TODO print function !
      return;
    }
  }

  OLSR_WARN(LOG_PLUGINS, "Unhandled incoming netlink message type %u cmd %u\n",
      hdr->nlmsg_type, gen_hdr->cmd);
}

static void
_send_genl_getfamily(void) {
  struct genlmsghdr *hdr;

  memset(_msgbuf, 0, UIO_MAXIOV);

  /* generic netlink initialization */
  hdr = NLMSG_DATA(_msgbuf);
  _msgbuf->nlmsg_len = NLMSG_LENGTH(sizeof(*hdr));
  _msgbuf->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

  /* request nl80211 identifier */
  _msgbuf->nlmsg_type = GENL_ID_CTRL;

  hdr->cmd = CTRL_CMD_GETFAMILY;
  hdr->version = 1;

  os_system_netlink_send(&_netlink_handler, _msgbuf);
}

static void
_send_nl80211_get_station_dump(int if_idx) {
  struct genlmsghdr *hdr;

  memset(_msgbuf, 0, UIO_MAXIOV);

  /* generic netlink initialization */
  hdr = NLMSG_DATA(_msgbuf);
  _msgbuf->nlmsg_len = NLMSG_LENGTH(sizeof(*hdr));
  _msgbuf->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

  /* get nl80211 station dump */
  _msgbuf->nlmsg_type = _nl80211_id;
  hdr->cmd = NL80211_CMD_GET_STATION;

  /* add interface index to the request */
  os_system_netlink_addreq(_msgbuf, NL80211_ATTR_IFINDEX, &if_idx, sizeof(if_idx));

  os_system_netlink_send(&_netlink_handler, _msgbuf);
}

static void
_send_nl80211_get_scan_dump(int if_idx) {
  struct genlmsghdr *hdr;

  memset(_msgbuf, 0, UIO_MAXIOV);

  /* generic netlink initialization */
  hdr = NLMSG_DATA(_msgbuf);
  _msgbuf->nlmsg_len = NLMSG_LENGTH(sizeof(*hdr));
  _msgbuf->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

  /* get nl80211 station dump */
  _msgbuf->nlmsg_type = _nl80211_id;
  hdr->cmd = NL80211_CMD_GET_SCAN;

  /* add interface index to the request */
  os_system_netlink_addreq(_msgbuf, NL80211_ATTR_IFINDEX, &if_idx, sizeof(if_idx));

  os_system_netlink_send(&_netlink_handler, _msgbuf);
}

static void
_cb_transmission_event(void *ptr __attribute__((unused))) {
  static bool station_dump = false;
  char *interf;
  int idx;

  if (_nl80211_id == -1) {
    _send_genl_getfamily();
    return;
  }

  FOR_ALL_STRINGS(&_config.interf, interf) {
    idx = if_nametoindex(interf);
    if (station_dump) {
      _send_nl80211_get_station_dump(idx);
    }
    else {
      _send_nl80211_get_scan_dump(idx);
    }
  }

  station_dump = !station_dump;
}

/**
 * Update configuration of remotecontrol plugin
 */
static void
_cb_config_changed(void) {
  if (cfg_schema_tobin(&_config, _nl80211_section.post,
      _nl80211_entries, ARRAYSIZE(_nl80211_entries))) {
    OLSR_WARN(LOG_CONFIG, "Could not convert nl80211_listener config to bin");
    return;
  }

  /* half of them station dumps, half of them passive scans */
  olsr_timer_start(&_transmission_timer, _config.interval / 2);
}
