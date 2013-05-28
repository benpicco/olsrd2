
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "common/daemonize.h"
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

/* prototypes */
static bool _cb_stop_scheduler(void);
static void quit_signal_handler(int);
static void hup_signal_handler(int);
static void setup_signalhandler(void);
static int mainloop(int argc, char **argv);
static void parse_early_commandline(int argc, char **argv);
static int parse_commandline(int argc, char **argv, bool reload_only);
static int display_schema(void);

static bool _end_oonf_signal, _display_schema, _debug_early, _ignore_unknown;
static char *_schema_name;

enum argv_short_options {
  argv_option_schema = 256,
  argv_option_debug_early,
  argv_option_ignore_unknown,
};

static struct option oonf_options[] = {
#if !defined(REMOVE_HELPTEXT)
  { "help",         no_argument,       0, 'h' },
#endif
  { "version",         no_argument,       0, 'v' },
  { "plugin",          required_argument, 0, 'p' },
  { "load",            required_argument, 0, 'l' },
  { "save",            required_argument, 0, 'S' },
  { "set",             required_argument, 0, 's' },
  { "remove",          required_argument, 0, 'r' },
  { "get",             optional_argument, 0, 'g' },
  { "format",          required_argument, 0, 'f' },
  { "quit",            no_argument,       0, 'q' },
  { "nodefault",       no_argument,       0, 'n' },
  { "schema",          optional_argument, 0, argv_option_schema },
  { "Xearlydebug",     no_argument,       0, argv_option_debug_early },
  { "Xignoreunknown",  no_argument,       0, argv_option_ignore_unknown },
  { NULL, 0,0,0 }
};

#if !defined(REMOVE_HELPTEXT)
static const char *help_text =
    "Mandatory arguments for long options are mandatory for short options too.\n"
    "  -h, --help                             Display this help file\n"
    "  -v, --version                          Display the version string and the included static plugins\n"
    "  -p, --plugin=shared-library            Load a shared library as a plugin\n"
    "  -q, --quit                             Load plugins and validate configuration, then end\n"
    "      --schema                           Display all allowed section types of configuration\n"
    "              =all                       Display all allowed entries in all sections\n"
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
    "  -n, --nodefault                        Do not load the default configuration file\n"
    "\n"
    "Expert/Experimental arguments\n"
    "  --Xearlydebug                          Activate debugging output before configuration could be parsed\n"
    "  --Xignoreunknown                       Ignore unknown command line arguments\n"
    "\n"
    "The remainder of the parameters which are no arguments are handled as interface names.\n"
;
#endif

/**
 * Main program
 */
int
main(int argc, char **argv) {
  int return_code;
  int fork_pipe;
  uint64_t next_interval;
  size_t i;
  size_t initialized;

  struct oonf_subsystem **subsystems;
  size_t subsystem_count;

  /* early initialization */
  return_code = 1;
  fork_pipe = -1;
  initialized = 0;

  _schema_name = NULL;
  _display_schema = false;
  _debug_early = false;
  _ignore_unknown = false;

  /* assemble list of subsystems first */
  subsystem_count = get_used_api_subsystem_count()
      + oonf_setup_get_subsystem_count();

  subsystems = calloc(subsystem_count, sizeof(struct oonf_subsystem *));
  if(!subsystems) {
    fprintf(stderr, "Out of memory error for subsystem array\n");
    return -1;
  }

  memcpy(&subsystems[0], used_api_subsystems,
      sizeof(struct oonf_subsystem *) * get_used_api_subsystem_count());
  memcpy(&subsystems[get_used_api_subsystem_count()],
      oonf_setup_get_subsystems(),
      sizeof(struct oonf_subsystem *) * oonf_setup_get_subsystem_count());

  /* setup signal handler */
  _end_oonf_signal = false;
  setup_signalhandler();

  /* parse "early" command line arguments */
  parse_early_commandline(argc, argv);

  /* initialize logger */
  if (oonf_log_init(oonf_appdata_get(), _debug_early ? LOG_SEVERITY_DEBUG : LOG_SEVERITY_WARN)) {
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

  /* parse command line and read configuration files */
  return_code = parse_commandline(argc, argv, false);
  if (return_code != -1) {
    /* end OONFd now */
    goto olsrd_cleanup;
  }

  /* prepare for an error during initialization */
  return_code = 1;

  /* read global section early */
  if (oonf_cfg_update_globalcfg(true)) {
    OONF_WARN(LOG_MAIN, "Cannot read global configuration section");
    goto olsrd_cleanup;
  }

  /* see if we need to fork */
  if (!_display_schema && config_global.fork) {
    /* fork into background */
    fork_pipe = daemonize_prepare();
    if (fork_pipe == -1) {
      OONF_WARN(LOG_MAIN, "Cannot fork into background");
      goto olsrd_cleanup;
    }
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

  /* check if we are root, otherwise stop */
#if OONF_NEED_ROOT == true
  if (geteuid() != 0) {
    OONF_WARN(LOG_MAIN, "You must be root(uid = 0) to run %s!\n",
        oonf_appdata_get()->app_name);
    goto olsrd_cleanup;
  }
#endif

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

  if (fork_pipe != -1) {
    /* tell main process that we are finished with initialization */
    daemonize_finish(fork_pipe, 0);
    fork_pipe = -1;
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

  if (fork_pipe != -1) {
    /* tell main process that we had a problem */
    daemonize_finish(fork_pipe, return_code);
  }

  free (subsystems);
  return return_code;
}

/**
 * Handle incoming SIGINT signal
 * @param signo
 */
static void
quit_signal_handler(int signo __attribute__ ((unused))) {
  oonf_cfg_exit();
}

/**
 * Handle incoming SIGHUP signal
 * @param signo
 */
static void
hup_signal_handler(int signo __attribute__ ((unused))) {
  oonf_cfg_trigger_reload();
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
      if (parse_commandline(argc, argv, true) == -1) {
        if (oonf_cfg_apply()) {
          break;
        }
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

static void
parse_early_commandline(int argc, char **argv) {
  int opt, opt_idx;

  opterr = 0;
  while (0 <= (opt = getopt_long(argc, argv, "-", oonf_options, &opt_idx))) {
    switch (opt) {
      case argv_option_debug_early:
        _debug_early = true;
        break;
      case argv_option_ignore_unknown:
        _ignore_unknown = true;
        break;
      default:
        break;
    }
  }
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
  struct oonf_subsystem *plugin;
  const char *parameters;
  struct autobuf log;
  struct cfg_db *db;
  int opt, opt_idx, return_code;
  bool loaded_file, nodefault;

  return_code = -1;
  loaded_file = false;
  nodefault = false;
  db = oonf_cfg_get_rawdb();

  /* reset getopt_long */
  opt_idx = -1;
  optind = 1;
  opterr = _ignore_unknown ? 0 : -1;

  abuf_init(&log);
  cfg_cmd_clear_state(oonf_cfg_get_instance());

  if (reload_only) {
    /* only parameters that load and change configuration data */
    parameters = "-p:l:s:r:f:n";
  }
  else {
    parameters = "-hvp:ql:S:s:r:g::f:n";
  }

  while (return_code == -1
      && 0 <= (opt = getopt_long(argc, argv, parameters, oonf_options, &opt_idx))) {
    switch (opt) {
      case 'h':
#if !defined(REMOVE_HELPTEXT)
        abuf_appendf(&log, "Usage: %s [OPTION]...\n%s%s%s", argv[0],
            oonf_appdata_get()->help_prefix,
            help_text,
            oonf_appdata_get()->help_suffix);
#endif
        return_code = 0;
        break;

      case argv_option_debug_early:
      case argv_option_ignore_unknown:
        /* ignore this here */
        break;

      case 'v':
        oonf_log_printversion(&log);
        avl_for_each_element(&oonf_plugin_tree, plugin, _node) {
          if (!oonf_subsystem_is_dynamic(plugin)) {
            abuf_appendf(&log, "Static plugin: %s\n", plugin->name);
          }
        }
        return_code = 0;
        break;
      case 'p':
        if (oonf_plugins_load(optarg) == NULL) {
          return_code = 1;
        }
        else {
          cfg_db_add_entry(oonf_cfg_get_rawdb(), CFG_SECTION_GLOBAL, NULL, CFG_GLOBAL_PLUGIN, optarg);
        }
        break;
      case 'q':
        oonf_cfg_exit();
        break;

      case argv_option_schema:
        _schema_name = optarg;
        _display_schema = true;
        break;

      case 'l':
        if (cfg_cmd_handle_load(oonf_cfg_get_instance(), db, optarg, &log)) {
          return_code = 1;
        }
        loaded_file = true;
        break;
      case 'S':
        if (cfg_cmd_handle_save(oonf_cfg_get_instance(), db, optarg, &log)) {
          return_code = 1;
        }
        break;
      case 's':
        if (cfg_cmd_handle_set(oonf_cfg_get_instance(), db, optarg, &log)) {
          return_code = 1;
        }
        break;
      case 'r':
        if (cfg_cmd_handle_remove(oonf_cfg_get_instance(), db, optarg, &log)) {
          return_code = 1;
        }
        break;
      case 'g':
        if (cfg_cmd_handle_get(oonf_cfg_get_instance(), db, optarg, &log)) {
          return_code = 1;
        }
        else {
          return_code = 0;
        }
        break;
      case 'f':
        if (cfg_cmd_handle_format(oonf_cfg_get_instance(), optarg)) {
          return_code = 1;
        }
        break;
      case 'n':
        nodefault = true;
        break;

      case 1:
        /* the rest are interface names */
        if (cfg_db_add_namedsection(db, CFG_INTERFACE_SECTION, optarg) == NULL) {
          abuf_appendf(&log, "Could not add named section for interface %s", optarg);
          return_code = 1;
        }
        break;

      default:
        if (!(reload_only ||_ignore_unknown)) {
          abuf_appendf(&log, "Unknown parameter: '%c' (%d)\n", opt, opt);
          return_code = 1;
        }
        break;
    }
  }

  while (return_code == -1 && optind < argc) {
    optind++;
  }

  if (return_code == -1 && !loaded_file && !nodefault) {
    /* try to load default config file if no other loaded */
    cfg_cmd_handle_load(oonf_cfg_get_instance(), db,
        oonf_appdata_get()->default_config, NULL);
  }

  if (abuf_getlen(&log) > 0) {
    if (reload_only) {
      OONF_WARN(LOG_MAIN, "Cannot reload configuration.\n%s", abuf_getptr(&log));
    }
    else {
      fputs(abuf_getptr(&log), return_code == 0 ? stdout : stderr);
    }
  }

  abuf_free(&log);

  return return_code;
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
