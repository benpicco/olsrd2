/*
 * olsr_netaddr_acl.h
 *
 *  Created on: Sep 7, 2011
 *      Author: rogge
 */

#ifndef OLSR_NETADDR_ACL_H_
#define OLSR_NETADDR_ACL_H_

#include "common/common_types.h"
#include "common/netaddr.h"

#include "config/cfg_schema.h"

#define CFG_VALIDATE_ACL(p_name, p_def, p_help, args...)         _CFG_VALIDATE(p_name, p_def, p_help, .cb_validate = olsr_acl_validate, .list = true, .validate_params = {.p_i1 = 0, .p_i2 = -1 }, ##args )
#define CFG_VALIDATE_ACL_HWADDR(p_name, p_def, p_help, args...)  _CFG_VALIDATE(p_name, p_def, p_help, .cb_validate = olsr_acl_validate, .list = true, .validate_params = {.p_i1 = -AF_MAC48, .p_i2 = AF_EUI64 }, ##args )
#define CFG_VALIDATE_ACL_MAC48(p_name, p_def, p_help, args...)   _CFG_VALIDATE(p_name, p_def, p_help, .cb_validate = olsr_acl_validate, .list = true, .validate_params = {.p_i1 = -AF_MAC48}, ##args )
#define CFG_VALIDATE_ACL_EUI64(p_name, p_def, p_help, args...)   _CFG_VALIDATE(p_name, p_def, p_help, .cb_validate = olsr_acl_validate, .list = true, .validate_params = {.p_i1 = -AF_EUI64}, ##args )
#define CFG_VALIDATE_ACL_V4(p_name, p_def, p_help, args...)      _CFG_VALIDATE(p_name, p_def, p_help, .cb_validate = olsr_acl_validate, .list = true, .validate_params = {.p_i1 = -AF_INET }, ##args )
#define CFG_VALIDATE_ACL_V6(p_name, p_def, p_help, args...)      _CFG_VALIDATE(p_name, p_def, p_help, .cb_validate = olsr_acl_validate, .list = true, .validate_params = {.p_i1 = -AF_INET6 }, ##args )
#define CFG_VALIDATE_ACL_V46(p_name, p_def, p_help, args...)     _CFG_VALIDATE(p_name, p_def, p_help, .cb_validate = olsr_acl_validate, .list = true, .validate_params = {.p_i1 = -AF_INET, .p_i2 = AF_INET6}, ##args )

#define CFG_MAP_ACL(p_reference, p_field, p_name, p_def, p_help, args...)         CFG_VALIDATE_ACL(p_name, p_def, p_help, .cb_to_binary = olsr_acl_tobin, .bin_offset = offsetof(struct p_reference, p_field), ##args)
#define CFG_MAP_ACL_HWADDR(p_reference, p_field, p_name, p_def, p_help, args...)  CFG_VALIDATE_ACL_HWADDR(p_name, p_def, p_help, .cb_to_binary = olsr_acl_tobin, .bin_offset = offsetof(struct p_reference, p_field), ##args)
#define CFG_MAP_ACL_MAC48(p_reference, p_field, p_name, p_def, p_help, args...)   CFG_VALIDATE_ACL_MAC48(p_name, p_def, p_help, .cb_to_binary = olsr_acl_tobin, .bin_offset = offsetof(struct p_reference, p_field), ##args)
#define CFG_MAP_ACL_EUI64(p_reference, p_field, p_name, p_def, p_help, args...)   CFG_VALIDATE_ACL_EUI64(p_name, p_def, p_help, .cb_to_binary = olsr_acl_tobin, .bin_offset = offsetof(struct p_reference, p_field), ##args)
#define CFG_MAP_ACL_V4(p_reference, p_field, p_name, p_def, p_help, args...)      CFG_VALIDATE_ACL_V4(p_name, p_def, p_help, .cb_to_binary = olsr_acl_tobin, .bin_offset = offsetof(struct p_reference, p_field), ##args)
#define CFG_MAP_ACL_V6(p_reference, p_field, p_name, p_def, p_help, args...)      CFG_VALIDATE_ACL_V6(p_name, p_def, p_help, .cb_to_binary = olsr_acl_tobin, .bin_offset = offsetof(struct p_reference, p_field), ##args)
#define CFG_MAP_ACL_V46(p_reference, p_field, p_name, p_def, p_help, args...)     CFG_VALIDATE_ACL_V46(p_name, p_def, p_help, .cb_to_binary = olsr_acl_tobin, .bin_offset = offsetof(struct p_reference, p_field), ##args)

struct olsr_netaddr_acl {
  struct netaddr *accept;
  size_t accept_count;

  struct netaddr *reject;
  size_t reject_count;

  bool reject_first;
  bool accept_default;
};

EXPORT void olsr_acl_add(struct olsr_netaddr_acl *);
EXPORT int olsr_acl_from_strarray(struct olsr_netaddr_acl *, const struct const_strarray *value);
EXPORT void olsr_acl_remove(struct olsr_netaddr_acl *);
EXPORT int olsr_acl_copy(struct olsr_netaddr_acl *to, struct olsr_netaddr_acl *from);

EXPORT bool olsr_acl_check_accept(struct olsr_netaddr_acl *, struct netaddr *);

EXPORT int olsr_acl_validate(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out);
EXPORT int olsr_acl_tobin(const struct cfg_schema_entry *s_entry,
    const struct const_strarray *value, void *reference);

#endif /* OLSR_NETADDR_ACL_H_ */
