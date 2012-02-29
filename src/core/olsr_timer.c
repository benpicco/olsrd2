
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

/* Number of hierarchies of buckets */
#define BUCKET_DEPTH          3

/* power of 2 for the number of elements in each bucket */
#define BUCKET_COUNT_POW2     9ull

#define BUCKET_COUNT          (1ull << BUCKET_COUNT_POW2)

/* power of 2 for the number of milliseconds each bucket represents */
#define BUCKET_TIMESLICE_POW2 7ull

#define BUCKET_TIMESLICE      (1ull << BUCKET_TIMESLICE_POW2)

/* maximum number of milliseconds a timer can use */
#define TIMER_MAX_RELTIME     (1ull << (BUCKET_COUNT_POW2 * BUCKET_DEPTH + BUCKET_COUNT_POW2))

/* root of all timers */
static struct list_entity _buckets[BUCKET_COUNT][BUCKET_DEPTH];
static int _bucket_ptr[BUCKET_DEPTH];

/* time when the next timer will fire */
static uint64_t _next_fire_event;

/* number of timer events still in queue */
static uint32_t _total_timer_events;

/* Memory cookie for the timer manager */
struct list_entity timerinfo_list;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_timer_state);

/* Prototypes */
static void _calc_clock(struct olsr_timer_entry *timer, uint64_t rel_time);
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
  now >>= BUCKET_TIMESLICE_POW2;

  for (i = 0; i < BUCKET_DEPTH; i++) {
    _bucket_ptr[i] = now & (BUCKET_COUNT - 1ull);

    now >>= BUCKET_COUNT_POW2;
    offset <<= BUCKET_COUNT_POW2;

    for (j = 0; j < BUCKET_COUNT; j++) {
      list_init_head(&_buckets[j][i]);
    }
  }

  /* at the moment we have no timer */
  _next_fire_event = ~0ull;
  _total_timer_events = 0;

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
 * All pointers to timers of this info will be invalid after this.
 * @param info pointer to timer info
 */
void
olsr_timer_remove(struct olsr_timer_info *info) {
  struct olsr_timer_entry *timer, *iterator;
  unsigned i,j;

  for (i = 0; i < BUCKET_DEPTH; i++) {
    for (j = 0; j < BUCKET_COUNT; j++) {
      list_for_each_element_safe(&_buckets[j][i], timer, _node, iterator) {
        /* remove all timers of this info */
        if (timer->info == info) {
          olsr_timer_stop(timer);
        }
      }
    }
  }

  list_remove(&info->node);
}

/**
 * Start or restart a new timer.
 * @param timer initialized timer entry
 */
void
olsr_timer_start(struct olsr_timer_entry *timer, uint64_t rel_time)
{
#if !defined(REMOVE_LOG_DEBUG)
  struct timeval_buf timebuf;
#endif
  uint64_t stored_time;

  assert(timer->info);
  assert(timer->jitter_pct <= 100);
  assert(rel_time > 0 && rel_time < TIMER_MAX_RELTIME);

  stored_time = timer->_clock;
  if (stored_time) {
    list_remove(&timer->_node);
    _total_timer_events--;
  }
  else {
    /* The cookie is used for debugging to traceback the originator */
    timer->info->usage++;
    timer->info->changes++;
  }

  /*
   * Compute random numbers only once.
   */
  if (!timer->_random) {
    timer->_random = random();
  }

  /* Fill entry */
  _calc_clock(timer, rel_time);

  /* Singleshot or periodical timer ? */
  timer->period = timer->info->periodic ? rel_time : 0;

  /*
   * Now insert in the respective _timer_wheel slot.
   */
  _insert_into_bucket(timer);

  /* and update internal time data */
  _total_timer_events++;
  if (timer->_clock < _next_fire_event) {
    _next_fire_event = timer->_clock;
  }
  else if (stored_time == _next_fire_event) {
    _calculate_next_event();
  }

  OLSR_DEBUG(LOG_TIMER, "TIMER: start %s timer %p firing in %s\n",
             timer->info->name, timer,
             olsr_clock_toClockString(&timebuf, timer->_clock));
}

/**
 * Delete a timer.
 * @param timer the olsr_timer_entry that shall be removed
 */
void
olsr_timer_stop(struct olsr_timer_entry *timer)
{
  if (timer->_clock == 0) {
    return;
  }

  OLSR_DEBUG(LOG_TIMER, "TIMER: stop %s\n", timer->info->name);

  /* remove timer from buckets */
  list_remove(&timer->_node);
  timer->_clock = 0;
  timer->_random = 0;
  timer->info->usage--;
  timer->info->changes++;

  /* and update internal time data */
  _total_timer_events--;
  if (_next_fire_event == timer->_clock) {
    _calculate_next_event();
  }
}

/**
 * This is the one stop shop for all sort of timer manipulation.
 * Depending on the passed in parameters a new timer is started,
 * or an existing timer is started or an existing timer is
 * terminated.
 * @param timer_ptr pointer to timer_entry pointer
 * @param rel_time time until the new timer should fire, 0 to stop timer
 */
void
olsr_timer_set(struct olsr_timer_entry *timer, uint64_t rel_time)
{
  if (rel_time == 0) {
    /* No good future time provided, kill it. */
    olsr_timer_stop(timer);
  }
  else {
    /* No timer running, kick it. */
    olsr_timer_start(timer, rel_time);
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
                  timer->info->name,
                  timer, timer->cb_context, _next_fire_event);

       /* This timer is expired, call into the provided callback function */
       timer->info->callback(timer->cb_context);
       timer->info->changes++;

       /*
        * Only act on actually running timers, the callback might have
        * called olsr_timer_stop() !
        */
       if (!timer->_clock) {
         /* Timer has been stopped by callback */
         continue;
       }
       if (timer->period) {
         /* For periodical timers, rehash the random number and restart */
         timer->_random = random();
         olsr_timer_start(timer, timer->period);
       } else {
         /* Singleshot timers are stopped */
         olsr_timer_stop(timer);
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

  slot = timer->_clock >> BUCKET_TIMESLICE_POW2;
  relative = olsr_clock_getRelative(timer->_clock) >> BUCKET_TIMESLICE_POW2;
  OLSR_DEBUG(LOG_TIMER, "Insert new timer: %" PRIu64, relative);

  for (group = 0; group < BUCKET_DEPTH;
      group++, slot >>= BUCKET_COUNT_POW2, relative >>= BUCKET_COUNT_POW2) {
    if (relative < (int64_t)BUCKET_COUNT) {
      slot &= (BUCKET_COUNT - 1);
      OLSR_DEBUG_NH(LOG_TIMER, "    Insert into bucket: %" PRId64"/%d", slot, group);
      list_add_tail(&_buckets[slot][group], &timer->_node);
      return;
    }
  }

  OLSR_WARN(LOG_TIMER, "Error, timer event too far in the future: %" PRIu64,
      olsr_clock_getRelative(timer->_clock));
}


/**
 * Decrement a relative timer by a random number range.
 * @param the relative timer expressed in units of milliseconds.
 * @param the jitter in percent
 * @param random_val cached random variable to calculate jitter
 * @return the absolute time when timer will fire
 */
static void
_calc_clock(struct olsr_timer_entry *timer, uint64_t rel_time)
{
  uint64_t jitter_time;

  /*
   * No jitter or, jitter larger than 99% does not make sense.
   * Also protect against overflows resulting from > 25 bit timers.
   */
  if (timer->jitter_pct == 0 || timer->jitter_pct > 99) {
    timer->_clock = olsr_clock_get_absolute(rel_time);
    return;
  }

  /*
   * Play some tricks to avoid overflows with integer arithmetic.
   * TODO: check if need to be changed because of 64 bit arithmetics
   */
  jitter_time = ((uint64_t)timer->jitter_pct * rel_time) / 100;
  jitter_time = (uint64_t)timer->_random / (1ull + (uint64_t)RAND_MAX / (jitter_time + 1ull));

  OLSR_DEBUG(LOG_TIMER, "TIMER: jitter %u%% rel_time %" PRIu64 "ms to %" PRIu64 "ms\n",
      timer->jitter_pct, rel_time, rel_time - jitter_time);

  timer->_clock = olsr_clock_get_absolute(rel_time - jitter_time);
}

static void
_copy_bucket(unsigned depth, unsigned idx) {
  struct olsr_timer_entry *timer, *timer_it;
  uint64_t shift;
  unsigned i;

  assert (depth > 0 && depth < BUCKET_DEPTH && idx < BUCKET_COUNT);

  shift = BUCKET_TIMESLICE_POW2;
  for (i = 0; i < depth-1; i++) {
    shift += BUCKET_COUNT_POW2;
  }

  _bucket_ptr[depth] = idx+1;

  list_for_each_element_safe(&_buckets[idx][depth], timer, _node, timer_it) {
    list_remove(&timer->_node);

    i = (timer->_clock >> shift) & (BUCKET_COUNT - 1ull);
    list_add_tail(&_buckets[i][depth-1], &timer->_node);
  }
}

static int
_look_for_event(unsigned depth) {
  unsigned i;
  int idx;

  /* first look in existing data before we need to load another layer */
  for (i=_bucket_ptr[depth]; i < BUCKET_COUNT; i++) {
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

  /* now look again for a full bucket */
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

  /* get the timestamp when the first bucket will happen */
  timer = list_first_element(&_buckets[_bucket_ptr[0]][0], timer, _node);
  _next_fire_event = timer->_clock & (~((uint64_t)BUCKET_TIMESLICE - 1));
}
