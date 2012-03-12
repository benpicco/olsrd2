
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
#include "olsr_logging_cfg.h"
#include "olsr.h"
#include "olsr_setup.h"
#include "olsr_layer2.h"

/* define the logging sources that are part of debug level 1 */
static enum log_source _level_1_sources[] = {
  LOG_MAIN,
};

static const char *_CUSTOM_LOG_NAMES[] = {
  /* add your custom logging names here */

  /* "custom-1", */
};

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_setup_state);

/**
 * Allocate resources for the user of the framework
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_setup_init(void) {
  if (olsr_subsystem_is_initialized(&_setup_state))
    return 0;

  /* add custom service setup here */
  olsr_layer2_init();

  /* no error happened */
  olsr_subsystem_init(&_setup_state);
  return 0;
}

/**
 * Cleanup all resources allocated by setup initialization
 */
void
olsr_setup_cleanup(void) {
  if (olsr_subsystem_cleanup(&_setup_state))
    return;

  /* add cleanup for custom services here */
  olsr_layer2_cleanup();
}

/**
 * @return number of logging sources for debug level 1
 */
size_t
olsr_setup_get_level1count(void) {
  return ARRAYSIZE(_level_1_sources);
}

/**
 * @return array of logging sources for debug level 1
 */
enum log_source *
olsr_setup_get_level1_logs(void) {
  return _level_1_sources;
}

/**
 * @return number of custom logging sources
 */
size_t
olsr_setup_get_logcount(void) {
  return ARRAYSIZE(_CUSTOM_LOG_NAMES);
}

/**
 * @return array of cutom logging source names
 */
const char **
olsr_setup_get_lognames(void) {
  return _CUSTOM_LOG_NAMES;
}
