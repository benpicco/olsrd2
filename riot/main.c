
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2013, the olsr.org team - see HISTORY file
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

#include <sys/types.h>
#include <sys/times.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "config/cfg_cmd.h"
#include "config/cfg_db.h"
#include "config/cfg_schema.h"
#include "core/oonf_cfg.h"
#include "core/oonf_libdata.h"
#include "core/oonf_logging.h"
#include "core/oonf_logging_cfg.h"
#include "core/oonf_plugins.h"
#include "core/oonf_subsystem.h"
#include "subsystems/oonf_clock.h"
#include "subsystems/oonf_interface.h"
#include "subsystems/oonf_socket.h"

#include "app_data.h"
#include "oonf_api_subsystems.h"
#include "oonf_setup.h"

#include "compat_misc.h"

#include "vtimer.h"
#include "board.h"
#include <thread.h>

char blink_stack[MINIMUM_STACK_SIZE];

void blink_thread(void) {
	while (1) {
		LED_GREEN_TOGGLE;
		vtimer_usleep(500000);
	}
}

/* prototypes */
static bool _cb_stop_scheduler(void);
static int mainloop(int argc, char **argv);
static int display_schema(void);

static bool _end_oonf_signal, _display_schema, _debug_early, _ignore_unknown;
static char *_schema_name;

enum argv_short_options {
  argv_option_schema = 256,
  argv_option_debug_early,
  argv_option_ignore_unknown,
};

/**
 * Main program
 */
int
main(int argc, char **argv) {
  uint64_t next_interval;
  size_t i;
  size_t initialized;
  int return_code;

  struct oonf_subsystem **subsystems;
  size_t subsystem_count;

  /* early initialization */
  return_code = 1;
  initialized = 0;

  _schema_name = NULL;
  _display_schema = false;
  _debug_early = true;
  _ignore_unknown = false;

  (void) thread_create(blink_stack, sizeof blink_stack, PRIORITY_MAIN-1, CREATE_STACKTEST, blink_thread, "blink");

  /* assemble list of subsystems first */
  subsystem_count = get_used_api_subsystem_count()
      + oonf_setup_get_subsystem_count();

  subsystems = my_calloc(subsystem_count, sizeof(struct oonf_subsystem *));
  if(!subsystems) {
    fprintf(stderr, "Out of memory error for subsystem array\n");
    return -1;
  }

  memcpy(&subsystems[0], used_api_subsystems,
      sizeof(struct oonf_subsystem *) * get_used_api_subsystem_count());
  memcpy(&subsystems[get_used_api_subsystem_count()],
      oonf_setup_get_subsystems(),
      sizeof(struct oonf_subsystem *) * oonf_setup_get_subsystem_count());

  /* initialize logger */
  if (oonf_log_init(oonf_appdata_get(), LOG_SEVERITY_DEBUG)) {
    goto olsrd_cleanup;
  }

  /* prepare plugin initialization */
  oonf_plugins_init();

  /* initialize configuration system */
  if (oonf_cfg_init(argc, argv)) {
    goto olsrd_cleanup;
  }

  /* add custom configuration definitions */
  oonf_logcfg_init();

  /* add configuration options for subsystems */
  for (i=0; i<subsystem_count; i++) {
    oonf_subsystem_configure(oonf_cfg_get_schema(), subsystems[i]);
  }

  /* add interface */
  if (cfg_db_add_namedsection(oonf_cfg_get_rawdb(), CFG_INTERFACE_SECTION, "if0") == NULL) {
	OONF_WARN(LOG_MAIN, "Cannot add interface");
  }

  if (cfg_cmd_handle_set(oonf_cfg_get_instance(), oonf_cfg_get_rawdb(), "log.debug=all", NULL)) {
	OONF_WARN(LOG_MAIN, "Cannot set log level to debug");
  }

  OONF_DEBUG(LOG_MAIN, "log level set to DEBUG=all");

  /* prepare for an error during initialization */
  return_code = 1;

  /* read global section early */
  if (oonf_cfg_update_globalcfg(true)) {
    OONF_WARN(LOG_MAIN, "Cannot read global configuration section");
    goto olsrd_cleanup;
  }

  /* configure logger */
  if (oonf_logcfg_apply(oonf_cfg_get_rawdb())) {
    goto olsrd_cleanup;
  }

  /* load plugins */
  if (oonf_cfg_loadplugins()) {
    goto olsrd_cleanup;
  }

  /* show schema if necessary */
  if (_display_schema) {
    return_code = display_schema();
    goto olsrd_cleanup;
  }

  /* initialize framework */
  for (initialized=0; initialized<subsystem_count; initialized++) {
    if (subsystems[initialized]->init != NULL) {
      if (subsystems[initialized]->init()) {
        OONF_WARN(LOG_MAIN, "Could not initialize '%s' submodule",
            subsystems[initialized]->name);
        goto olsrd_cleanup;
      }
    }
  }

  /* call initialization callbacks of dynamic plugins */
  oonf_cfg_initplugins();

  /* apply configuration */
  if (oonf_cfg_apply()) {
    goto olsrd_cleanup;
  }

  if (!oonf_cfg_is_running()) {
    /*
     * mayor error during late initialization
     * or maybe the user decided otherwise and pressed CTRL-C
     */
    return_code = _end_oonf_signal ? 0 : 1;
    goto olsrd_cleanup;
  }

  /* activate mainloop */
  return_code = mainloop(argc, argv);

  /* tell framework shutdown is in progress */
  for (i=0; i<subsystem_count; i++) {
    if (subsystems[i]->initiate_shutdown != NULL) {
      subsystems[i]->initiate_shutdown();
    }
  }

  /* wait for 500 milliseconds and process socket events */
  if (oonf_clock_update()) {
    OONF_WARN(LOG_MAIN, "Clock update for shutdown failed");
  }
  next_interval = oonf_clock_get_absolute(500);
  if (oonf_socket_handle(NULL, next_interval)) {
    OONF_WARN(LOG_MAIN, "Grace period for shutdown failed.");
  }

olsrd_cleanup:
  /* free plugins */
  oonf_cfg_unconfigure_plugins();
  oonf_plugins_cleanup();

  /* cleanup framework */
  while (initialized-- > 0) {
    if (subsystems[initialized]->cleanup != NULL) {
      subsystems[initialized]->cleanup();
    }
  }

  /* free logging/config bridge resources */
  oonf_logcfg_cleanup();

  /* free configuration resources */
  oonf_cfg_cleanup();

  /* free logger resources */
  oonf_log_cleanup();

  free (subsystems);
  return return_code;
}

/**
 * Mainloop of olsrd
 * @return exit code for olsrd
 */
static int
mainloop(int argc, char **argv) {
  int exit_code = 0;

  OONF_INFO(LOG_MAIN, "Starting %s", oonf_appdata_get()->app_name);

  /* enter main loop */
  while (oonf_cfg_is_running()) {
    /*
     * Update the global timestamp. We are using a non-wallclock timer here
     * to avoid any undesired side effects if the system clock changes.
     */
    if (oonf_clock_update()) {
      exit_code = 1;
      break;
    }

    /* Read incoming data and handle it immediately */
    if (oonf_socket_handle(_cb_stop_scheduler, 0)) {
      exit_code = 1;
      break;
    }

    /* reload configuration if triggered */
    if (oonf_cfg_is_reload_set()) {
      OONF_INFO(LOG_MAIN, "Reloading configuration");
      if (oonf_cfg_clear_rawdb()) {
        break;
      }
    }

    /* commit config if triggered */
    if (oonf_cfg_is_commit_set()) {
      OONF_INFO(LOG_MAIN, "Commiting configuration");
      if (oonf_cfg_apply()) {
        break;
      }
    }
  }

  OONF_INFO(LOG_MAIN, "Ending %s", oonf_appdata_get()->app_name);
  return exit_code;
}

/**
 * Callback for the scheduler that tells it when to return to the mainloop.
 * @return true if scheduler should return to the mainloop now
 */
static bool
_cb_stop_scheduler(void) {
  return oonf_cfg_is_commit_set()
      || oonf_cfg_is_reload_set()
      || !oonf_cfg_is_running();
}

/**
 * Call the handle_schema command to give the user the schema of
 * the configuration including plugins
 * @return -1 if an error happened, 0 otherwise
 */
static int
display_schema(void) {
  struct autobuf log;
  int return_code;

  return_code = 0;

  abuf_init(&log);
  cfg_cmd_clear_state(oonf_cfg_get_instance());

  if (cfg_cmd_handle_schema(oonf_cfg_get_rawdb(), _schema_name, &log)) {
    return_code = -1;
  }

  if (abuf_getlen(&log) > 0) {
    fputs(abuf_getptr(&log), stdout);
  }

  abuf_free(&log);

  return return_code;
}
