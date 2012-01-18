
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2011, the olsr.org team - see HISTORY file
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
