
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2012, the olsr.org team - see HISTORY file
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
#include "olsr_memcookie.h"
#include "olsr_logging.h"
#include "olsr.h"

struct list_entity olsr_cookies;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_memcookie_state);

/**
 * Initialize the memory cookie system
 */
void
olsr_memcookie_init(void) {
  if (olsr_subsystem_init(&_memcookie_state))
    return;

  list_init_head(&olsr_cookies);
}

/**
 * Cleanup the memory cookie system
 */
void
olsr_memcookie_cleanup(void)
{
  struct olsr_memcookie_info *info, *iterator;

  if (olsr_subsystem_cleanup(&_memcookie_state))
    return;

  /*
   * Walk the full index range and kill 'em all.
   */
  OLSR_FOR_ALL_COOKIES(info, iterator) {
    olsr_memcookie_remove(info);
  }
}

/**
 * Allocate a new memcookie.
 * @param ci initialized memcookie
 */
void
olsr_memcookie_add(struct olsr_memcookie_info *ci)
{
  assert (ci->name);
  assert (ci->size >= sizeof(struct list_entity));

  /* Init the free list */
  list_init_head(&ci->_free_list);

  list_add_tail(&olsr_cookies, &ci->_node);
}

/**
 * Delete a memcookie and all memory in the free list
 * @param ci pointer to memcookie
 */
void
olsr_memcookie_remove(struct olsr_memcookie_info *ci)
{
  struct list_entity *item;

  /* remove memcookie from tree */
  list_remove(&ci->_node);

  /* remove all free memory blocks */
  while (!list_is_empty(&ci->_free_list)) {
    item = ci->_free_list.next;

    list_remove(item);
    free(item);
  }
}

/**
 * Allocate a fixed amount of memory based on a passed in cookie type.
 * @param ci pointer to memcookie info
 * @return allocated memory
 */
void *
olsr_memcookie_malloc(struct olsr_memcookie_info *ci)
{
  struct list_entity *entity;
  void *ptr;

#if !defined REMOVE_LOG_DEBUG
  bool reuse = false;
#endif

  /*
   * Check first if we have reusable memory.
   */
  if (list_is_empty(&ci->_free_list)) {
    /*
     * No reusable memory block on the free_list.
     * Allocate a fresh one.
     */
    ptr = calloc(1, ci->size);
    if (ptr == NULL) {
      return NULL;
    }
    ci->_allocated++;
  } else {
    /*
     * There is a memory block on the free list.
     * Carve it out of the list, and clean.
     */
    entity = ci->_free_list.next;
    list_remove(entity);

    memset(entity, 0, ci->size);
    ptr = entity;

    ci->_free_list_size--;
    ci->_recycled++;
#if !defined REMOVE_LOG_DEBUG
    reuse = true;
#endif
  }

  /* Stats keeping */
  ci->_current_usage++;

  OLSR_DEBUG(LOG_MEMCOOKIE, "MEMORY: alloc %s, %" PRINTF_SIZE_T_SPECIFIER " bytes%s\n",
             ci->name, ci->size, reuse ? ", reuse" : "");
  return ptr;
}

/**
 * Free a memory block owned by a given cookie.
 * @param ci pointer to memcookie info
 * @param ptr pointer to memory block
 */
void
olsr_memcookie_free(struct olsr_memcookie_info *ci, void *ptr)
{
  struct list_entity *item;
#if !defined REMOVE_LOG_DEBUG
  bool reuse = false;
#endif

  /*
   * Rather than freeing the memory right away, try to reuse at a later
   * point. Keep at least ten percent of the active used blocks or at least
   * ten blocks on the free list.
   */
  if (ci->_free_list_size < ci->min_free_count
      || (ci->_free_list_size < ci->_current_usage / COOKIE_FREE_LIST_THRESHOLD)) {
    item = ptr;

    list_add_tail(&ci->_free_list, item);

    ci->_free_list_size++;
#if !defined REMOVE_LOG_DEBUG
    reuse = true;
#endif
  } else {

    /* No interest in reusing memory. */
    free(ptr);
  }

  /* Stats keeping */
  ci->_current_usage--;

  OLSR_DEBUG(LOG_MEMCOOKIE, "MEMORY: free %s, %"PRINTF_SIZE_T_SPECIFIER" bytes%s\n",
             ci->name, ci->size, reuse ? ", reuse" : "");
}
