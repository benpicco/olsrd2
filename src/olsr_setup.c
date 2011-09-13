/*
 * olsr_setup.c
 *
 *  Created on: Sep 13, 2011
 *      Author: rogge
 */

#include "common/common_types.h"
#include "olsr.h"
#include "olsr_setup.h"

/* define the logging sources that are part of debug level 1 */
static enum log_source _level_1_sources[] = {
    LOG_MAIN,
    0,
};

enum log_source *olsr_setup_debuglevel1 = _level_1_sources;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_setupcfg_state);
OLSR_SUBSYSTEM_STATE(olsr_setup_state);


int
olsr_setup_cfginit(void) {
  if (olsr_subsystem_init(&olsr_setupcfg_state))
    return 0;

  /* add custom configuration setup here */


  /* no error happened */
  return 0;
}

void
olsr_setup_cfgcleanup(void) {
  if (olsr_subsystem_cleanup(&olsr_setupcfg_state))
    return;

  /* add cleanup for custom configuration setup here */
}

int
olsr_setup_init(void) {
  if (olsr_subsystem_init(&olsr_setup_state))
    return 0;

  /* add custom service setup here */


  /* no error happened */
  return 0;
}

void
olsr_setup_cleanup(void) {
  if (olsr_subsystem_cleanup(&olsr_setup_state))
    return;

  /* add cleanup for custom services here */
}

