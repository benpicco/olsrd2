/*
 * olsr_telnet.h
 *
 *  Created on: Sep 7, 2011
 *      Author: rogge
 */

#ifndef OLSR_TELNET_H_
#define OLSR_TELNET_H_

#include "common/common_types.h"

typedef enum olsr_txtcommand_result (*olsr_txthandler)
    (void *con, const char *command, const char *parameter);

struct olsr_txtcommand {
  struct avl_node node;
  const char *command;

  const char *help;

  struct ip_acl *acl;

  olsr_txthandler handler;
  olsr_txthandler help_handler;
};

void olsr_telnet_init(void);
void olsr_telnet_cleanup(void);

//EXPORT int olsr_telnet_add(struct olsr_telnet_command *command);
//EXPORT void olsr_telnet_remove(struct olsr_telnet_command *command);

#endif /* OLSR_TELNET_H_ */
