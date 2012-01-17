/*
 * olsr_setup.c
 *
 *  Created on: Sep 13, 2011
 *      Author: rogge
 */

#include "common/common_types.h"
#include "olsr_logging_cfg.h"
#include "olsr.h"
#include "olsr_setup.h"

/* define the logging sources that are part of debug level 1 */
static enum log_source _level_1_sources[] = {
  LOG_MAIN,
  0,
};

static const char *_CUSTOM_LOG_NAMES[] = {
  /* add your custom logging names here */

  /* "custom-1", */
};

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_setupcfg_state);
OLSR_SUBSYSTEM_STATE(olsr_setup_state);

int
olsr_setup_cfginit(void) {
  if (olsr_subsystem_init(&olsr_setupcfg_state))
    return 0;

  /* initialize logging configuration first */
  olsr_logcfg_init(_level_1_sources, ARRAYSIZE(_level_1_sources));

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

size_t
olsr_setup_get_logcount(void) {
  return ARRAYSIZE(_CUSTOM_LOG_NAMES);
}

const char **
olsr_setup_get_lognames(void) {
  return _CUSTOM_LOG_NAMES;
}
