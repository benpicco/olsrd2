/*
 * olsr_telnet.c
 *
 *  Created on: Sep 7, 2011
 *      Author: rogge
 */

#include "common/common_types.h"

#include "config/cfg_delta.h"
#include "config/cfg_schema.h"

#include "olsr_cfg.h"
#include "olsr_logging.h"
#include "olsr_netaddr_acl.h"
#include "olsr.h"
#include "olsr_telnet.h"

static void _config_changed(void);

struct telnet_config {
  struct olsr_netaddr_acl acl;
  struct netaddr bindto_v4;
  struct netaddr bindto_v6;
  uint16_t port;
};

static struct cfg_schema_section telnet_section = {
  .t_type = "telnet",
  .t_help = "Settings for the telnet interface",
};

static struct cfg_schema_entry telnet_entries[] = {
  CFG_MAP_ACL_V46(telnet_config, acl, "127.0.0.1",
      "Access control list for telnet interface"),
  CFG_MAP_NETADDR_V4(telnet_config, bindto_v4, "0.0.0.0",
      "Bind ipv4 socket to this address", false),
  CFG_MAP_NETADDR_V6(telnet_config, bindto_v6, "::",
      "Bind ipv6 socket to this address", false),
  CFG_MAP_INT_MINMAX(telnet_config, port, "2006",
      "Network port for telnet interface", 1, 65535),
};

static struct cfg_delta_handler telnet_handler = {
  .s_type = "telnet",
  .callback = _config_changed
};

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_telnet_state);

void
olsr_telnet_init(void) {
  if (olsr_subsystem_init(&olsr_telnet_state))
    return;

  cfg_schema_add_section(olsr_cfg_get_schema(), &telnet_section);
  cfg_schema_add_entries(&telnet_section, telnet_entries, ARRAYSIZE(telnet_entries));

  cfg_delta_add_handler(olsr_cfg_get_delta(), &telnet_handler);
}

void
olsr_telnet_cleanup(void) {
  if (olsr_subsystem_cleanup(&olsr_telnet_state))
    return;

  cfg_delta_remove_handler(olsr_cfg_get_delta(), &telnet_handler);
  cfg_schema_remove_section(olsr_cfg_get_schema(), &telnet_section);
}

static void
_config_changed(void) {
  struct telnet_config config;
  struct netaddr_str buf;


  if (!telnet_handler.post)
    return;

  memset(&config, 0, sizeof(config));

  if (cfg_schema_tobin(&config, telnet_handler.post, telnet_entries, ARRAYSIZE(telnet_entries))) {
    // error
    OLSR_WARN(LOG_TELNET, "Cannot map telnet config to binary data");
    return;
  }

  OLSR_INFO(LOG_TELNET, "Testing %s: %s", netaddr_to_string(&buf, &config.bindto_v4),
      olsr_acl_check_accept(&config.acl, &config.bindto_v4) ? "true" : "false");
  OLSR_INFO(LOG_TELNET, "Testing %s: %s", netaddr_to_string(&buf, &config.bindto_v6),
      olsr_acl_check_accept(&config.acl, &config.bindto_v6) ? "true" : "false");
}
