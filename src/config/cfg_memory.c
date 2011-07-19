
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

#include <assert.h>
#include <stdlib.h>

#include "common/list.h"

#include "config/cfg.h"
#include "config/cfg_memory.h"

#ifdef CFG_MEMORY_MANAGER
/* list of possible block sizes */
static const size_t _alloc_size[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };

/* largest index of the array above */
static const int _largest_block_idx = (int)ARRAYSIZE(_alloc_size) - 1;

/* amount of memory allocated for each chunk */
static const size_t _block_size = 4096;

static int _get_sizeidx(size_t size);
static void _add_block(struct cfg_memory *mem);
static void *_alloc(struct cfg_memory *mem, int size_idx);
static void _free(struct cfg_memory *mem, void *ptr, int size_idx);

/**
 * Initialize a new memory allocator and add a first block of
 * memory.
 * @param mem pointer to uninitialized cfg_memory struct
 */
void
cfg_memory_add(struct cfg_memory *mem) {
  int i;

  /* initialize list */
  list_init_head(&mem->blocks);

  for (i=0; i<=_largest_block_idx; i++) {
    list_init_head(&mem->free_list[i]);
  }
}

/**
 * Remove a memory allocator including all attached
 * memory blocks
 * @param mem pointer to memory allocator
 */
void
cfg_memory_remove(struct cfg_memory *mem) {
  struct cfg_memory_block *block, *iterator;

  list_for_each_element_reverse_safe(&mem->blocks, block, node, iterator) {
    list_remove(&block->node);
    free(block->ptr);
  }
}

/**
 * Allocates a slot of memory for a string and store
 * the size of the slot in the first byte, give the rest
 * to the user.
 * The returned pointer will not be aligned for anothing
 * except characters.
 * @param mem pointer to memory allocator
 * @param size number of bytes necessary for the string
 *   including 0-byte.
 * @return pointer to allocated string buffer
 */
char *
cfg_memory_alloc_string(struct cfg_memory *mem, size_t size) {
  char *ptr;

  size++;
  ptr = cfg_memory_alloc(mem, size);

  ptr[0] = (char)_get_sizeidx(size);
  return ptr + 1;
}

/**
 * Free an allocated string and put it into the
 * corresponding free slot list.
 * @param mem pointer to memory allocator
 * @param ptr pointer to string
 */
void
cfg_memory_free_string(struct cfg_memory *mem, char *ptr) {
  if (ptr) {
    ptr--;

    _free(mem, ptr, ptr[0]);
  }
}

/**
 * Allocate a slot of memory. User have to remember the
 * size of the allocated slot.
 * The returned pointer will be aligned similar to a
 * normal malloc() block.
 * @param mem pointer to memory allocator
 * @param size number of bytes necessary for the user
 * @param pointer to allocated memory
 */
void *
cfg_memory_alloc(struct cfg_memory *mem, size_t size) {
  int idx;

  idx = _get_sizeidx(size);

  if (idx == -1) {
    return malloc(size);
  }

  return _alloc(mem, idx);
}

/**
 * Fre a slot of allocated memory.
 * @param mem pointer to memory allocator
 * @param ptr pointer to allocated memory
 * @param size size of allocated memory
 */
void
cfg_memory_free(struct cfg_memory *mem, void *ptr, size_t size) {
  int idx;

  if (ptr) {
    idx = _get_sizeidx(size);
    if (idx == -1) {
      free(ptr);
      return;
    }

    _free(mem, ptr, idx);
  }
}

/**
 * Duplicate a string into an allocated string memory block
 * @param mem pointer to memory allocator
 * @param txt pointer to string
 * @return pointer to copied string
 */
char *
cfg_memory_strdup(struct cfg_memory *mem, const char *txt) {
  char *ptr;
  size_t len;

  if (txt == NULL) {
    return NULL;
  }

  len = strlen(txt) + 1;
  ptr = cfg_memory_alloc_string(mem, len);
  memcpy(ptr, txt, len);
  return ptr;
}

/**
 * Calculate the index of the slotsize necessary for
 * a certain memory size.
 * @param size necessary memory size
 * @return slotsize index, -1 if too large for slots
 */
static int
_get_sizeidx(size_t size) {
  int i;

  for (i=0; i <= _largest_block_idx; i++) {
    if (size < _alloc_size[i]) {
      return i;
    }
  }
  return -1;
}

/**
 * Add a new block of memory to the allocator
 * @param mem pointer to memory allocator
 */
static void
_add_block(struct cfg_memory *mem) {
  struct list_entity *ptr;
  struct cfg_memory_block *block_data;

  /* first get new memory block and put it into the free list */
  ptr = calloc(1, _block_size);
  list_add_tail(&mem->free_list[_largest_block_idx], ptr);

  /* now get a cfg_memory_block */
  block_data = cfg_memory_alloc(mem, sizeof(*block_data));
  block_data->ptr = ptr;
  list_add_tail(&mem->blocks, &block_data->node);
}

/**
 * Allocate a slot of memory
 * @param mem pointer to memory allocator
 * @param size_idx index of slotsize
 * @return pointer to allocated slot
 */
static void *
_alloc(struct cfg_memory *mem, int size_idx) {
  char *ptr;
  struct list_entity *node;
  struct list_entity *freelist;

  assert (size_idx <= _largest_block_idx);

  freelist = &mem->free_list[size_idx];

  if (list_is_empty(freelist)) {
    if (size_idx == _largest_block_idx) {
      /* not enough memory left, get a new block */
      _add_block(mem);
      return _alloc(mem, size_idx);
    }

    /* split larger block, add first to freelist */
    node = _alloc(mem, size_idx + 1);
    list_add_tail(freelist, node);

    ptr = (char *)node + _alloc_size[size_idx];
  }
  else {
    /* get first element */
    node = freelist->next;
    list_remove(node);

    ptr = (char *)node;
  }

  memset (ptr, 0, _alloc_size[size_idx]);
  return ptr;
}

/**
 * Free a slot of memory
 * @param mem pointer to memory allocator
 * @param ptr pointer to allocated slot
 * @param size_idx size index of allocated slot
 */
static void
_free(struct cfg_memory *mem, void *ptr, int size_idx) {
  struct list_entity *node = ptr;

  /* add to free list */
  list_add_tail(&mem->free_list[size_idx], node);
}

#endif /* CFG_MEMORY_MANAGER */
