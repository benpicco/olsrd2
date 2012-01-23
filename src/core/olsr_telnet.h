/*
 * olsr_telnet.h
 *
 *  Created on: Sep 7, 2011
 *      Author: rogge
 */

#ifndef OLSR_TELNET_H_
#define OLSR_TELNET_H_

#include "common/common_types.h"
#include "common/avl.h"
#include "common/list.h"
#include "olsr_netaddr_acl.h"
#include "olsr_stream_socket.h"

enum olsr_telnet_result {
  TELNET_RESULT_ACTIVE,
  TELNET_RESULT_CONTINOUS,
  TELNET_RESULT_INTERNAL_ERROR,
  TELNET_RESULT_QUIT,
  TELNET_RESULT_UNKNOWN_COMMAND,
};

struct olsr_telnet_cleanup {
  struct list_entity node;
  struct olsr_telnet_data *data;

  void (*cleanup_handler)(struct olsr_telnet_cleanup *);
  void *custom;
};

struct olsr_telnet_data {
  /* address of remote communication partner */
  struct netaddr *remote;

  /* output buffer for telnet commands */
  struct autobuf *out;

  /* current telnet command and parameters */
  const char *command;
  const char *parameter;

  /* remember if echo mode is active */
  bool show_echo;

  /* millisecond timeout between commands */
  uint32_t timeout_value;

  /* callback and data to stop a continous output txt command */
  void (*stop_handler)(struct olsr_telnet_data *);
  void *stop_data[4];

  struct list_entity cleanup_list;
};

struct olsr_telnet_session {
  struct olsr_stream_session session;
  struct olsr_telnet_data data;
};

typedef enum olsr_telnet_result (*olsr_telnethandler)
    (struct olsr_telnet_data *con);

#if !defined(REMOVE_HELPTEXT)
#define TELNET_CMD(cmd, cb, helptext, args...) { .command = (cmd), .handler = (cb), .help = helptext, ##args }
#else
#define TELNET_CMD(cmd, cb, helptext, args...) { .command = (cmd), .handler = (cb), .help = "", ##args }
#endif

struct olsr_telnet_command {
  struct avl_node node;
  const char *command;

  const char *help;

  struct olsr_netaddr_acl *acl;

  olsr_telnethandler handler;
  olsr_telnethandler help_handler;
};

#define FOR_ALL_TELNET_COMMANDS(cmd, ptr) avl_for_each_element_safe(&telnet_cmd_tree, cmd, node, ptr)
EXPORT struct avl_tree telnet_cmd_tree;

EXPORT int olsr_telnet_init(void) __attribute__((warn_unused_result));
EXPORT void olsr_telnet_cleanup(void);

EXPORT int olsr_telnet_add(struct olsr_telnet_command *command);
EXPORT void olsr_telnet_remove(struct olsr_telnet_command *command);

EXPORT void olsr_telnet_stop(struct olsr_telnet_data *data);

EXPORT enum olsr_telnet_result olsr_telnet_execute(
    const char *cmd, const char *para,
    struct autobuf *out, struct netaddr *remote);

/**
 * Add a cleanup handler to a telnet session
 * @param data pointer to telnet data
 * @param cleanup pointer to initialized cleanup handler
 */
static INLINE void
olsr_telnet_add_cleanup(struct olsr_telnet_data *data,
    struct olsr_telnet_cleanup *cleanup) {
  cleanup->data = data;
  list_add_tail(&data->cleanup_list, &cleanup->node);
}

/**
 * Removes a cleanup handler to a telnet session
 * @param cleanup pointer to cleanup handler
 */
static INLINE void
olsr_telnet_remove_cleanup(struct olsr_telnet_cleanup *cleanup) {
  list_remove(&cleanup->node);
}

#endif /* OLSR_TELNET_H_ */
