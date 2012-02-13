/*
 * os_routing_linux.c
 *
 *  Created on: Feb 13, 2012
 *      Author: rogge
 */

#include <sys/utsname.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "common/common_types.h"
#include "olsr.h"
#include "os_routing.h"
#include "os_system.h"

/* ip forwarding */
#define PROC_IPFORWARD_V4 "/proc/sys/net/ipv4/ip_forward"
#define PROC_IPFORWARD_V6 "/proc/sys/net/ipv6/conf/all/forwarding"

/* Redirect proc entry */
#define PROC_IF_REDIRECT "/proc/sys/net/ipv4/conf/%s/send_redirects"
#define PROC_ALL_REDIRECT "/proc/sys/net/ipv4/conf/all/send_redirects"

/* IP spoof proc entry */
#define PROC_IF_SPOOF "/proc/sys/net/ipv4/conf/%s/rp_filter"
#define PROC_ALL_SPOOF "/proc/sys/net/ipv4/conf/all/rp_filter"

static bool _is_at_least_linuxkernel_2_6_31(void);
static int _os_linux_writeToProc(const char *file, char *old, char value);

/* global procfile state before initialization */
static char _original_rp_filter;
static char _original_icmp_redirect;

OLSR_SUBSYSTEM_STATE(_os_routing_state);

void
os_routing_init(void) {
  if (olsr_subsystem_init(&_os_routing_state))
    return;

  if (_os_linux_writeToProc(PROC_ALL_REDIRECT, &_original_icmp_redirect, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable ICMP redirects! "
        "You should manually ensure that ICMP redirects are disabled!");
  }

  /* check kernel version and disable global rp_filter */
  if (_is_at_least_linuxkernel_2_6_31()) {
    if (_os_linux_writeToProc(PROC_ALL_SPOOF, &_original_rp_filter, '0')) {
      OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable global rp_filter "
          "(necessary for kernel 2.6.31 and newer)! You should manually "
          "ensure that rp_filter is disabled!");
    }
  }
}

void
os_routing_cleanup(void) {
  if (olsr_subsystem_cleanup(&_os_routing_state))
    return;

  if (_original_icmp_redirect != 0
      && _os_linux_writeToProc(PROC_ALL_REDIRECT, &_original_icmp_redirect, '0') != 0) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "WARNING! Could not restore ICMP redirect flag %s to %c!",
        PROC_ALL_REDIRECT, _original_icmp_redirect);
  }

  /* check kernel version and disable global rp_filter */
  if (_os_linux_writeToProc(PROC_ALL_SPOOF, &_original_rp_filter, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "WARNING! Could not restore global rp_filter flag %s to %c!",
        PROC_ALL_SPOOF, _original_rp_filter);
  }
}

/**
 * Initialize interface for mesh usage
 * @param interf pointer to interface object
 * @return -1 if an error happened, 0 otherwise
 */
int
os_routing_init_mesh_if(struct olsr_interface *interf) {
  char procfile[FILENAME_MAX];
  char old_redirect = 0, old_spoof = 0;

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_REDIRECT, interf->name);

  if (_os_linux_writeToProc(procfile, &old_redirect, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable ICMP redirects! "
        "You should manually ensure that ICMP redirects are disabled!");
  }

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_SPOOF, interf->name);

  if (_os_linux_writeToProc(procfile, &old_spoof, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable the IP spoof filter! "
        "You should mannually ensure that IP spoof filtering is disabled!");
  }

  interf->_original_state = (old_redirect << 8) | (old_spoof);
  return 0;
}

/**
 * Cleanup interface after mesh usage
 * @param interf pointer to interface object
 */
void
os_routing_cleanup_mesh_if(struct olsr_interface *interf) {
  char restore_redirect, restore_spoof;
  char procfile[FILENAME_MAX];

  restore_redirect = (interf->_original_state >> 8) & 255;
  restore_spoof = (interf->_original_state & 255);

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_REDIRECT, interf->name);

  if (restore_redirect != 0
      && _os_linux_writeToProc(procfile, NULL, restore_redirect) != 0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Could not restore ICMP redirect flag %s to %c",
        procfile, restore_redirect);
  }

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_SPOOF, interf->name);

  if (restore_spoof != 0
      && _os_linux_writeToProc(procfile, NULL, restore_spoof) != 0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Could not restore IP spoof flag %s to %c",
        procfile, restore_spoof);
  }

  interf->_original_state = 0;
  return;
}

/**
 * Overwrite a numeric entry in the procfile system and keep the old
 * value.
 * @param file pointer to filename (including full path)
 * @param old pointer to memory to store old value
 * @param value new value
 * @return -1 if an error happened, 0 otherwise
 */
static int
_os_linux_writeToProc(const char *file, char *old, char value) {
  int fd;
  char rv;

  if ((fd = open(file, O_RDWR)) < 0) {
    goto writetoproc_error;
  }

  if (read(fd, &rv, 1) != 1) {
    goto writetoproc_error;
  }

  if (rv != value) {
    if (lseek(fd, SEEK_SET, 0) == -1) {
      goto writetoproc_error;
    }

    if (write(fd, &value, 1) != 1) {
      goto writetoproc_error;
    }

    OLSR_DEBUG(LOG_OS_SYSTEM, "Writing '%c' (was %c) to %s", value, rv, file);
  }

  if (close(fd) != 0) {
    goto writetoproc_error;
  }

  if (old && rv != value) {
    *old = rv;
  }

  return 0;

writetoproc_error:
  OLSR_WARN(LOG_OS_SYSTEM,
    "Error, cannot read proc entry %s: %s (%d)\n",
    file, strerror(errno), errno);
  return -1;
}

static bool
_is_at_least_linuxkernel_2_6_31(void) {
  struct utsname uts;
  char *next;
  int first = 0, second = 0, third = 0;

  memset(&uts, 0, sizeof(uts));
  if (uname(&uts)) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Error, could not read kernel version: %s (%d)\n",
        strerror(errno), errno);
    return false;
  }

  first = strtol(uts.release, &next, 10);
  /* check for linux 3.x */
  if (first >= 3) {
    return true;
  }

  if (*next != '.') {
    goto kernel_parse_error;
  }

  second = strtol(next+1, &next, 10);
  if (*next != '.') {
    goto kernel_parse_error;
  }

  third = strtol(next+1, NULL, 10);

  /* better or equal than linux 2.6.31 ? */
  return first == 2 && second == 6 && third >= 31;

kernel_parse_error:
  OLSR_WARN(LOG_OS_SYSTEM,
      "Error, cannot parse kernel version: %s\n", uts.release);
  return false;
}
