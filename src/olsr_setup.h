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

/* define the first line of the command line help */
#define OLSR_SETUP_HELP_HEADLINE  "Activates OLSR.org routing daemon\n"

/* define program name */
#define OLSR_SETUP_PROGRAM        "Olsrd"

enum log_source *olsr_setup_debuglevel1;

int olsr_setup_cfginit(void);
int olsr_setup_init(void);
void olsr_setup_cleanup(void);
void olsr_setup_cfgcleanup(void);

#endif /* OLSR_SETUP_H_ */
