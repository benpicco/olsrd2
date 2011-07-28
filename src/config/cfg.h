
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

#ifndef CFG_H_
#define CFG_H_

#include <ctype.h>

#include "common/autobuf.h"
#include "common/common_types.h"

#ifndef ARRAYSIZE
#define ARRAYSIZE(a)  (sizeof(a) / sizeof(*(a)))
#endif

EXPORT int cfg_append_printable_line(struct autobuf *autobuf, const char *fmt, ...)
  __attribute__ ((format(printf, 2, 3)));
EXPORT bool cfg_is_printable(const char *value);
EXPORT bool cfg_is_allowed_key(const char *key);
EXPORT int cfg_get_choice_index(const char *value, const char **array, size_t array_size);

EXPORT int cfg_avlcmp_keys(const void *p1, const void *p2, void *unused);

EXPORT const char *CFGLIST_BOOL_TRUE[4];
EXPORT const char *CFGLIST_BOOL[8];

/**
 * Compares to keys/names of two section types/names or entry names.
 * A NULL pointer is considered larger than any valid string.
 * @param str1 first key
 * @param str2 second key
 * @return similar to strcmp()
 */
static INLINE int
cfg_cmp_keys(const char *str1, const char *str2) {
  return cfg_avlcmp_keys(str1, str2, NULL);
}

/**
 * Checks if a string value represents a positive boolean value
 * @param pointer to string
 * @return boolean value of the string representation
 */
static INLINE bool
cfg_get_bool(const char *value) {
  return cfg_get_choice_index(value, CFGLIST_BOOL_TRUE, ARRAYSIZE(CFGLIST_BOOL_TRUE)) >= 0;
}

#endif /* CFG_H_ */
