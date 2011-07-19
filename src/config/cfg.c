
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2011, the olsr.org team - see HISTORY file
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

#include <strings.h>

#include "common/autobuf.h"
#include "common/common_types.h"
#include "config/cfg.h"

const char *CFGLIST_BOOL_TRUE[] = { "true", "1", "on", "yes" };
const char *CFGLIST_BOOL[] = { "true", "1", "on", "yes", "false", "0", "off", "no" };

/**
 * Appends a single line to an autobuffer.
 * The function replaces all non-printable characters with '.'
 * and will append a newline at the end
 * @param autobuf pointer to autobuf object
 * @param format printf format string
 * @return -1 if an out-of-memory error happened, 0 otherwise
 */
int
cfg_append_printable_line(struct autobuf *autobuf, const char *fmt, ...) {
  unsigned char *_value;
  size_t len;
  int rv;
  va_list ap;

  if (autobuf == NULL) return 0;

  _value = (unsigned char *)autobuf->buf + autobuf->len;
  len = autobuf->len;

  va_start(ap, fmt);
  rv = abuf_vappendf(autobuf, fmt, ap);
  va_end(ap);

  if (rv < 0) {
    return rv;
  }

  /* convert everything non-printable to '.' */
  while (*_value && len++ < autobuf->len) {
    if (*_value < 32 || *_value == 127 || *_value == 255) {
      *_value = '.';
    }
    _value++;
  }
  abuf_append_uint8(autobuf, '\n');
  return 0;
}

/**
 * Printable is defined as all ascii characters >= 32 except
 * 127 and 255.
 * @param value stringpointer
 * @return true if string only contains printable characters,
 *   false otherwise
 */
bool
cfg_is_printable(const char *value) {
  const unsigned char *_value;

  _value = (const unsigned char *)value;

  while (*_value) {
    if (*_value < 32 || *_value == 127 || *_value == 255) {
      return false;
    }
    _value++;
  }
  return true;
}

/**
 * Tests on the pattern [a-zA-Z_][a-zA-Z0-9_]*
 * @param key section_type/name or entry name
 * @return true if input string is valid for this parser,
 *   false otherwise
 */
bool
cfg_is_allowed_key(const char *key) {
  static const char *valid = "_0123456789"
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  /* test for [a-zA-Z_][a-zA-Z0-9_]* */
  if (*key >= '0' && *key <= '9') {
    return false;
  }

  return key[strspn(key, valid)] == 0;
}

/**
 * Null-pointer safe avl compare function for keys implementation.
 * NULL is considered a string greater than all normal strings.
 * @param p1 pointer to key 1
 * @param p2 pointer to key 2
 * @return similar to strcmp()
 */
int
cfg_avlcmp_keys(const void *p1, const void *p2, void *unused __attribute__((unused))) {
  const char *str1 = p1;
  const char *str2 = p2;

  if (str1 == NULL) {
    return str2 == NULL ? 0 : 1;
  }
  if (str2 == NULL) {
    return -1;
  }

  return strcasecmp(str1, str2);
}

/**
 * Looks up the index of a string within a string array
 * @param key pointer to string to be looked up in the array
 * @param array pointer to string pointer array
 * @param array_size number of strings in array
 * @return index of the string inside the array, -1 if not found
 */
int
cfg_get_choice_index(const char *key, const char **array, size_t array_size) {
  size_t i;

  for (i=0; i<array_size; i++) {
    if (strcasecmp(key, array[i]) == 0) {
      return (int) i;
    }
  }
  return -1;
}
