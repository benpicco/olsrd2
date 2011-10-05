
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

#ifndef COMMON_STRING_H_
#define COMMON_STRING_H_

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "common/common_types.h"

/*
 * Represents a string or an array of strings
 * The strings (including there Zero-Byte) are just appended
 * into a large binary buffer. The struct contains a pointer
 * to the first and to the last string and the size of the
 * binary buffer
 *
 * typically append operations are done by realloc() calls
 * while remove operations are done with memmove
 */
struct strarray {
  /* pointer to the first string */
  char *value;

  /* pointer to the last string */
  char *last_value;

  /* total length of all strings including zero-bytes */
  size_t length;
};

EXPORT char *strscpy (char *dest, const char *src, size_t size);
EXPORT char *strscat (char *dest, const char *src, size_t size);
EXPORT void str_trim (char **ptr);

EXPORT int strarray_copy(struct strarray *dst, struct strarray *src);
EXPORT int strarray_append(struct strarray *, const char *);
EXPORT void strarray_remove_ext(struct strarray *, char *, bool);

EXPORT char *strarray_get(struct strarray *array, size_t idx);
EXPORT size_t strarray_get_count(struct strarray *array);

/**
 * Initialize string array object
 * @param array pointer to string array object
 */
static INLINE void
strarray_init(struct strarray *array) {
  memset(array, 0, sizeof(*array));
}

/**
 * Free memory of string array object
 * @param array pointer to string array object
 */
static INLINE void
strarray_free(struct strarray *array) {
  free(array->value);
  strarray_init(array);
}

/**
 * Remove an element from a string array
 * @param array pointer to string array object
 * @param element an element to be removed from the array
 */
static INLINE void
strarray_remove(struct strarray *array, char *element) {
  strarray_remove_ext(array, element, true);
}

/**
 * @param array pointer to strarray object
 * @return pointer to first string of string array
 */
static INLINE char *
strarray_get_first(struct strarray *array) {
  return array->value;
}

/**
 * @param array pointer to strarray object
 * @return pointer to last string of string array
 */
static INLINE char *
strarray_get_last(struct strarray *array) {
  return array->last_value;
}

/**
 * Do not call this function for the last string in
 * a string array.
 * @param array pointer to strarray object
 * @param current pointer to a string in array
 * @return pointer to next string in string array
 */
static INLINE char *
strarray_get_next(struct strarray *array __attribute__((unused)),
    char *current) {
  return current + strlen(current) + 1;
}

/**
 * @param array pointer to strarray object
 * @param current pointer to a string in array
 * @return pointer to next string in string array,
 *   NULL if there is no further string
 */
static INLINE char *
strarray_get_next_safe(struct strarray *array, char *current) {
  if (current == array->last_value) {
    return NULL;
  }
  return current + strlen(current) + 1;
}

/**
 * Loop over an array of strings. This loop should not be used if elements are
 * removed from the array during the loop.
 *
 * @param array pointer to strarray object
 * @param charptr pointer to loop variable
 */
#define FOR_ALL_STRINGS(array, charptr) for (charptr = strarray_get_first(array); charptr != NULL && charptr <= strarray_get_last(array); charptr = strarray_get_next(array, charptr))

#endif
