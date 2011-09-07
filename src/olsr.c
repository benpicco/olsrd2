
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

#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <strings.h>
#include <sys/socket.h>
#include <net/if.h>

#include "common/daemonize.h"
#include "common/list.h"
#include "config/cfg_cmd.h"
#include "config/cfg_db.h"
#include "config/cfg_schema.h"
#include "builddata/plugin_static.h"
#include "builddata/version.h"
#include "builddata/data.h"
#include "olsr_cfg.h"
#include "olsr_clock.h"
#include "olsr_logging.h"
#include "olsr_logging_cfg.h"
#include "olsr_memcookie.h"
#include "olsr_packet_socket.h"
#include "olsr_plugins.h"
#include "olsr_socket.h"
#include "olsr_stream_socket.h"
#include "olsr_telnet.h"
#include "olsr_timer.h"
#include "olsr.h"

static bool running, reload_config, exit_called;
static char *schema_name;

enum argv_short_options {
  argv_option_schema = 256,
};

static struct option olsr_options[] = {
#if !defined(REMOVE_HELPTEXT)
  { "help",         no_argument,       0, 'h' },
#endif
  { "version",      no_argument,       0, 'v' },
  { "plugin",       required_argument, 0, 'p' },
  { "load",         required_argument, 0, 'l' },
  { "save",         required_argument, 0, 'S' },
  { "set",          required_argument, 0, 's' },
  { "remove",       required_argument, 0, 'r' },
  { "get",          optional_argument, 0, 'g' },
  { "format",       required_argument, 0, 'f' },
  { "quit",         no_argument,       0, 'q' },
  { "schema",       optional_argument, 0, argv_option_schema },
  { NULL, 0,0,0 }
};

#if !defined(REMOVE_HELPTEXT)
static const char *help_text =
    "Activates OLSR.org routing daemon\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "  -h, --help                             Display this help file\n"
    "  -v, --version                          Display the version string and the included static plugins\n"
    "  -p, --plugin=shared-library            Load a shared library as a plugin\n"
    "      --quit                             Load plugins and validate configuration, then end\n"
    "      --schema                           Display all allowed section types of configuration\n"
    "              =section_type              Display all allowed entries of one configuration section\n"
    "              =section_type.key          Display help text for configuration entry\n"
    "  -l, --load=SOURCE                      Load configuration from a SOURCE\n"
    "  -S, --save=TARGET                      Save configuration to a TARGET\n"
    "  -s, --set=section_type.                Add an unnamed section to the configuration\n"
    "           =section_type.key=value       Add a key/value pair to an unnamed section\n"
    "           =section_type[name].          Add a named section to the configuration\n"
    "           =section_type[name].key=value Add a key/value pair to a named section\n"
    "  -r, --remove=section_type.             Remove all sections of a certain type\n"
    "              =section_type.key          Remove a key in an unnamed section\n"
    "              =section_type[name].       Remove a named section\n"
    "              =section_type[name].key    Remove a key in a named section\n"
    "  -g, --get                              Show all section types in database\n"
    "           =section_type.                Show all named sections of a certain type\n"
    "           =section_type.key             Show the value(s) of a key in an unnamed section\n"
    "           =section_type[name].key       Show the value(s) of a key in a named section\n"
    "  -f, --format=FORMAT                    Set the format for loading/saving data\n"
    "                                         (use 'AUTO' for automatic detection of format)\n"
;
#endif

/* logging sources included in debug level 1 */
static enum log_source level_1_sources[] = {
    LOG_MAIN,
};

/* name of default configuration file */
static const char *DEFAULT_CONFIGFILE = OLSRD_GLOBAL_CONF_FILE;

/* prototype for local statics */
static void quit_signal_handler(int);
static void hup_signal_handler(int);
static void setup_signalhandler(void);
static int mainloop(int argc, char **argv);
static int parse_commandline(int argc, char **argv, bool reload_only);
static int display_schema(void);

/**
 * Main program
 */
int
main(int argc, char **argv) {
  int return_code;
  int fork_pipe;
  const char *fork_str;

  /* early initialization */
  return_code = 1;
  fork_pipe = -1;
  schema_name = NULL;

  /* setup signal handler */
  running = true;
  reload_config = false;
  exit_called = false;
  setup_signalhandler();

  /* initialize logger */
  if (olsr_log_init(SEVERITY_WARN)) {
    goto olsrd_cleanup;
  }

  /* add configuration definition */
  if (olsr_cfg_init()) {
    goto olsrd_cleanup;
  }

  /* initialize logging to config interface */
  olsr_logcfg_init(level_1_sources, ARRAYSIZE(level_1_sources));
  olsr_logcfg_addschema(olsr_cfg_get_schema());

  /* load static plugins */
  olsr_plugins_load_static();

  /* parse command line and read configuration files */
  return_code = parse_commandline(argc, argv, false);
  if (return_code != -1) {
    /* end OLSRd now */
    goto olsrd_cleanup;
  }

  /* prepare for an error during initialization */
  return_code = 1;

  /* TODO: check if we are root, otherwise stop */
  if (0 && geteuid()) {
    OLSR_WARN(LOG_MAIN, "You must be root(uid = 0) to run olsrd!\n");
    goto olsrd_cleanup;
  }

  /* see if we need to fork */
  fork_str = cfg_db_get_entry_value(olsr_cfg_get_rawdb(), CFG_SECTION_GLOBAL, NULL, CFG_GLOBAL_FORK);
  if (cfg_get_bool(fork_str)) {
    /* fork into background */
    fork_pipe = daemonize_prepare();
    if (fork_pipe == -1) {
      OLSR_WARN(LOG_MAIN, "Cannot fork into background");
      goto olsrd_cleanup;
    }
  }

  /* configure logger */
  if (olsr_logcfg_apply(olsr_cfg_get_rawdb())) {
    goto olsrd_cleanup;
  }

  /* initialize basic framework */
  olsr_memcookie_init();
  if (olsr_clock_init()) {
    goto olsrd_cleanup;
  }
  if (olsr_timer_init()) {
    goto olsrd_cleanup;
  }
  if (olsr_socket_init()) {
    goto olsrd_cleanup;
  }
  olsr_packet_init();
  if (olsr_stream_init()) {
    goto olsrd_cleanup;
  }
  olsr_telnet_init();

  /* activate plugins */
  olsr_plugins_init();

  /* show schema if necessary */
  if (schema_name) {
    return_code = display_schema();
    goto olsrd_cleanup;
  }

  /* apply configuration */
  if (olsr_cfg_apply()) {
    goto olsrd_cleanup;
  }

  if (!running) {
    /*
     * mayor error during late initialization
     * or maybe the user decided otherwise and pressed CTRL-C
     */
    return_code = exit_called ? 1 : 0;
    goto olsrd_cleanup;
  }

  if (fork_pipe != -1) {
    /* tell main process that we are finished with initialization */
    daemonize_finish(fork_pipe, 0);
    fork_pipe = -1;
  }

  /* activate mainloop */
  return_code = mainloop(argc, argv);

olsrd_cleanup:
  /* free plugins */
  olsr_plugins_cleanup();

  /* free framework resources */
  olsr_telnet_cleanup();
  olsr_stream_cleanup();
  olsr_packet_cleanup();
  olsr_socket_cleanup();
  olsr_timer_cleanup();
  olsr_memcookie_cleanup();
  olsr_logcfg_cleanup();

  /* free configuration resources */
  olsr_cfg_cleanup();

  /* free logger resources */
  olsr_log_cleanup();

  if (fork_pipe != -1) {
    fprintf(stderr, "Errorcode: %d\n", return_code);
    /* tell main process that we had a problem */
    daemonize_finish(fork_pipe, return_code);
  }

  return return_code;
}

void
olsr_exit(void) {
  running = false;
  exit_called = true;
}

/**
 * Handle incoming SIGINT signal
 * @param signo
 */
static void
quit_signal_handler(int signo __attribute__ ((unused))) {
  running = false;
}

/**
 * Handle incoming SIGHUP signal
 * @param signo
 */
static void
hup_signal_handler(int signo __attribute__ ((unused))) {
  reload_config = true;
}

/**
 * Mainloop of olsrd
 * @return exit code for olsrd
 */
static int
mainloop(int argc, char **argv) {
  uint32_t next_interval;
  int exit_code = 0;

  OLSR_INFO(LOG_MAIN, "Starting olsr.org adapter daemon");

  /* enter main loop */
  while (running) {
    /*
     * Update the global timestamp. We are using a non-wallclock timer here
     * to avoid any undesired side effects if the system clock changes.
     */
    if (olsr_clock_update()) {
      exit_code = 1;
      break;
    }
    next_interval = olsr_clock_get_absolute(50);

    /* Process timers */
    olsr_timer_walk();

    /* Read incoming data and handle it immediately */
    if (olsr_socket_handle(next_interval)) {
      exit_code = 1;
      break;
    }

    /* reload configuration if triggered */
    if (reload_config) {
      if (olsr_cfg_create_new_rawdb()) {
        running = false;
      }
      else if (parse_commandline(argc, argv, true) == -1) {
        olsr_cfg_apply();
      }
      reload_config = false;
    }
  }

  /* wait for 500 milliseconds and process socket events */
  next_interval = olsr_clock_get_absolute(500);
  olsr_timer_walk();
  if (olsr_socket_handle(next_interval)) {
    exit_code = 1;
  }

  OLSR_INFO(LOG_MAIN, "Ending olsr.org daemon");
  return exit_code;
}

/**
 * Setup signal handling for olsrd
 */
static void
setup_signalhandler(void) {
  static struct sigaction act;

  memset(&act, 0, sizeof(act));

  /* setup signal handler first */
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;

  act.sa_handler = quit_signal_handler;
  sigaction(SIGINT, &act, NULL);
  sigaction(SIGQUIT, &act, NULL);
  sigaction(SIGILL, &act, NULL);
  sigaction(SIGABRT, &act, NULL);
  sigaction(SIGTERM, &act, NULL);

  act.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &act, NULL);
  sigaction(SIGUSR1, &act, NULL);
  sigaction(SIGUSR2, &act, NULL);

  act.sa_handler = hup_signal_handler;
  sigaction(SIGHUP, &act, NULL);
}

/**
 * Parse command line of olsrd
 * @param argc number of arguments
 * @param argv argument vector
 * @param def_config default configuration file, NULL if
 *   no default config file should be loaded
 * @param reload_only true if only the command line arguments should
 *   be parsed that load a configuration (--set, --remove, --load,
 *   and --format), false for normal full parsing.
 * @return -1 if olsrd should start normally, otherwise olsrd should
 *   exit with the returned number
 */
static int
parse_commandline(int argc, char **argv, bool reload_only) {
  const char *parameters;
  struct olsr_plugin *plugin, *plugin_it;
  struct cfg_cmd_state state;
  struct autobuf log;
  struct cfg_db *db;
  int opt, opt_idx, return_code;
  bool loaded_file;

  return_code = -1;
  loaded_file = false;
  db = olsr_cfg_get_rawdb();

  /* reset getopt_long */
  opt_idx = -1;
  optind = 1;

  abuf_init(&log, 1024);
  cfg_cmd_add(&state);

  if (reload_only) {
    /* only parameters that load and change configuration data */
    parameters = "l:s:r:f:";
  }
  else {
    parameters = "hvp:ql:S:s:r:g::f:";
  }
  while (return_code == -1
      && 0 <= (opt = getopt_long(argc, argv, parameters, olsr_options, &opt_idx))) {
    switch (opt) {
      case 'h':
#if !defined(REMOVE_HELPTEXT)
        abuf_appendf(&log, "Usage: %s [OPTION]...\n%s", argv[0], help_text);
#endif
        return_code = 0;
        break;

      case 'v':
        olsr_builddata_printversion(&log);
        OLSR_FOR_ALL_PLUGIN_ENTRIES(plugin, plugin_it) {
          abuf_appendf(&log, " Static plugin: %s\n", plugin->name);
        }
        return_code = 0;
        break;
      case 'p':
        if (olsr_plugins_load(optarg) == NULL) {
          return_code = 1;
        }
        break;
      case 'q':
        running = false;
        break;

      case argv_option_schema:
        schema_name = optarg;
        break;

      case 'l':
        if (cfg_cmd_handle_load(db, &state, optarg, &log)) {
          return_code = 1;
        }
        loaded_file = true;
        break;
      case 'S':
        if (cfg_cmd_handle_save(db, &state, optarg, &log)) {
          return_code = 1;
        }
        break;
      case 's':
        if (cfg_cmd_handle_set(db, &state, optarg, &log)) {
          return_code = 1;
        }
        break;
      case 'r':
        if (cfg_cmd_handle_remove(db, &state, optarg, &log)) {
          return_code = 1;
        }
        break;
      case 'g':
        if (cfg_cmd_handle_get(db, &state, optarg, &log)) {
          return_code = 1;
        }
        else {
          return_code = 0;
        }
        break;
      case 'f':
        if (cfg_cmd_handle_format(db, &state, optarg, &log)) {
          return_code = 1;
        }
        break;

      default:
        if (!reload_only) {
          return_code = 1;
        }
        break;
    }
  }

  if (return_code == -1 && !loaded_file) {
    /* try to load default config file if no other loaded */
    cfg_cmd_handle_load(db, &state, DEFAULT_CONFIGFILE, NULL);
  }

  if (return_code == -1) {
    /* validate configuration */
    if (cfg_schema_validate(db, false, false, true, &log)) {
      return_code = 1;
    }
  }

  if (log.len > 0) {
    if (reload_only) {
      OLSR_WARN(LOG_MAIN, "Cannot reload configuration.\n%s", log.buf);
    }
    else {
      fputs(log.buf, return_code == 0 ? stdout : stderr);
    }
  }

  abuf_free(&log);
  cfg_cmd_remove(&state);

  return return_code;
}

static int
display_schema(void) {
  struct cfg_cmd_state state;
  struct autobuf log;
  int return_code;

  return_code = 0;

  abuf_init(&log, 1024);
  cfg_cmd_add(&state);

  if (cfg_cmd_handle_schema(olsr_cfg_get_rawdb(), &state, schema_name, &log)) {
    return_code = 1;
  }

  if (log.len > 0) {
    fputs(log.buf, stdout);
  }

  abuf_free(&log);
  cfg_cmd_remove(&state);

  return return_code;
}