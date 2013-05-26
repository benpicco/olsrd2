#include <sys/types.h>
#include <sys/times.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "sys/net/destiny/socket.h"
#include <signal.h>
#include "common/list.h"
#include "config/cfg_cmd.h"
#include "config/cfg_db.h"
#include "config/cfg_schema.h"
#include "core/os_clock.h"
#include "core/os_net.h"
#include "core/os_system.h"
#include "core/os_syslog.h"
#include "core/olsr_class.h"
#include "core/olsr_clock.h"
#include "core/olsr_interface.h"
#include "core/olsr_libdata.h"
#include "core/olsr_logging.h"
#include "core/olsr_packet_socket.h"
#include "core/olsr_plugins.h"
#include "core/olsr_socket.h"
#include "core/olsr_stream_socket.h"
#include "core/olsr_timer.h"
#include "olsr_setup.h"
#include "core/olsr_subsystem.h"

#include "tools/olsr_cfg.h"
#include "tools/olsr_http.h"
#include "tools/olsr_logging_cfg.h"
#include "tools/olsr_rfc5444.h"
#include "tools/olsr_telnet.h"

#include "nhdp/nhdp.h"

#include "core/os_routing.h"

static enum log_source _level_1_sources[] = { LOG_ALL };

/**
 * Mainloop of olsrd
 * @return exit code for olsrd
 */
static int mainloop() {
  uint64_t next_interval;
  int exit_code = 0;

  /* enter main loop */
  while (1) {
    /*
     * Update the global timestamp. We are using a non-wallclock timer here
     * to avoid any undesired side effects if the system clock changes.
     */
    if (olsr_clock_update()) {
      exit_code = 1;
      break;
    }

    /* Read incoming data and handle it immediately */
    if (olsr_socket_handle(0)) {
      exit_code = 1;
      break;
    }
  }

  /* wait for 500 milliseconds and process socket events */
  next_interval = olsr_clock_get_absolute(500);
  if (olsr_socket_handle(next_interval)) {
    exit_code = 1;
  }

  return exit_code;
}

void main() {

  /* initialize basic framework */
puts("olsr_logcfg_init");
  olsr_logcfg_init(_level_1_sources, ARRAYSIZE(_level_1_sources));

puts("os_syslog_init");
  os_syslog_init();
puts("olsr_class_init");
  olsr_class_init();
puts("os_clock_init");
  if (os_clock_init()) {
    goto olsrd_cleanup;
  }
puts("olsr_clock_init");
  if (olsr_clock_init()) {
    goto olsrd_cleanup;
  }
puts("olsr_timer_init");
  olsr_timer_init();
puts("olsr_socket_init");
  olsr_socket_init();
puts("olsr_packet_init");
  olsr_packet_init();
puts("olsr_stream_init");
  olsr_stream_init();

  /* activate os-specific code */
puts("os_system_init");
  if (os_system_init()) {
    goto olsrd_cleanup;
  }

puts("os_routing_init");
  if (os_routing_init()) {
    goto olsrd_cleanup;
  }

puts("os_net_init");
  if (os_net_init()) {
    goto olsrd_cleanup;
  }

  /* activate interface listening system */
puts("olsr_interface_init");
  olsr_interface_init();

  /* activate rfc5444 scheduler */
puts("olsr_rfc5444_init");
  if (olsr_rfc5444_init()) {
    goto olsrd_cleanup;
  }

puts("nhdp_init");
  if (nhdp_init()) {
    goto olsrd_cleanup;
  }

  /* activate mainloop */
puts("starting mainloop");
  mainloop();

olsrd_cleanup:
puts("ERROR - olsrd_cleanup called");
  /* free framework resources */
  olsr_rfc5444_cleanup();
  olsr_interface_cleanup();
  os_net_cleanup();
  os_routing_cleanup();
  os_system_cleanup();
  olsr_stream_cleanup();
  olsr_packet_cleanup();
  olsr_socket_cleanup();
  olsr_timer_cleanup();
  olsr_clock_cleanup();
  os_clock_cleanup();
  olsr_class_cleanup();
  os_syslog_cleanup();
  olsr_logcfg_cleanup();

  /* free configuration resources */
  olsr_cfg_cleanup();

  /* free logger resources */
  olsr_log_cleanup();
}
