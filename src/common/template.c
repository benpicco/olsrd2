
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

#include "common/common_types.h"
#include "common/autobuf.h"
#include "common/template.h"

static int abuf_find_template(const char **keys, size_t tmplLength,
    const char *txt, size_t txtLength);

/**
 * Initialize an index table for a template engine.
 * Each usage of a key in the format has to be %key%.
 * The existing keys (start, end, key-number) will be recorded
 * in the integer array the user provided, so the template
 * engine can replace them with the values later.
 *
 * @param keys array of keys for the template engine
 * @param tmplLength number of keys
 * @param format format string of the template
 * @param indexTable pointer to an template_storage array with a minimum
 *   length equals to the number of keys used in the format string
 * @param indexLength length of the size_t array
 * @return number of indices written into index table,
 *   -1 if an error happened
 */
int
abuf_template_init (const char **keys, size_t tmplLength, const char *format,
    struct abuf_template_storage *indexTable, size_t indexLength) {
  size_t pos = 0, indexCount = 0;
  size_t start = 0;
  int i = 0;
  bool escape = false;
  bool no_open_format = true;

  while (format[pos]) {
    if (!escape && format[pos] == '%') {
      if (no_open_format) {
        start = pos++;
        no_open_format = false;
        continue;
      }
      if (pos - start > 1) {
        if (indexCount+1 > indexLength) {
          return -1;
        }

        i = abuf_find_template(keys, tmplLength, &format[start+1], pos-start-1);
        if (i != -1) {
          indexTable[indexCount].start = start;
          indexTable[indexCount].end = pos+1;
          indexTable[indexCount].key_index = (size_t)i;

          indexCount++;
        }
      }
      no_open_format = true;
    }
    else if (format[pos] == '\\') {
      /* handle "\\" and "\%" in text */
      escape = !escape;
    }
    else {
      escape = false;
    }

    pos++;
  }
  return (int)indexCount;
}

/**
 * Append the result of a template engine into an autobuffer.
 * Each usage of a key will be replaced with the corresponding
 * value.
 * @param autobuf pointer to autobuf object
 * @param format format string (as supplied to abuf_template_init()
 * @param values array of values (same number as keys)
 * @param indexTable pointer to index table initialized by abuf_template_init()
 * @param indexCount length of index table as returned by abuf_template_init()
 * @return -1 if an out-of-memory error happened, 0 otherwise
 */
int
abuf_templatef (struct autobuf *autobuf, const char *format,
    const char **values, struct abuf_template_storage *indexTable, size_t indexCount) {
  size_t i, last = 0;

  if (autobuf == NULL) return 0;

  for (i=0; i<indexCount; i++) {
    /* copy prefix text */
    if (last < indexTable[i].start) {
      if (abuf_memcpy(autobuf, &format[last], indexTable[i].start - last) < 0) {
        return -1;
      }
    }
    if (abuf_puts(autobuf, values[indexTable[i].key_index]) < 0) {
      return -1;
    }
    last = indexTable[i].end;
  }

  if (last < strlen(format)) {
    if (abuf_puts(autobuf, &format[last]) < 0) {
      return -1;
    }
  }
  return 0;
}

/**
 * Find the position of one member of a string array in a text.
 * @param keys pointer to string array
 * @param tmplLength number of strings in array
 * @param txt pointer to text to search in
 * @param txtLength length of text to search in
 * @return index in array found in text, -1 if no string matched
 */
static int
abuf_find_template(const char **keys, size_t tmplLength, const char *txt, size_t txtLength) {
  size_t i;

  for (i=0; i<tmplLength; i++) {
    if (strncmp(keys[i], txt, txtLength) == 0 && keys[i][txtLength] == 0) {
      return (int)i;
    }
  }
  return -1;
}
