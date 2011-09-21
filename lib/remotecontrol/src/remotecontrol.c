
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2009, the olsr.org team - see HISTORY file
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

/*
 * Dynamic linked library for the olsr.org olsr daemon
 */

#include <stdlib.h>

#include "common/common_types.h"
#include "common/autobuf.h"
#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/netaddr.h"
#include "common/string.h"

#include "config/cfg_cmd.h"

#include "olsr_cfg.h"
#include "olsr_logging.h"
#include "olsr_memcookie.h"
#include "olsr_plugins.h"
#include "olsr_telnet.h"
#include "olsr_timer.h"
#include "olsr.h"
#include "remotecontrol.h"

static int _plugin_load(void);
static int _plugin_enable(void);
static int _plugin_disable(void);

static enum olsr_telnet_result _handle_resource(struct olsr_telnet_session *con,
    const char *cmd, const char *param);
static enum olsr_telnet_result _handle_log(struct olsr_telnet_session *con,
    const char *cmd, const char *param);
static enum olsr_telnet_result _handle_config(struct olsr_telnet_session *con,
    const char *cmd, const char *param);
static enum olsr_telnet_result _update_logfilter(
    struct olsr_telnet_session *con,
    const char *cmd, const char *param, const char *current, bool value);

static bool _print_memory(struct autobuf *buf);
static bool _print_timer(struct autobuf *buf);

static void _stop_logging(struct olsr_telnet_session *session);

static void _print_log(struct log_handler_entry *,
    enum log_severity, enum log_source,
    bool, const char *, int, char *, int, int);
static const char *_str_hasnextword (const char *buffer, const char *word);

/* TODO: plugin configuration */

/* plugin parameters */
OLSR_PLUGIN7 {
  .descr = "OLSRD remote control and debug plugin",
  .author = "Henning Rogge",
  .load = _plugin_load,
  .enable = _plugin_enable,
  .disable = _plugin_disable,
};

/* command callbacks and names */
static struct olsr_telnet_command _telnet_cmds[] = {
  TELNET_CMD("resources", _handle_resource,
      "\"resources memory\": display information about memory usage\n"
      "\"resources timer\": display information about active timers\n"
      ),
  TELNET_CMD("log", _handle_log,
      "\"log\":      continuous output of logging to this console\n"
      "\"log show\": show configured logging option for debuginfo output\n"
      "\"log add <severity> <source1> <source2> ...\": Add one or more sources of a defined severity for logging\n"
      "\"log remove <severity> <source1> <source2> ...\": Remove one or more sources of a defined severity for logging\n"
      ),
  TELNET_CMD("config", _handle_config,
      "\"config commit\":                                   Commit changed configuration\n"
      "\"config revert\":                                   Revert to active configuration\n"
      "\"config schema\":                                   Display all allowed section types of configuration\n"
      "\"config schema <section_type>\":                    Display all allowed entries of one configuration section\n"
      "\"config schema <section_type.key>\":                Display help text for configuration entry\n"
      "\"config load <SOURCE>\":                            Load configuration from a SOURCE\n"
      "\"config save <TARGET>\":                            Save configuration to a TARGET\n"
      "\"config set <section_type>.\":                      Add an unnamed section to the configuration\n"
      "\"config set <section_type>.<key>=<value>\":         Add a key/value pair to an unnamed section\n"
      "\"config set <section_type>[<name>].\":              Add a named section to the configuration\n"
      "\"config set <section_type>[<name>].<key>=<value>\": Add a key/value pair to a named section\n"
      "\"config remove <section_type>.\":                   Remove all sections of a certain type\n"
      "\"config remove <section_type>.<key>\":              Remove a key in an unnamed section\n"
      "\"config remove <section_type>[<name>].\":           Remove a named section\n"
      "\"config remove <section_type>[<name>].<key>\":      Remove a key in a named section\n"
      "\"config get\":                                      Show all section types in database\n"
      "\"config get <section_type>.\":                      Show all named sections of a certain type\n"
      "\"config get <section_type>.<key>\":                 Show the value(s) of a key in an unnamed section\n"
      "\"config get <section_type>[<name>].<key>\":         Show the value(s) of a key in a named section\n"
      "\"config format <FORMAT>\":                          Set the format for loading/saving data\n"
      "\"config format AUTO\":                              Set the format to automatic detection\n"
      ),
};

/* variables for log access */
static struct log_handler_mask _logging_mask;
static int _log_source_maxlen, _log_severity_maxlen;
static struct olsr_telnet_session *_log_session;

static struct log_handler_entry _log_handler = {
  .bitmask_ptr = &_logging_mask,
  .handler = _print_log
};

/**
 * Initialize remotecontrol plugin
 * @return 0 if plugin was initialized, -1 if an error happened
 */
static int
_plugin_load(void)
{
  int i;

  /* calculate maximum length of log source names */
  _log_source_maxlen = 0;
  for (i=1; i<LOG_SOURCE_COUNT; i++) {
    int len = strlen(LOG_SOURCE_NAMES[i]);

    if (len > _log_source_maxlen) {
      _log_source_maxlen = len;
    }
  }

  /* calculate maximum length of log severity names */
  _log_severity_maxlen = 0;
  for (i=1; i<LOG_SEVERITY_COUNT; i++) {
    int len = strlen(LOG_SEVERITY_NAMES[i]);

    if (len > _log_severity_maxlen) {
      _log_severity_maxlen = len;
    }
  }

  return 0;
}

/**
 * Deactivate remotecontrol plugin
 * @return 0 if plugin was disabled, -1 if an error happened
 */
static int
_plugin_disable(void)
{
  size_t i;

  for (i=0; i<ARRAYSIZE(_telnet_cmds); i++) {
    olsr_telnet_remove(&_telnet_cmds[i]);
  }

  if (_log_session) {
    olsr_stream_close(&_log_session->session);
    olsr_log_removehandler(&_log_handler);
    _log_session = NULL;
  }
  return 0;
}

/**
 * Enable remotecontrol plugin
 * @return 0 if plugin was enabled, -1 if an error happened
 */
static int
_plugin_enable(void)
{
  size_t i;

  _log_session = NULL;

  for (i=0; i<ARRAYSIZE(_telnet_cmds); i++) {
    olsr_telnet_add(&_telnet_cmds[i]);
  }

  /* copy global logging mask */
  memcpy(&_logging_mask, &log_global_mask, sizeof(log_global_mask));

  return 0;
}

static bool
_print_memory(struct autobuf *buf) {
  struct olsr_memcookie_info *c, *iterator;

  OLSR_FOR_ALL_COOKIES(c, iterator) {
    if (abuf_appendf(buf, "%-25s (MEMORY) size: %lu usage: %u freelist: %u\n",
        c->ci_name, (unsigned long)c->ci_size, c->ci_usage, c->ci_free_list_usage) < 0) {
      return true;
    }
  }
  return false;
}

static bool
_print_timer(struct autobuf *buf) {
  struct olsr_timer_info *t, *iterator;

  OLSR_FOR_ALL_TIMERS(t, iterator) {
    if (abuf_appendf(buf, "%-25s (TIMER) usage: %u changes: %u\n",
        t->name, t->usage, t->changes) < 0) {
      return true;
    }
  }
  return false;
}

static enum olsr_telnet_result
_handle_resource(struct olsr_telnet_session *con,
    const char *cmd __attribute__ ((unused)), const char *param)
{
  if (param == NULL || strcasecmp(param, "memory") == 0) {
    if (abuf_puts(&con->session.out, "Memory cookies:\n") < 0) {
      return TELNET_RESULT_ABUF_ERROR;
    }

    if (_print_memory(&con->session.out)) {
      return TELNET_RESULT_ABUF_ERROR;
    }
  }

  if (param == NULL || strcasecmp(param, "timer") == 0) {
    if (abuf_puts(&con->session.out, "\nTimer cookies:\n") < 0) {
      return TELNET_RESULT_ABUF_ERROR;
    }

    if (_print_timer(&con->session.out)) {
      return TELNET_RESULT_ABUF_ERROR;
    }
  }
  return TELNET_RESULT_ACTIVE;
}

static enum olsr_telnet_result
_update_logfilter(struct olsr_telnet_session *con,
    const char *cmd, const char *param, const char *current, bool value) {
  const char *next;
  int src, sev;

  for (sev = 0; sev < LOG_SEVERITY_COUNT; sev++) {
    if ((next = _str_hasnextword(current, LOG_SEVERITY_NAMES[sev])) != NULL) {
      break;
    }
  }
  if (sev == LOG_SEVERITY_COUNT) {
    abuf_appendf(&con->session.out, "Error, unknown severity in command: %s %s\n", cmd, param);
    return TELNET_RESULT_ACTIVE;
  }

  current = next;
  while (current && *current) {
    for (src = 0; src < LOG_SOURCE_COUNT; src++) {
      if ((next = _str_hasnextword(current, LOG_SOURCE_NAMES[src])) != NULL) {
        _logging_mask.mask[sev][src] = value;
        break;
      }
    }
    if (src == LOG_SOURCE_COUNT) {
      abuf_appendf(&con->session.out, "Error, unknown source in command: %s %s\n", cmd, param);
      return TELNET_RESULT_ACTIVE;
    }
    current = next;
  }

  olsr_log_updatemask();
  return TELNET_RESULT_ACTIVE;
}

static void
_print_log(struct log_handler_entry *h __attribute__((unused)),
    enum log_severity severity __attribute__((unused)),
    enum log_source source __attribute__((unused)),
    bool no_header __attribute__((unused)),
    const char *file __attribute__((unused)),
    int line __attribute__((unused)),
    char *buffer,
    int timeLength __attribute__((unused)),
    int prefixLength __attribute__((unused)))
{
  abuf_puts(&_log_session->session.out, buffer);
  abuf_puts(&_log_session->session.out, "\n");

  olsr_stream_flush(&_log_session->session);
}

static void
_stop_logging(struct olsr_telnet_session *session) {
  olsr_log_removehandler(&_log_handler);

  session->stop_handler = NULL;
  _log_session = NULL;
}

static enum olsr_telnet_result
_handle_log(struct olsr_telnet_session *con, const char *cmd, const char *param) {
  const char *next;
  int src;

  if (param == NULL) {
    if (con->stop_handler) {
      abuf_puts(&con->session.out, "Error, you cannot stack continuous output commands\n");
      return TELNET_RESULT_ACTIVE;
    }
    if (_log_session != NULL) {
      abuf_puts(&con->session.out, "Error, debuginfo cannot handle concurrent logging\n");
      return TELNET_RESULT_ACTIVE;
    }

    _log_session = con;
    con->stop_handler = _stop_logging;

    olsr_log_addhandler(&_log_handler);
    return TELNET_RESULT_CONTINOUS;
  }

  if (strcasecmp(param, "show") == 0) {
    abuf_appendf(&con->session.out, "%*s %6s %6s %6s\n",
        _log_source_maxlen, "",
        LOG_SEVERITY_NAMES[SEVERITY_DEBUG],
        LOG_SEVERITY_NAMES[SEVERITY_INFO],
        LOG_SEVERITY_NAMES[SEVERITY_WARN]);

    for (src=0; src<LOG_SOURCE_COUNT; src++) {
      abuf_appendf(&con->session.out, "%*s %*s %*s %*s\n",
        _log_source_maxlen, LOG_SOURCE_NAMES[src],
        (int)sizeof(LOG_SEVERITY_NAMES[SEVERITY_DEBUG]),
        _logging_mask.mask[SEVERITY_DEBUG][src] ? "*" : "",
        (int)sizeof(LOG_SEVERITY_NAMES[SEVERITY_INFO]),
        _logging_mask.mask[SEVERITY_INFO][src] ? "*" : "",
        (int)sizeof(LOG_SEVERITY_NAMES[SEVERITY_WARN]),
        _logging_mask.mask[SEVERITY_WARN][src] ? "*" : "");
    }
    return TELNET_RESULT_ACTIVE;
  }

  if ((next = _str_hasnextword(param, "add")) != NULL) {
    return _update_logfilter(con, cmd, param, next, true);
  }

  if ((next = _str_hasnextword(param, "remove")) != NULL) {
    return _update_logfilter(con, cmd, param, next, false);
  }

  return TELNET_RESULT_UNKNOWN_COMMAND;
}

static enum olsr_telnet_result
_handle_config(struct olsr_telnet_session *con,
    const char *cmd __attribute__((unused)), const char *param) {
  const char *next = NULL;

  if (param == NULL || *param == 0) {
    abuf_puts(&con->session.out, "Error, 'config' needs a parameter\n");
    return TELNET_RESULT_ACTIVE;
  }

  if ((next = _str_hasnextword(param, "commit"))) {
    if (!cfg_schema_validate(olsr_cfg_get_rawdb(),
        false, false, true, &con->session.out)) {
      olsr_commit();
    }
  }
  else if ((next = _str_hasnextword(param, "rollback"))) {
    olsr_cfg_rollback();
  }
  else if ((next = _str_hasnextword(param, "format"))) {
    cfg_cmd_handle_format(olsr_cfg_get_instance(), next);
  }
  else if ((next = _str_hasnextword(param, "get"))) {
    cfg_cmd_handle_get(olsr_cfg_get_instance(),
        olsr_cfg_get_rawdb(), next, &con->session.out);
  }
  else if ((next = _str_hasnextword(param, "load"))) {
    cfg_cmd_handle_load(olsr_cfg_get_instance(),
        olsr_cfg_get_rawdb(), next, &con->session.out);
  }
  else if ((next = _str_hasnextword(param, "remove"))) {
    cfg_cmd_handle_remove(olsr_cfg_get_instance(),
        olsr_cfg_get_rawdb(), next, &con->session.out);
  }
  else if ((next = _str_hasnextword(param, "save"))) {
    cfg_cmd_handle_save(olsr_cfg_get_instance(),
        olsr_cfg_get_rawdb(), next, &con->session.out);
  }
  else if ((next = _str_hasnextword(param, "schema"))) {
    cfg_cmd_handle_schema(
        olsr_cfg_get_rawdb(), next, &con->session.out);
  }
  else if ((next = _str_hasnextword(param, "set"))) {
    cfg_cmd_handle_set(olsr_cfg_get_instance(),
        olsr_cfg_get_rawdb(), next, &con->session.out);
  }
  else {
    return TELNET_RESULT_UNKNOWN_COMMAND;
  }

  return TELNET_RESULT_ACTIVE;
}
/**
 * Check if a string starts with a certain word. The function
 * is not case sensitive.
 * @param buffer pointer to string
 * @param word pointer to the word
 * @return pointer to the string behind the word, NULL if no match
 */
static const char *
_str_hasnextword (const char *buffer, const char *word) {
  /* skip whitespaces first */
  while (isblank(*buffer)) {
    buffer++;
  }

  while (*word != 0 && *buffer != 0 && !isblank(*buffer) && tolower(*word) == tolower(*buffer)) {
    word++;
    buffer++;
  }

  /* complete match ? */
  if (*word == 0) {
    while (isblank(*buffer)) {
      buffer++;
    }
    return buffer;
  }
  return NULL;
}


/*
 * Local Variables:
 * mode: c
 * style: linux
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
