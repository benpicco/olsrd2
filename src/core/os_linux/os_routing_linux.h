/*
 * os_routing_linux.h
 *
 *  Created on: Feb 13, 2012
 *      Author: rogge
 */

#ifndef OS_ROUTING_LINUX_H_
#define OS_ROUTING_LINUX_H_

#ifndef OS_NET_SPECIFIC_INCLUDE
#error "DO not include this file directly, always use 'os_system.h'"
#endif

#include "common/common_types.h"
#include "common/list.h"
#include "os_helper.h"

struct os_route_internal {
  struct list_entity _node;

  uint32_t nl_seq;
};
#endif /* OS_ROUTING_LINUX_H_ */
