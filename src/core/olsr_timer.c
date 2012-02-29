
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
#include <string.h>
#include <unistd.h>

#include "common/avl.h"
#include "common/common_types.h"
#include "olsr_clock.h"
#include "olsr_logging.h"
#include "olsr_memcookie.h"
#include "olsr_timer.h"
#include "olsr.h"

/* BUCKET_COUNT and BUCKET_TIMESLICE must be powers of 2 */
#define BUCKET_DEPTH 3
#define BUCKET_COUNT 512ull
#define BUCKET_TIMESLICE 128ull /* ms */

/* root of all timers */
static struct list_entity _buckets[BUCKET_COUNT][BUCKET_DEPTH];
static int _bucket_ptr[BUCKET_DEPTH];

/* time when the next timer will fire */
static uint64_t _next_fire_event;

/* number of timer events still in queue */
static uint32_t _total_timer_events;

/* Memory cookie for the timer manager */
struct list_entity timerinfo_list;
static struct olsr_memcookie_info _timer_mem_cookie = {
  .name = "timer entry",
  .size = sizeof(struct olsr_timer_entry),
};

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_timer_state);

/* Prototypes */
static uint64_t _calc_jitter(uint64_t rel_time, uint8_t jitter_pct, unsigned int random_val);
static void _insert_into_bucket(struct olsr_timer_entry *);
static void _calculate_next_event(void);

/**
 * Initialize timer scheduler subsystem
 * @return -1 if an error happened, 0 otherwise
 */
void
olsr_timer_init(void)
{
  uint64_t now, offset;
  unsigned i,j;

  if (olsr_subsystem_init(&_timer_state))
    return;

  OLSR_INFO(LOG_TIMER, "Initializing timer scheduler.\n");
  /* mask "last run" to slot size */
  now = olsr_clock_getNow();

  offset = BUCKET_TIMESLICE * BUCKET_COUNT;
  now /= BUCKET_TIMESLICE;

  for (i = 0; i < BUCKET_DEPTH; i++) {
    _bucket_ptr[i] = now & (BUCKET_COUNT - 1ull);

    now /= BUCKET_COUNT;
    offset *= BUCKET_COUNT;

    for (j = 0; j < BUCKET_COUNT; j++) {
      list_init_head(&_buckets[j][i]);
    }
  }

  /* at the moment we have no timer */
  _next_fire_event = ~0ull;
  _total_timer_events = 0;

  /* initialize a cookie for the block based memory manager. */
  olsr_memcookie_add(&_timer_mem_cookie);

  list_init_head(&timerinfo_list);
}

/**
 * Cleanup timer scheduler, this stops and deletes all timers
 */
void
olsr_timer_cleanup(void)
{
  struct olsr_timer_info *ti, *iterator;
  struct list_entity *timer_head_node;
  unsigned i,j;

  if (olsr_subsystem_cleanup(&_timer_state))
    return;

  for (i = 0; i < BUCKET_DEPTH; i++) {
    for (j = 0; j < BUCKET_COUNT; j++) {
      timer_head_node = &_buckets[j][i];

      /* Kill all entries hanging off this hash bucket. */
      while (!list_is_empty(timer_head_node)) {
        struct olsr_timer_entry *timer;

        timer = list_first_element(timer_head_node, timer, _node);
        olsr_timer_stop(timer);
      }
    }
  }

  /* free all timerinfos */
  OLSR_FOR_ALL_TIMERS(ti, iterator) {
    olsr_timer_remove(ti);
  }

  /* release memory cookie for timers */
  olsr_memcookie_remove(&_timer_mem_cookie);
}

/**
 * Add a new group of timers to the scheduler
 * @param ti pointer to uninitialized timer info
 */
void
olsr_timer_add(struct olsr_timer_info *ti) {
  list_add_tail(&timerinfo_list, &ti->node);
}

/**
 * Removes a group of timers from the scheduler
 * All pointers to timers of this timer_info will be invalid after this.
 * @param info pointer to timer info
 */
void
olsr_timer_remove(struct olsr_timer_info *info) {
  struct olsr_timer_entry *timer, *iterator;
  unsigned i,j;

  for (i = 0; i < BUCKET_DEPTH; i++) {
    for (j = 0; j < BUCKET_COUNT; j++) {
      list_for_each_element_safe(&_buckets[j][i], timer, _node, iterator) {
        /* remove all timers of this timer_info */
        if (timer->timer_info == info) {
          olsr_timer_stop(timer);
        }
      }
    }
  }

  list_remove(&info->node);
}

/**
 * Start a new timer.
 * @param rel_time time expressed in milliseconds
 * @param jitter_pct expressed in percent
 * @param context for the callback function
 * @param ti pointer to timer_info object
 * @return a pointer to the created entry
 */
struct olsr_timer_entry *
olsr_timer_start(uint64_t rel_time,
    uint8_t jitter_pct, void *context, struct olsr_timer_info *ti)
{
  struct olsr_timer_entry *timer;

#if !defined(REMOVE_LOG_DEBUG)
  struct timeval_buf timebuf;
#endif

  assert(ti != 0);
  assert(jitter_pct <= 100);
  assert (rel_time > 0 && rel_time < 1000ull * INT32_MAX);

  timer = olsr_memcookie_malloc(&_timer_mem_cookie);

  /*
   * Compute random numbers only once.
   */
  if (!timer->timer_random) {
    timer->timer_random = random();
  }

  /* Fill entry */
  timer->timer_clock = _calc_jitter(rel_time, jitter_pct, timer->timer_random);
  timer->timer_cb_context = context;
  timer->timer_jitter_pct = jitter_pct;
  timer->timer_running = true;

  /* The cookie is used for debugging to traceback the originator */
  timer->timer_info = ti;
  ti->usage++;
  ti->changes++;

  /* Singleshot or periodical timer ? */
  timer->timer_period = ti->periodic ? rel_time : 0;

  /*
   * Now insert in the respective _timer_wheel slot.
   */
  _insert_into_bucket(timer);

  /* and update internal time data */
  _total_timer_events++;
  if (timer->timer_clock < _next_fire_event) {
    _next_fire_event = timer->timer_clock;
  }

  OLSR_DEBUG(LOG_TIMER, "TIMER: start %s timer %p firing in %s, ctx %p\n",
             ti->name, timer, olsr_clock_toClockString(&timebuf, timer->timer_clock), context);

  return timer;
}

/**
 * Delete a timer.
 * @param timer the olsr_timer_entry that shall be removed
 */
void
olsr_timer_stop(struct olsr_timer_entry *timer)
{
  /* It's okay to get a NULL here */
  if (timer == NULL) {
    return;
  }

  OLSR_DEBUG(LOG_TIMER, "TIMER: stop %s timer %p, ctx %p\n",
             timer->timer_info->name, timer, timer->timer_cb_context);


  /*
   * Carve out of the existing wheel_slot and free.
   */
  list_remove(&timer->_node);
  timer->timer_running = false;
  timer->timer_info->usage--;
  timer->timer_info->changes++;

  /* and update internal time data */
  _total_timer_events--;
  if (_next_fire_event == timer->timer_clock) {
    _calculate_next_event();
  }

  if (!timer->timer_in_callback) {
    olsr_memcookie_free(&_timer_mem_cookie, timer);
  }
}

/**
 * Change time when a timer should fire.
 * @param timer olsr_timer_entry to be changed.
 * @param rel_time new relative time expressed in units of milliseconds.
 * @param jitter_pct new jitter expressed in percent.
 */
void
olsr_timer_change(struct olsr_timer_entry *timer, uint64_t rel_time, uint8_t jitter_pct)
{
#if !defined(REMOVE_LOG_DEBUG)
  struct timeval_buf timebuf;
#endif
  bool recalculate;

  /* Sanity check. */
  if (!timer) {
    return;
  }

  assert (rel_time < 1000ull * INT32_MAX);

  /* remember if we have to recalculate the _next_fire_event variable */
  recalculate = _next_fire_event == timer->timer_clock;

  /* Singleshot or periodical timer ? */
  timer->timer_period = timer->timer_info->periodic ? rel_time : 0;

  timer->timer_clock = _calc_jitter(rel_time, jitter_pct, timer->timer_random);
  timer->timer_jitter_pct = jitter_pct;

  /*
   * Changes are easy: Remove timer from the existing _timer_wheel slot
   * and reinsert into the new slot.
   */

  list_remove(&timer->_node);
  _insert_into_bucket(timer);

  if (timer->timer_clock < _next_fire_event) {
    _next_fire_event = timer->timer_clock;
  }
  else if (recalculate) {
    _calculate_next_event();
  }

  OLSR_DEBUG(LOG_TIMER, "TIMER: change %s timer, firing to %s, ctx %p\n",
             timer->timer_info->name,
             olsr_clock_toClockString(&timebuf, timer->timer_clock), timer->timer_cb_context);
}

/**
 * This is the one stop shop for all sort of timer manipulation.
 * Depending on the passed in parameters a new timer is started,
 * or an existing timer is started or an existing timer is
 * terminated.
 * @param timer_ptr pointer to timer_entry pointer
 * @param rel_time time until the new timer should fire, 0 to stop timer
 * @param jitter_pct jitter of timer in percent
 * @param context context pointer of timer
 * @param ti timer_info of timer
 */
void
olsr_timer_set(struct olsr_timer_entry **timer_ptr,
               uint64_t rel_time,
               uint8_t jitter_pct, void *context, struct olsr_timer_info *ti)
{
  assert(ti);          /* we want timer cookies everywhere */
  if (rel_time == 0) {
    /* No good future time provided, kill it. */
    olsr_timer_stop(*timer_ptr);
    *timer_ptr = NULL;
  }
  else if ((*timer_ptr) == NULL) {
    /* No timer running, kick it. */
    *timer_ptr = olsr_timer_start(rel_time, jitter_pct, context, ti);
  }
  else {
    olsr_timer_change(*timer_ptr, rel_time, jitter_pct);
  }
}

/**
 * Walk through the timer list and check if any timer is ready to fire.
 * Call the provided function with the context pointer.
 */
void
olsr_timer_walk(void)
{
  struct olsr_timer_entry *timer, *t_it;
  int i;

  while (_next_fire_event <= olsr_clock_getNow()) {
    i = _bucket_ptr[0];
    list_for_each_element_safe(&_buckets[i][0], timer, _node, t_it) {
      OLSR_DEBUG(LOG_TIMER, "TIMER: fire %s timer %p, ctx %p, "
                  "at clocktick %" PRIu64 "\n",
                  timer->timer_info->name,
                  timer, timer->timer_cb_context, _next_fire_event);

       /* This timer is expired, call into the provided callback function */
       timer->timer_in_callback = true;
       timer->timer_info->callback(timer->timer_cb_context);
       timer->timer_in_callback = false;
       timer->timer_info->changes++;

       /* Only act on actually running timers */
       if (timer->timer_running) {
         /*
          * Don't restart the periodic timer if the callback function has
          * stopped the timer.
          */
         if (timer->timer_period) {
           /* For periodical timers, rehash the random number and restart */
           timer->timer_random = random();
           olsr_timer_change(timer, timer->timer_period, timer->timer_jitter_pct);
         } else {
           /* Singleshot timers are stopped */
           olsr_timer_stop(timer);
         }
       }
       else {
         /* free memory */
         olsr_memcookie_free(&_timer_mem_cookie, timer);
       }
    }

    /* advance our 'next event' marker */
    _calculate_next_event();
  }
}

/**
 * @return timestamp when next timer will fire
 */
uint64_t
olsr_timer_getNextEvent(void) {
  return _next_fire_event;
}

/**
 *
 * @param timestamp
 * @param depth
 * @return true if the
 */
static void
_insert_into_bucket(struct olsr_timer_entry *timer) {
  uint64_t slot;
  int64_t relative;
  int group;

  slot = timer->timer_clock / BUCKET_TIMESLICE;
  relative = olsr_clock_getRelative(timer->timer_clock) / BUCKET_TIMESLICE;
  OLSR_DEBUG(LOG_TIMER, "Insert new timer: %" PRIu64, relative);

  for (group = 0; group < BUCKET_DEPTH; group++, slot /= BUCKET_COUNT, relative /= BUCKET_COUNT) {
    if (relative < (int64_t)BUCKET_COUNT) {
      slot %= BUCKET_COUNT;
      OLSR_DEBUG_NH(LOG_TIMER, "    Insert into bucket: %" PRId64"/%d", slot, group);
      list_add_tail(&_buckets[slot][group], &timer->_node);
      return;
    }
  }

  OLSR_WARN(LOG_TIMER, "Error, timer event too far in the future: %" PRIu64,
      olsr_clock_getRelative(timer->timer_clock));
}


/**
 * Decrement a relative timer by a random number range.
 * @param the relative timer expressed in units of milliseconds.
 * @param the jitter in percent
 * @param random_val cached random variable to calculate jitter
 * @return the absolute time when timer will fire
 */
static uint64_t
_calc_jitter(uint64_t rel_time, uint8_t jitter_pct, unsigned int random_val)
{
  uint64_t jitter_time;

  /*
   * No jitter or, jitter larger than 99% does not make sense.
   * Also protect against overflows resulting from > 25 bit timers.
   */
  if (jitter_pct == 0 || jitter_pct > 99 || rel_time > (1 << 24)) {
    return olsr_clock_get_absolute(rel_time);
  }

  /*
   * Play some tricks to avoid overflows with integer arithmetic.
   */
  jitter_time = ((uint64_t)jitter_pct * rel_time) / 100;
  jitter_time = (uint64_t)random_val / (1ull + (uint64_t)RAND_MAX / (jitter_time + 1ull));

  OLSR_DEBUG(LOG_TIMER, "TIMER: jitter %u%% rel_time %" PRIu64 "ms to %" PRIu64 "ms\n",
      jitter_pct, rel_time, rel_time - jitter_time);

  return olsr_clock_get_absolute(rel_time - jitter_time);
}

static void
_copy_bucket(unsigned depth, unsigned idx) {
  struct olsr_timer_entry *timer, *timer_it;
  uint64_t divide;
  unsigned i;

  assert (depth > 0 && depth < BUCKET_DEPTH && idx < BUCKET_COUNT);

  divide = BUCKET_TIMESLICE;
  for (i = 0; i < depth-1; i++) {
    divide *= BUCKET_COUNT;
  }

  _bucket_ptr[depth] = idx+1;

  list_for_each_element_safe(&_buckets[idx][depth], timer, _node, timer_it) {
    list_remove(&timer->_node);

    i = (timer->timer_clock / divide) & (BUCKET_COUNT - 1ull);
    list_add_tail(&_buckets[i][depth-1], &timer->_node);
  }
}

static int
_look_for_event(unsigned depth) {
  unsigned i,j;
  int idx;

  /* first look in existing data */
  for (i=_bucket_ptr[depth], j=0; j < BUCKET_COUNT; i=(i+1)&255, j++) {
    if (!list_is_empty(&_buckets[i][depth])) {
      return i;
    }
  }

  /* copy bucket from level higher if possible */
  if (depth < BUCKET_DEPTH - 1) {
    idx = _look_for_event(depth+1);
    if (idx != -1) {
      _copy_bucket(depth+1, idx);
    }
  }

  for (i=0; i < BUCKET_COUNT; i++) {
    if (!list_is_empty(&_buckets[i][depth])) {
      return i;
    }
  }

  return -1;
}

static void
_calculate_next_event(void) {
  struct olsr_timer_entry *timer;

  /* no timer event in queue ? */
  if (_total_timer_events == 0) {
    _next_fire_event = ~0ull;
    return;
  }

  _bucket_ptr[0] = _look_for_event(0);
  assert (_bucket_ptr[0] != -1);

  timer = list_first_element(&_buckets[_bucket_ptr[0]][0], timer, _node);
  _next_fire_event = timer->timer_clock & (~((uint64_t)BUCKET_TIMESLICE - 1));
}
