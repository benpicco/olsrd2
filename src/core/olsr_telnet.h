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
#include "olsr_netaddr_acl.h"
#include "olsr_stream_socket.h"

enum olsr_txtcommand_result {
  ACTIVE,
  CONTINOUS,
  QUIT,
  ABUF_ERROR,
  UNKNOWN,
};

struct olsr_telnet_session {
  struct olsr_stream_session session;

  /* remember if echo mode is active */
  bool show_echo;

  /* millisecond timeout between commands */
  uint32_t timeout_value;

  /* callback and data to stop a continous output txt command */
  void (*stop_handler)(struct olsr_telnet_session *);
  void *stop_data[4];

};

typedef enum olsr_txtcommand_result (*olsr_telnethandler)
    (struct olsr_telnet_session *con, const char *command, const char *parameter);

struct olsr_telnet_command {
  struct avl_node node;
  const char *command;

  const char *help;

  struct olsr_netaddr_acl acl;

  olsr_telnethandler handler;
  olsr_telnethandler help_handler;
};

#define FOR_ALL_TELNET_COMMANDS(cmd, ptr) avl_for_each_element_safe(&telnet_cmd_tree, cmd, node, ptr)
EXPORT struct avl_tree telnet_cmd_tree;

int olsr_telnet_init(void);
void olsr_telnet_cleanup(void);

EXPORT int olsr_telnet_add(struct olsr_telnet_command *command);
EXPORT void olsr_telnet_remove(struct olsr_telnet_command *command);

#endif /* OLSR_TELNET_H_ */
