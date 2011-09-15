/*
 * olsr_setup.h
 *
 *  Created on: Sep 13, 2011
 *      Author: rogge
 */

#ifndef OLSR_SETUP_H_
#define OLSR_SETUP_H_

#include "common/common_types.h"
#include "olsr_logging.h"

/* define the first/last lines of the command line help */
#define OLSR_SETUP_HELP_HEADER  "Activates OLSR.org routing daemon\n"
#define OLSR_SETUP_HELP_TRAILER ""

/* define program name */
#define OLSR_SETUP_PROGRAM        "Olsrd"

/* define trailer text to version string */
#define OLSR_SETUP_VERSION_TRAILER "Visit http://www.olsr.org\n"

/* define default configuration file, might be overwritten by cmake */
#ifndef OLSRD_GLOBAL_CONF_FILE
#define OLSRD_GLOBAL_CONF_FILE "/etc/olsrd.conf"
#endif

enum log_source *olsr_setup_debuglevel1;

int olsr_setup_cfginit(void);
int olsr_setup_init(void);
void olsr_setup_cleanup(void);
void olsr_setup_cfgcleanup(void);

#endif /* OLSR_SETUP_H_ */
