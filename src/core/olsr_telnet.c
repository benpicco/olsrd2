/*
 * olsr_telnet.c
 *
 *  Created on: Sep 7, 2011
 *      Author: rogge
 */

#include "common/common_types.h"
#include "common/avl.h"

#include "config/cfg_delta.h"
#include "config/cfg_schema.h"

#include "olsr_cfg.h"
#include "olsr_logging.h"
#include "olsr_memcookie.h"
#include "olsr_netaddr_acl.h"
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

struct telnet_config {
  struct olsr_netaddr_acl acl;
  struct netaddr bindto_v4;
  struct netaddr bindto_v6;
  uint16_t port;
};

/* static function prototypes */
static void _config_changed(void);
static int _apply_config(struct cfg_named_section *section);
static int _setup_socket(struct olsr_stream_socket *sock,
    struct netaddr *bindto, uint16_t port);
static void _telnet_init(struct olsr_stream_session *);
static void _telnet_cleanup(struct olsr_stream_session *);
static void _telnet_create_error(struct olsr_stream_session *,
    enum olsr_stream_errors);
static enum olsr_stream_session_state _telnet_receive_data(
    struct olsr_stream_session *);

/* global and static variables */
static struct cfg_schema_section telnet_section = {
  .t_type = _CFG_TELNET_SECTION,
  .t_help = "Settings for the telnet interface",
};

static struct cfg_schema_entry telnet_entries[] = {
  [_CFG_TELNET_ACL] = CFG_MAP_ACL_V46(telnet_config, acl, "127.0.0.1",
      "Access control list for telnet interface"),
  [_CFG_TELNET_BINDTO_V4] = CFG_MAP_NETADDR_V4(telnet_config, bindto_v4, "127.0.0.1",
      "Bind ipv4 socket to this address", false),
  [_CFG_TELNET_BINDTO_V6] = CFG_MAP_NETADDR_V6(telnet_config, bindto_v6, "::1",
      "Bind ipv6 socket to this address", false),
  [_CFG_TELNET_PORT] = CFG_MAP_INT_MINMAX(telnet_config, port, "2006",
      "Network port for telnet interface", 1, 65535),
};

static struct cfg_delta_filter telnet_filter[ARRAYSIZE(telnet_entries) + 1];

static struct cfg_delta_handler telnet_handler;

/* configuration of telnet interface */
static struct telnet_config _telnet_config;
static struct olsr_stream_socket _telnet_v4, _telnet_v6;
static struct olsr_netaddr_acl _telnet_acl;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_telnet_state);

/* memcookie for telnet sessions */
static struct olsr_memcookie_info *_telnet_memcookie;

/* tree of telnet commands */
struct avl_tree telnet_cmd_tree;

int
olsr_telnet_init(void) {
  struct cfg_named_section *section;
  if (olsr_subsystem_init(&olsr_telnet_state))
    return 0;

  _telnet_memcookie = olsr_memcookie_add("telnet session",
      sizeof(struct olsr_telnet_session));
  if (_telnet_memcookie == NULL) {
    return -1;
  }

  cfg_schema_add_section(olsr_cfg_get_schema(), &telnet_section);
  cfg_schema_add_entries(&telnet_section, telnet_entries, ARRAYSIZE(telnet_entries));

  cfg_delta_add_handler_by_schema(
      olsr_cfg_get_delta(), _config_changed, 0, &telnet_handler,
      &telnet_filter[0], &telnet_section, &telnet_entries[0],
      ARRAYSIZE(telnet_entries));

  memset(&_telnet_config, 0, sizeof(_telnet_config));
  memset(&_telnet_v4, 0, sizeof(_telnet_v4));
  memset(&_telnet_v6, 0, sizeof(_telnet_v6));

  section = cfg_db_find_unnamedsection(olsr_cfg_get_rawdb(),
      _CFG_TELNET_SECTION);
  if (_apply_config(section)) {
    olsr_memcookie_remove(_telnet_memcookie);
    olsr_subsystem_cleanup(&olsr_telnet_state);
    return -1;
  }
  return 0;
}

void
olsr_telnet_cleanup(void) {
  if (olsr_subsystem_cleanup(&olsr_telnet_state))
    return;

  olsr_stream_remove(&_telnet_v4);
  olsr_stream_remove(&_telnet_v6);

  cfg_delta_remove_handler(olsr_cfg_get_delta(), &telnet_handler);
  cfg_schema_remove_section(olsr_cfg_get_schema(), &telnet_section);

  olsr_stream_remove(&_telnet_v4);
  olsr_stream_remove(&_telnet_v6);

  olsr_memcookie_remove(_telnet_memcookie);
}

int
olsr_telnet_add(struct olsr_telnet_command *command) {
  command->node.key = command->command;
  if (avl_insert(&telnet_cmd_tree, &command->node)) {
    return -1;
  }

  /* default for empty acl should be 'accept' */
  if (command->acl.accept_count + command->acl.reject_count == 0) {
    command->acl.accept_default = true;
  }
  return 0;
}

void
olsr_telnet_remove(struct olsr_telnet_command *command) {
  avl_remove(&telnet_cmd_tree, &command->node);
}




static void
_config_changed(void) {
  _apply_config(telnet_handler.post);
}

static int
_apply_config(struct cfg_named_section *section) {
  struct telnet_config config;

  /* clear old config */
  memset(&config, 0, sizeof(config));

  if (cfg_schema_tobin(&config, section, telnet_entries, ARRAYSIZE(telnet_entries))) {
    // error
    OLSR_WARN(LOG_TELNET, "Cannot map telnet config to binary data");
    return -1;
  }

  /* copy acl */
  memcpy(&_telnet_acl, &config.acl, sizeof(_telnet_acl));

  /* refresh sockets */
  if (_setup_socket(&_telnet_v4, &config.bindto_v4, config.port)) {
    return -1;
  }
  if (_setup_socket(&_telnet_v6, &config.bindto_v6, config.port)) {
    return -1;
  }
  return 0;
}

static int
_setup_socket(struct olsr_stream_socket *sock, struct netaddr *bindto, uint16_t port) {
  union netaddr_socket local;
  struct netaddr_str buf;

  /* generate local socket data */
  netaddr_socket_init(&local, bindto, port);
  OLSR_INFO(LOG_TELNET, "Opening telnet socket %s", netaddr_socket_to_string(&buf, &local));

  /* check if change is really necessary */
  if (memcmp(&sock->local_socket, &local, sizeof(local)) == 0) {
    /* nothing to do */
    return 0;
  }

  /* free olsr socket resources */
  olsr_stream_remove(sock);

  /* initialize new socket */
  if (olsr_stream_add(sock, &local)) {
    memset(sock, 0, sizeof(*sock));
    return -1;
  }
  sock->session_timeout = 120000; /* 120 seconds */
  sock->maximum_input_buffer = 4096;
  sock->allowed_sessions = 3;
  sock->memcookie = _telnet_memcookie;
  sock->init = _telnet_init;
  sock->cleanup = _telnet_cleanup;
  sock->receive_data = _telnet_receive_data;
  sock->create_error = _telnet_create_error;
  return 0;
}

static void
_telnet_init(struct olsr_stream_session *session) {
  struct olsr_telnet_session *telnet_session;

  /* get telnet session pointer */
  telnet_session = (struct olsr_telnet_session *)session;

  telnet_session->show_echo = true;
  telnet_session->stop_handler = NULL;
  telnet_session->timeout_value = 120000;
}

static void
_telnet_cleanup(struct olsr_stream_session *session) {
  struct olsr_telnet_session *telnet_session;

  /* get telnet session pointer */
  telnet_session = (struct olsr_telnet_session *)session;
  if (telnet_session->stop_handler) {
    telnet_session->stop_handler(telnet_session);
    telnet_session->stop_handler = NULL;
  }
}

static void
_telnet_create_error(struct olsr_stream_session *session,
    enum olsr_stream_errors error) {
  switch(error) {
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
  enum olsr_txtcommand_result res;
  char *eol;
  int len;
  bool processedCommand = false, chainCommands = false;
  uint32_t old_timeout;

  struct netaddr remote;
  int ret;

  /* get telnet session pointer */
  telnet_session = (struct olsr_telnet_session *)session;

  /* check ACL */
  ret = netaddr_from_socket(&remote, &session->peer_addr);
  if (ret != 0 || !olsr_acl_check_accept(&_telnet_acl, &remote)) {
    struct netaddr_str buf;
    OLSR_INFO(LOG_TELNET, "Illegal access from %s",
        netaddr_socket_to_string(&buf, &session->peer_addr));

    return STREAM_SESSION_SEND_AND_QUIT;
  }

  old_timeout = session->timeout->timer_period;

  /* loop over input */
  while (session->in.len > 0 && session->state == STREAM_SESSION_ACTIVE) {
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
        res = ACTIVE;
        // TODO res = olsr_com_handle_txtcommand(con, cmd, para);
        switch (res) {
          case ACTIVE:
            break;
          case CONTINOUS:
            break;
          case ABUF_ERROR:
            session->out.len = len;
            abuf_appendf(&session->out,
                "Error in autobuffer during command '%s'.\n", cmd);
            break;
          case UNKNOWN:
            session->out.len = len;
            abuf_appendf(&session->out, "Error, unknown command '%s'\n", cmd);
            break;
          case QUIT:
            session->state = STREAM_SESSION_SEND_AND_QUIT;
            break;
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
      session->state = STREAM_SESSION_SEND_AND_QUIT;
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
