/*
 * olsr_netaddr_acl.c
 *
 *  Created on: Sep 7, 2011
 *      Author: rogge
 */

#include "common/common_types.h"
#include "common/netaddr.h"

#include "config/cfg_db.h"
#include "config/cfg_schema.h"

#include "olsr_netaddr_acl.h"

static int _handle_control_cmd(struct olsr_netaddr_acl *, const char *);
static bool _is_in_array(struct netaddr *, size_t, struct netaddr *);

static const char *_FIRST_REJECT = "first_reject";
static const char *_FIRST_ACCEPT = "first_accept";
static const char *_DEFAULT_ACCEPT = "default_accept";
static const char *_DEFAULT_REJECT = "default_reject";

void
olsr_acl_add(struct olsr_netaddr_acl *acl) {
  memset(acl, 0, sizeof(acl));
}

int
olsr_acl_from_cfg_entry(struct olsr_netaddr_acl *acl, struct cfg_stringarray *array) {
  size_t accept_count, reject_count;
  char *ptr;
  accept_count = 0;
  reject_count = 0;

  /* clear acl struct */
  memset(acl, 0, sizeof(acl));

  /* count number of address entries */
  CFG_FOR_ALL_STRINGS(array, ptr) {
    if (_handle_control_cmd(acl, ptr) == 0) {
      continue;
    }

    if (ptr[0] == '-') {
      reject_count++;
    }
    else {
      accept_count++;
    }
  }

  /* allocate memory */
  if (accept_count > 0) {
    acl->accept = calloc(accept_count, sizeof(struct netaddr));
    if (acl->accept == NULL) {
      goto from_entry_error;
    }
  }
  if (reject_count > 0) {
    acl->reject = calloc(reject_count, sizeof(struct netaddr));
    if (acl->reject == NULL) {
      goto from_entry_error;
    }
  }

  /* read netaddr strings into buffers */
  CFG_FOR_ALL_STRINGS(array, ptr) {
    char *addr;
    if (_handle_control_cmd(acl, ptr) == 0) {
      continue;
    }

    addr = ptr;
    if (*ptr == '-' || *ptr == '+') {
      addr++;
    }

    if (*ptr == '-') {
      if (netaddr_from_string(&acl->reject[acl->reject_count], addr)) {
        goto from_entry_error;
      }
      acl->reject_count++;
    }
    else {
      if (netaddr_from_string(&acl->accept[acl->accept_count], addr)) {
         goto from_entry_error;
      }
      acl->accept_count++;
    }
  }
  return 0;

from_entry_error:
  free(acl->accept);
  free(acl->reject);
  memset(acl, 0, sizeof(*acl));
  return -1;
}

void
olsr_acl_remove(struct olsr_netaddr_acl *acl) {
  free(acl->accept);
  free(acl->reject);

  memset(acl, 0, sizeof(*acl));
}

int
olsr_acl_copy(struct olsr_netaddr_acl *to, struct olsr_netaddr_acl *from) {
  olsr_acl_remove(to);
  memcpy(to, from, sizeof(*to));

  if (to->accept_count) {
    to->accept = calloc(to->accept_count, sizeof(struct netaddr));
    if (to->accept == NULL) {
      return -1;
    }
    memcpy(to->accept, from->accept, to->accept_count * sizeof(struct netaddr));
  }

  if (to->reject_count) {
    to->reject = calloc(to->reject_count, sizeof(struct netaddr));
    if (to->reject == NULL) {
      return -1;
    }
    memcpy(to->reject, from->reject, to->reject_count * sizeof(struct netaddr));
  }
  return 0;
}

bool
olsr_acl_check_accept(struct olsr_netaddr_acl *acl, struct netaddr *addr) {
  if (acl->reject_first) {
    if (_is_in_array(acl->reject, acl->reject_count, addr)) {
      return false;
    }
  }

  if (_is_in_array(acl->accept, acl->accept_count, addr)) {
    return true;
  }

  if (!acl->reject_first) {
    if (_is_in_array(acl->reject, acl->reject_count, addr)) {
      return false;
    }
  }

  return acl->accept_default;
}

/**
 * Schema entry validator for access control lists
 * See CFG_VALIDATE_ACL_() macros
 * @param entry pointer to schema entry
 * @param section_name name of section type and name
 * @param value value of schema entry
 * @param out pointer to autobuffer for validator output
 * @return 0 if validation found no problems, -1 otherwise
 */
int
olsr_acl_validate(const struct cfg_schema_entry *entry,
    const char *section_name, const char *value, struct autobuf *out) {
  struct olsr_netaddr_acl dummy;

  if (value == NULL) {
    cfg_schema_validate_netaddr(entry, section_name, value, out);
    cfg_append_printable_line(out, "    Additional keywords are %s, %s, %s and %s",
        _FIRST_ACCEPT, _FIRST_REJECT, _DEFAULT_ACCEPT, _DEFAULT_REJECT);
    return 0;
  }

  if (_handle_control_cmd(&dummy, value) == 0) {
    return 0;
  }

  if (*value == '+' || *value == '-') {
    return cfg_schema_validate_netaddr(entry, section_name, value+1, out);
  }
  return cfg_schema_validate_netaddr(entry, section_name, value, out);
}

int
olsr_acl_tobin(const struct cfg_schema_entry *s_entry __attribute__((unused)),
    struct cfg_stringarray *value, void *reference) {
  struct olsr_netaddr_acl *ptr;

  ptr = (struct olsr_netaddr_acl *)reference;

  free(ptr->accept);
  free(ptr->reject);

  return olsr_acl_from_cfg_entry(ptr, value);
}

static int
_handle_control_cmd(struct olsr_netaddr_acl *acl, const char *cmd) {
  if (strcasecmp(cmd, _DEFAULT_ACCEPT) == 0) {
    acl->accept_default = true;
  }
  else if (strcasecmp(cmd, _DEFAULT_REJECT) == 0) {
    acl->accept_default = false;
  }
  else if (strcasecmp(cmd, _FIRST_ACCEPT) == 0) {
    acl->reject_first = false;
  }
  else if (strcasecmp(cmd, _FIRST_REJECT) == 0) {
    acl->reject_first = true;
  }
  else {
    /* no control command, must be an address */
    return -1;
  }

  /* was one of the four valid control commands */
  return 0;
}

static bool
_is_in_array(struct netaddr *array, size_t length, struct netaddr *addr) {
  size_t i;

  for (i=0; i<length; i++) {
    if (netaddr_is_in_subnet(&array[i], addr)) {
      return true;
    }
  }
  return false;
}
