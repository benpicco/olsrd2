/*
 * os_system_generic.c
 *
 *  Created on: Oct 15, 2011
 *      Author: henning
 */

#include <syslog.h>

#include "builddata/data.h"
#include "olsr_logging.h"
#include "os_system.h"
#include "olsr.h"

#if OS_SYSTEM_LOG == OS_GENERIC
/* we only need this subsystem state here in the generic case */
OLSR_SUBSYSTEM_STATE(_os_log_state);
#endif

#if OS_SYSTEM_INIT == OS_GENERIC
/**
 * Initialize os-specific subsystem
 * @return -1 if an error happened, 0 otherwise
 */
int
os_system_init(void) {
  return 0;
}

/**
 * Cleanup os-specific subsystem
 */
void os_system_cleanup(void) {
}
#endif

#if OS_SYSTEM_INIT_IF == OS_GENERIC
/**
 * Initialize interface for mesh usage
 * @param interf pointer to interface object
 * @return -1 if an error happened, 0 otherwise
 */
int
os_routing_init_mesh_if(
    struct olsr_interface *interf __attribute__((unused))) {
  return 0;
}

/**
 * Cleanup interface after mesh usage
 * @param interf pointer to interface object
 */
void
os_routing_cleanup_mesh_if(
    struct olsr_interface *interf __attribute__((unused))) {
}
#endif

#if OS_SYSTEM_SET_IFSTATE == OS_GENERIC
/**
 * Set interface up or down
 * @param dev pointer to name of interface
 * @param up true if interface should be up, false if down
 * @return -1 if an error happened, 0 otherwise
 */
int
os_system_set_interface_state(
    const char *dev __attribute__((unused)),
    bool up __attribute__((unused))) {
  return -1;
}
#endif

#if OS_SYSTEM_LOG == OS_GENERIC
/**
 * Initialize syslog system
 */
void
os_system_openlog(void) {
  if (olsr_subsystem_init(&_os_log_state)) {
    return;
  }

  openlog(olsr_log_get_builddata()->app_name, LOG_PID | LOG_ODELAY, LOG_DAEMON);
  setlogmask(LOG_UPTO(LOG_DEBUG));

  return;
}

/**
 * Cleanup syslog system
 */
void
os_system_closelog(void) {
  if (olsr_subsystem_cleanup(&_os_log_state)) {
    return;
  }

  closelog();
}

/**
 * Print a line to the syslog
 * @param severity severity of entry
 * @param msg line to print
 */
void
os_system_log(enum log_severity severity, const char *msg) {
  int log_sev;

  switch (severity) {
    case SEVERITY_DEBUG:
      log_sev = LOG_DEBUG;
      break;
    case SEVERITY_INFO:
      log_sev = LOG_DEBUG;
      break;
    default:
    case SEVERITY_WARN:
      log_sev = LOG_WARNING;
      break;
  }

  syslog(log_sev, "%s", msg);
}
#endif
