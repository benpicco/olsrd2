
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

#ifndef CFG_MEMORY_H_
#define CFG_MEMORY_H_

#include <string.h>
#include <stdlib.h>

#include "common/common_types.h"

/* one block of allocated memory */
#ifdef CFG_MEMORY_MANAGER
struct cfg_memory_block {
  /* node for list in cfg_memory */
  struct list_entity node;

  /* pointer to allocated block */
  void *ptr;
};
#endif /* CFG_MEMORY_MANAGER */

/*
 * Represents a memory allocator
 * for a single configuration database
 */
struct cfg_memory {
#ifdef CFG_MEMORY_MANAGER
  /* list of memory blocks */
  struct list_entity blocks;

  /*
   * memory blocks are split into slots of
   * 16, 32, 64, 128, 256, 512, 1024, 2048 and 4096 bytes.
   *
   * the list contains the free slots of this allocator
   */
  struct list_entity free_list[9];
#endif /* CFG_MEMORY_MANAGER */
};

#ifdef CFG_MEMORY_MANAGER
EXPORT void cfg_memory_add(struct cfg_memory *);
EXPORT void cfg_memory_remove(struct cfg_memory *);

EXPORT char *cfg_memory_alloc_string(struct cfg_memory *, size_t size);
EXPORT void cfg_memory_free_string(struct cfg_memory *, char *ptr);

EXPORT void *cfg_memory_alloc(struct cfg_memory *, size_t size);
EXPORT void cfg_memory_free(struct cfg_memory *, void *ptr, size_t);

EXPORT char *cfg_memory_strdup(struct cfg_memory *mem, const char *txt);
#else /* CFG_MEMORY_MANAGER */

/* see doxygen comments in cfg_memory.c */
static INLINE void
cfg_memory_add(struct cfg_memory *m __attribute__((unused))) {}
static INLINE void
cfg_memory_remove(struct cfg_memory *m __attribute__((unused))) {}

static INLINE char *
cfg_memory_alloc_string(struct cfg_memory *m __attribute__((unused)), size_t size) {
  return calloc(1, size);
}
static INLINE void
cfg_memory_free_string(struct cfg_memory *m __attribute__((unused)), char *ptr) {
  free (ptr);
}

static INLINE void *
cfg_memory_alloc(struct cfg_memory *m __attribute__((unused)), size_t size) {
  return calloc(1, size);
}
static INLINE void
cfg_memory_free(struct cfg_memory *m __attribute__((unused)), void *ptr,
    size_t size __attribute__((unused))) {
  free (ptr);
}

static INLINE char *
cfg_memory_strdup(struct cfg_memory *m __attribute__((unused)), const char *txt) {
  if (txt) {
    return strdup(txt);
  }
  return NULL;
}

#endif /* CFG_MEMORY_MANAGER */

#endif /* CFG_MEMORY_H_ */
