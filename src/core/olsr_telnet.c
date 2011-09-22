/*
 * olsr_telnet.c
 *
 *  Created on: Sep 7, 2011
 *      Author: rogge
 */

#include "common/common_types.h"
#include "common/avl.h"
#include "common/avl_comp.h"

#include "builddata/version.h"

#include "config/cfg_delta.h"
#include "config/cfg_schema.h"

#include "olsr_cfg.h"
#include "olsr_logging.h"
#include "olsr_memcookie.h"
#include "olsr_netaddr_acl.h"
#include "olsr_plugins.h"
#include "olsr_stream_socket.h"
#include "olsr_timer.h"
#include "olsr.h"
#include "olsr_telnet.h"

#define _CFG_TELNET_SECTION "telnet"

/* variable definitions */
enum cfg_telnet_idx {
  _CFG_TELNET_ACL,
  _CFG_TELNET_BINDTO_V4,
  _CFG_TELNET_BINDTO_V6,
  _CFG_TELNET_PORT,
};

/* static function prototypes */
static void _config_changed(void);
static int _telnet_init(struct olsr_stream_session *);
static void _telnet_cleanup(struct olsr_stream_session *);
static void _telnet_create_error(struct olsr_stream_session *,
    enum olsr_stream_errors);
static enum olsr_stream_session_state _telnet_receive_data(
    struct olsr_stream_session *);
static enum olsr_telnet_result _telnet_handle_command(
    struct olsr_telnet_session *, const char *, const char *);
static struct olsr_telnet_command *_check_telnet_command(
    struct olsr_telnet_session *telnet,
    struct olsr_telnet_command *cmd, const char *command);

static void _telnet_repeat_timer(void *data);
static enum olsr_telnet_result _telnet_quit(
    struct olsr_telnet_session *con, const char *cmd, const char *param);
static enum olsr_telnet_result _telnet_help(
    struct olsr_telnet_session *con, const char *cmd, const char *param);
static enum olsr_telnet_result _telnet_echo(
    struct olsr_telnet_session *con, const char *cmd, const char *param);
static enum olsr_telnet_result _telnet_repeat(
    struct olsr_telnet_session *con, const char *cmd, const char *param);
static enum olsr_telnet_result _telnet_timeout(
    struct olsr_telnet_session *con, const char *cmd, const char *param);
static enum olsr_telnet_result _telnet_version(
    struct olsr_telnet_session *con, const char *cmd, const char *param);
static enum olsr_telnet_result _telnet_plugin(
    struct olsr_telnet_session *con, const char *cmd, const char *param);

/* global and static variables */
static struct cfg_schema_section telnet_section = {
  .t_type = _CFG_TELNET_SECTION,
  .t_help = "Settings for the telnet interface",
};

static struct cfg_schema_entry telnet_entries[] = {
  [_CFG_TELNET_ACL] = CFG_MAP_ACL_V46(olsr_stream_managed_config,
      acl, "127.0.0.1", "Access control list for telnet interface"),
  [_CFG_TELNET_BINDTO_V4] = CFG_MAP_NETADDR_V4(olsr_stream_managed_config,
      bindto_v4, "127.0.0.1", "Bind ipv4 socket to this address", false),
  [_CFG_TELNET_BINDTO_V6] = CFG_MAP_NETADDR_V6(olsr_stream_managed_config,
      bindto_v6, "::1", "Bind ipv6 socket to this address", false),
  [_CFG_TELNET_PORT] = CFG_MAP_INT_MINMAX(olsr_stream_managed_config,
      port, "2006", "Network port for telnet interface", 1, 65535),
};

static struct cfg_delta_handler telnet_handler = {
  .s_type = _CFG_TELNET_SECTION,
  .callback = _config_changed
};

/* built-in telnet commands */
static struct olsr_telnet_command _builtin[] = {
  TELNET_CMD("quit", _telnet_quit, "Ends telnet session"),
  TELNET_CMD("exit", _telnet_quit, "Ends telnet session"),
  TELNET_CMD("help", _telnet_help,
      "help: Display the online help text and a list of commands"),
  TELNET_CMD("echo", _telnet_echo,"echo <string>: Prints a string"),
  TELNET_CMD("repeat", _telnet_repeat,
      "repeat <seconds> <command>: Repeats a telnet command every X seconds"),
  TELNET_CMD("timeout", _telnet_timeout,
      "timeout <seconds> :Sets telnet session timeout"),
  TELNET_CMD("version", _telnet_version, "Displays version of the program"),
  TELNET_CMD("plugin", _telnet_plugin,
        "control plugins dynamically, parameters are 'list',"
        " 'activate <plugin>', 'deactivate <plugin>', "
        "'load <plugin>' and 'unload <plugin>'"),
};

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_telnet_state);

/* telnet session handling */
static struct olsr_memcookie_info *_telnet_memcookie;
static struct olsr_stream_managed _telnet_managed;
static struct olsr_timer_info *_telnet_repeat_timerinfo;

struct avl_tree telnet_cmd_tree;

int
olsr_telnet_init(void) {
  size_t i;

  if (olsr_subsystem_init(&olsr_telnet_state))
    return 0;

  _telnet_memcookie = olsr_memcookie_add("telnet session",
      sizeof(struct olsr_telnet_session));
  if (_telnet_memcookie == NULL) {
    olsr_subsystem_cleanup(&olsr_telnet_state);
    return -1;
  }

  _telnet_repeat_timerinfo = olsr_timer_add("txt repeat timer", _telnet_repeat_timer, true);
  if (_telnet_repeat_timerinfo == NULL) {
    olsr_memcookie_remove(_telnet_memcookie);
    olsr_subsystem_cleanup(&olsr_telnet_state);
    return -1;
  }

  cfg_schema_add_section(olsr_cfg_get_schema(), &telnet_section);
  cfg_schema_add_entries(&telnet_section, telnet_entries, ARRAYSIZE(telnet_entries));

  cfg_delta_add_handler(olsr_cfg_get_delta(), &telnet_handler);

  olsr_stream_add_managed(&_telnet_managed);
  _telnet_managed.config.session_timeout = 120000; /* 120 seconds */
  _telnet_managed.config.maximum_input_buffer = 4096;
  _telnet_managed.config.allowed_sessions = 3;
  _telnet_managed.config.memcookie = _telnet_memcookie;
  _telnet_managed.config.init = _telnet_init;
  _telnet_managed.config.cleanup = _telnet_cleanup;
  _telnet_managed.config.receive_data = _telnet_receive_data;
  _telnet_managed.config.create_error = _telnet_create_error;

  /* initialize telnet commands */
  avl_init(&telnet_cmd_tree, avl_comp_strcasecmp, false, NULL);
  for (i=0; i<ARRAYSIZE(_builtin); i++) {
    olsr_telnet_add(&_builtin[i]);
  }

  return 0;
}

void
olsr_telnet_cleanup(void) {
  if (olsr_subsystem_cleanup(&olsr_telnet_state))
    return;

  olsr_stream_remove_managed(&_telnet_managed);

  cfg_delta_remove_handler(olsr_cfg_get_delta(), &telnet_handler);
  cfg_schema_remove_section(olsr_cfg_get_schema(), &telnet_section);

  olsr_memcookie_remove(_telnet_memcookie);
}

int
olsr_telnet_add(struct olsr_telnet_command *command) {
  command->node.key = command->command;
  if (avl_insert(&telnet_cmd_tree, &command->node)) {
    return -1;
  }
  return 0;
}

void
olsr_telnet_remove(struct olsr_telnet_command *command) {
  avl_remove(&telnet_cmd_tree, &command->node);
}

void
olsr_telnet_stop(struct olsr_telnet_session *session) {
  if (session->stop_handler) {
    session->stop_handler(session);
    session->stop_handler = NULL;
  }
}

static void
_config_changed(void) {
  struct olsr_stream_managed_config config;
  int ret = -1;

  /* generate binary config */
  memset(&config, 0, sizeof(config));
  if (cfg_schema_tobin(&config, telnet_handler.post,
      telnet_entries, ARRAYSIZE(telnet_entries))) {
    /* error in conversion */
    OLSR_WARN(LOG_TELNET, "Cannot map telnet config to binary data");
    goto apply_config_failed;
  }

  if (olsr_stream_apply_managed(&_telnet_managed, &config)) {
    /* error while updating sockets */
    goto apply_config_failed;
  }
  ret = 0;

  /* fall through */
apply_config_failed:
  olsr_acl_remove(&config.acl);
}

static int
_telnet_init(struct olsr_stream_session *session) {
  struct olsr_telnet_session *telnet_session;

  /* get telnet session pointer */
  telnet_session = (struct olsr_telnet_session *)session;

  telnet_session->show_echo = true;
  telnet_session->stop_handler = NULL;
  telnet_session->timeout_value = 120000;

  list_init_head(&telnet_session->cleanup_list);

  return 0;
}

static void
_telnet_cleanup(struct olsr_stream_session *session) {
  struct olsr_telnet_session *telnet_session;
  struct olsr_telnet_cleanup *handler, *it;

  /* get telnet session pointer */
  telnet_session = (struct olsr_telnet_session *)session;

  /* stop continuous commands */
  olsr_telnet_stop(telnet_session);

  /* call all cleanup handlers */
  list_for_each_element_safe(&telnet_session->cleanup_list, handler, node, it) {
    /* remove from list first */
    olsr_telnet_remove_cleanup(handler);

    /* after this command the handler pointer might not be valid anymore */
    handler->cleanup_handler(handler);
  }
}

static void
_telnet_create_error(struct olsr_stream_session *session,
    enum olsr_stream_errors error) {
  switch(error) {
    case STREAM_REQUEST_FORBIDDEN:
      /* no message */
      break;
    case STREAM_REQUEST_TOO_LARGE:
      abuf_puts(&session->out, "Input buffer overflow, ending connection\n");
      break;
    case STREAM_SERVICE_UNAVAILABLE:
      abuf_puts(&session->out, "Telnet service unavailable, too many sessions\n");
      break;
  }
}

static enum olsr_stream_session_state
_telnet_receive_data(struct olsr_stream_session *session) {
  static const char defaultCommand[] = "/link/neigh/topology/hna/mid/routes";
  static char tmpbuf[128];

  struct olsr_telnet_session *telnet_session;
  enum olsr_telnet_result cmd_result;
  char *eol;
  int len;
  bool processedCommand = false, chainCommands = false;
  uint32_t old_timeout;

  /* get telnet session pointer */
  telnet_session = (struct olsr_telnet_session *)session;
  old_timeout = session->timeout->timer_period;

  /* loop over input */
  while (session->in.len > 0) {
    char *para = NULL, *cmd = NULL, *next = NULL;

    /* search for end of line */
    eol = memchr(session->in.buf, '\n', session->in.len);

    if (eol == NULL) {
      break;
    }

    /* terminate line with a 0 */
    if (eol != session->in.buf && eol[-1] == '\r') {
      eol[-1] = 0;
    }
    *eol++ = 0;

    /* handle line */
    OLSR_DEBUG(LOG_TELNET, "Interactive console: %s\n", session->in.buf);
    cmd = &session->in.buf[0];
    processedCommand = true;

    /* apply default command */
    if (strcmp(cmd, "/") == 0) {
      strcpy(tmpbuf, defaultCommand);
      cmd = tmpbuf;
    }

    if (cmd[0] == '/') {
      cmd++;
      chainCommands = true;
    }
    while (cmd) {
      len = session->out.len;

      /* handle difference between multicommand and singlecommand mode */
      if (chainCommands) {
        next = strchr(cmd, '/');
        if (next) {
          *next++ = 0;
        }
      }
      para = strchr(cmd, ' ');
      if (para != NULL) {
        *para++ = 0;
      }

      /* if we are doing continous output, stop it ! */
      if (telnet_session->stop_handler) {
        telnet_session->stop_handler(telnet_session);
        telnet_session->stop_handler = NULL;
      }

      if (strlen(cmd) != 0) {
        OLSR_DEBUG(LOG_TELNET, "Processing telnet command: '%s' '%s'",
            cmd, para);

        cmd_result = _telnet_handle_command(telnet_session, cmd, para);
        switch (cmd_result) {
          case TELNET_RESULT_ACTIVE:
            break;
          case TELNET_RESULT_CONTINOUS:
            break;
          case TELNET_RESULT_ABUF_ERROR:
            session->out.len = len;
            abuf_appendf(&session->out,
                "Error in autobuffer during command '%s'.\n", cmd);
            break;
          case TELNET_RESULT_UNKNOWN_COMMAND:
            session->out.len = len;
            abuf_appendf(&session->out, "Error, unknown command '%s'\n", cmd);
            break;
          case TELNET_RESULT_QUIT:
            return STREAM_SESSION_SEND_AND_QUIT;
        }
        /* put an empty line behind each command */
        if (telnet_session->show_echo) {
          abuf_puts(&session->out, "\n");
        }
      }
      cmd = next;
    }

    /* remove line from input buffer */
    abuf_pull(&session->in, eol - session->in.buf);

    if (session->in.buf[0] == '/') {
      /* end of multiple command line */
      return STREAM_SESSION_SEND_AND_QUIT;
    }
  }

  /* reset timeout */
  olsr_stream_set_timeout(session, telnet_session->timeout_value);

  /* print prompt */
  if (processedCommand && session->state == STREAM_SESSION_ACTIVE
      && telnet_session->show_echo) {
    abuf_puts(&session->out, "> ");
  }

  return STREAM_SESSION_ACTIVE;
}

static enum olsr_telnet_result
_telnet_handle_command(struct olsr_telnet_session *telnet,
    const char *command, const char *parameter) {
  struct olsr_telnet_command *cmd;
#if !defined(REMOVE_LOG_INFO)
  struct netaddr_str buf;
#endif
  cmd = _check_telnet_command(telnet, NULL, command);
  if (cmd == NULL) {
    return TELNET_RESULT_UNKNOWN_COMMAND;
  }

  OLSR_INFO(LOG_TELNET, "Executing command from %s: %s %s",
      netaddr_to_string(&buf, &telnet->session.remote_address), command,
      parameter == NULL ? "" : parameter);
  return cmd->handler(telnet, command, parameter);
}

static struct olsr_telnet_command *
_check_telnet_command(struct olsr_telnet_session *telnet,
    struct olsr_telnet_command *cmd, const char *command) {
#if !defined(REMOVE_LOG_DEBUG)
  struct netaddr_str buf;
#endif

  if (cmd == NULL) {
    cmd = avl_find_element(&telnet_cmd_tree, command, cmd, node);
    if (cmd == NULL) {
      return cmd;
    }
  }
  if (cmd->acl == NULL) {
    return cmd;
  }

  if (!olsr_acl_check_accept(cmd->acl, &telnet->session.remote_address)) {
    OLSR_DEBUG(LOG_TELNET, "Blocked telnet command '%s' to '%s' because of acl",
        cmd->command, netaddr_to_string(&buf, &telnet->session.remote_address));
    return NULL;
  }
  return cmd;
}

static enum olsr_telnet_result
_telnet_quit(struct olsr_telnet_session *telnet __attribute__ ((unused)),
    const char *cmd __attribute__ ((unused)), const char *param __attribute__ ((unused))) {
  return TELNET_RESULT_QUIT;
}

static enum olsr_telnet_result
_telnet_help(struct olsr_telnet_session *telnet,
    const char *cmd __attribute__ ((unused)), const char *param) {
  struct olsr_telnet_command *ptr, *iterator;

  if (param != NULL) {
    ptr = _check_telnet_command(telnet, NULL, param);
    ptr = avl_find_element(&telnet_cmd_tree, param, ptr, node);
    if (ptr == NULL) {
      abuf_appendf(&telnet->session.out, "No help text found for command: %s\n", param);
      return TELNET_RESULT_ACTIVE;
    }

    if (ptr->help_handler) {
      ptr->help_handler(telnet, param, "");
    }
    else {
      if (abuf_appendf(&telnet->session.out, "%s\n", ptr->help) < 0) {
        return TELNET_RESULT_ABUF_ERROR;
      }
    }
    return TELNET_RESULT_ACTIVE;
  }

  if (abuf_puts(&telnet->session.out, "Known commands:\n") < 0) {
    return TELNET_RESULT_ABUF_ERROR;
  }

  FOR_ALL_TELNET_COMMANDS(ptr, iterator) {
    if (_check_telnet_command(telnet, ptr, NULL)) {
      if (abuf_appendf(&telnet->session.out, "  %s\n", ptr->command) < 0) {
        return TELNET_RESULT_ABUF_ERROR;
      }
    }
  }

  if (abuf_puts(&telnet->session.out, "Use 'help <command> to see a help text for one command\n") < 0) {
    return TELNET_RESULT_ABUF_ERROR;
  }
  return TELNET_RESULT_ACTIVE;
}

static enum olsr_telnet_result
_telnet_echo(struct olsr_telnet_session *telnet,
    const char *cmd __attribute__ ((unused)), const char *param) {

  if (abuf_appendf(&telnet->session.out, "%s\n",
      param == NULL ? "" : param) < 0) {
    return TELNET_RESULT_ABUF_ERROR;
  }
  return TELNET_RESULT_ACTIVE;
}

static enum olsr_telnet_result
_telnet_timeout(struct olsr_telnet_session *telnet,
    const char *cmd __attribute__ ((unused)), const char *param) {
  uint32_t timeout;
  timeout = (uint32_t)strtoul(param, NULL, 10) * 1000;

  olsr_stream_set_timeout(&telnet->session, timeout);
  return TELNET_RESULT_ACTIVE;
}

static void
_telnet_repeat_stophandler(struct olsr_telnet_session *con) {
  olsr_timer_stop((struct olsr_timer_entry *)con->stop_data[0]);
  free(con->stop_data[1]);

  con->stop_handler = NULL;
  con->stop_data[0] = NULL;
  con->stop_data[1] = NULL;
  con->stop_data[2] = NULL;
}

static void
_telnet_repeat_timer(void *data) {
  struct olsr_telnet_session *con = data;

  if (_telnet_handle_command(con, con->stop_data[1], con->stop_data[2]) != TELNET_RESULT_ACTIVE) {
    con->stop_handler(con);
  }

  olsr_stream_flush(&con->session);
}

static enum olsr_telnet_result
_telnet_repeat(struct olsr_telnet_session *telnet,
    const char *cmd __attribute__ ((unused)), const char *param) {
  int interval = 0;
  char *ptr;
  struct olsr_timer_entry *timer;

  if (telnet->stop_handler) {
    abuf_puts(&telnet->session.out, "Error, you cannot stack continous output commands\n");
    return TELNET_RESULT_ACTIVE;
  }

  if (param == NULL || (ptr = strchr(param, ' ')) == NULL) {
    abuf_puts(&telnet->session.out, "Missing parameters for repeat\n");
    return TELNET_RESULT_ACTIVE;
  }

  ptr++;

  interval = atoi(param);

  timer = olsr_timer_start(interval * 1000, 0, telnet, _telnet_repeat_timerinfo);
  telnet->stop_handler = _telnet_repeat_stophandler;
  telnet->stop_data[0] = timer;
  telnet->stop_data[1] = strdup(ptr);
  telnet->stop_data[2] = NULL;

  /* split command/parameter and remember it */
  ptr = strchr(telnet->stop_data[1], ' ');
  if (ptr != NULL) {
    /* found a parameter */
    *ptr++ = 0;
    telnet->stop_data[2] = ptr;
  }

  /* start command the first time */
  if (_telnet_handle_command(telnet, telnet->stop_data[1], telnet->stop_data[2]) != TELNET_RESULT_ACTIVE) {
    telnet->stop_handler(telnet);
  }
  return TELNET_RESULT_CONTINOUS;
}

static enum olsr_telnet_result
_telnet_version(struct olsr_telnet_session *telnet,
    const char *cmd __attribute__ ((unused)), const char *param __attribute__ ((unused))) {
  olsr_builddata_printversion(&telnet->session.out);
  return TELNET_RESULT_ACTIVE;
}

static enum olsr_telnet_result
_telnet_plugin(struct olsr_telnet_session *telnet,
    const char *cmd __attribute__ ((unused)), const char *param __attribute__ ((unused))) {
  struct olsr_plugin *plugin, *iterator;
  char *para2 = NULL;

  if (param == NULL || strcasecmp(param, "list") == 0) {
    if (abuf_puts(&telnet->session.out, "Plugins:\n") < 0) {
      return TELNET_RESULT_ABUF_ERROR;
    }
    OLSR_FOR_ALL_PLUGIN_ENTRIES(plugin, iterator) {
      if (abuf_appendf(&telnet->session.out, " %-30s\t%s\t%s\n",
          plugin->name, olsr_plugins_is_enabled(plugin) ? "enabled" : "",
          olsr_plugins_is_static(plugin) ? "static" : "") < 0) {
        return TELNET_RESULT_ABUF_ERROR;
      }
    }
    return TELNET_RESULT_ACTIVE;
  }

  para2 = strchr(param, ' ');
  if (para2 == NULL) {
    if (abuf_appendf(&telnet->session.out, "Error, missing or unknown parameter\n") < 0) {
      return TELNET_RESULT_ABUF_ERROR;
    }
    return TELNET_RESULT_ACTIVE;
  }
  *para2++ = 0;

  plugin = olsr_plugins_get(para2);
  if (strcasecmp(param, "load") == 0) {
    if (plugin != NULL) {
      abuf_appendf(&telnet->session.out, "Plugin %s already loaded\n", para2);
      return TELNET_RESULT_ACTIVE;
    }
    plugin = olsr_plugins_load(para2);
    if (plugin != NULL) {
      abuf_appendf(&telnet->session.out, "Plugin %s successfully loaded\n", para2);
    }
    else {
      abuf_appendf(&telnet->session.out, "Could not load plugin %s\n", para2);
    }
    return TELNET_RESULT_ACTIVE;
  }

  if (plugin == NULL) {
    if (abuf_appendf(&telnet->session.out, "Error, could not find plugin '%s'.\n", para2) < 0) {
      return TELNET_RESULT_ABUF_ERROR;
    }
    return TELNET_RESULT_ACTIVE;
  }
  if (strcasecmp(param, "activate") == 0) {
    if (olsr_plugins_is_enabled(plugin)) {
      abuf_appendf(&telnet->session.out, "Plugin %s already active\n", para2);
    }
    else {
      if (olsr_plugins_enable(plugin)) {
        abuf_appendf(&telnet->session.out, "Could not activate plugin %s\n", para2);
      }
      else {
        abuf_appendf(&telnet->session.out, "Plugin %s successfully activated\n", para2);
      }
    }
  }
  else if (strcasecmp(param, "deactivate") == 0) {
    if (!olsr_plugins_is_enabled(plugin)) {
      abuf_appendf(&telnet->session.out, "Plugin %s is not active\n", para2);
    }
    else {
      if (olsr_plugins_disable(plugin)) {
        abuf_appendf(&telnet->session.out, "Could not deactivate plugin %s\n", para2);
      }
      else {
        abuf_appendf(&telnet->session.out, "Plugin %s successfully deactivated\n", para2);
      }
    }
  }
  else if (strcasecmp(param, "unload") == 0) {
    if (olsr_plugins_is_static(plugin)) {
      abuf_appendf(&telnet->session.out, "Plugin %s is static and cannot be unloaded\n", para2);
    }
    else {
      if (olsr_plugins_unload(plugin)) {
        abuf_appendf(&telnet->session.out, "Could not unload plugin %s\n", para2);
      }
      else {
        abuf_appendf(&telnet->session.out, "Plugin %s successfully unloaded\n", para2);
      }
    }
  }
  else {
    abuf_appendf(&telnet->session.out, "Unknown command '%s %s %s'.\n", cmd, param, para2);
  }
  return TELNET_RESULT_ACTIVE;
}
