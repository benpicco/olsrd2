
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
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

#include "os_system.h"
#include "olsr_logging.h"
#include "olsr_clock.h"
#include "olsr.h"

/* Timer data */
static uint32_t now_times;             /* relative time compared to startup (in milliseconds */
static struct timeval first_tv;        /* timevalue during startup */
static struct timeval last_tv;         /* timevalue used for last olsr_times() calculation */

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_clock_state);

static int olsr_get_timezone(void);

/**
 * Initialize olsr clock system
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_clock_init(void) {
  if (olsr_subsystem_is_initialized(&_clock_state))
    return 0;

  /* Grab initial timestamp */
  if (os_system_gettimeofday(&first_tv)) {
    OLSR_WARN(LOG_TIMER, "OS clock is not working: %s (%d)\n", strerror(errno), errno);
    return -1;
  }
  last_tv = first_tv;

  if (olsr_clock_update()) {
    return -1;
  }

  olsr_subsystem_init(&_clock_state);
  return 0;
}

/**
 * Cleanup reference counter
 */
void
olsr_clock_cleanup(void) {
  olsr_subsystem_cleanup(&_clock_state);
}

/**
 * Update the internal clock to current system time
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_clock_update(void)
{
  struct timeval tv;
  uint32_t t;

  if (os_system_gettimeofday(&tv) != 0) {
    OLSR_WARN(LOG_TIMER, "OS clock is not working: %s (%d)\n", strerror(errno), errno);
    return -1;
  }

  /* test if time jumped backward or more than 60 seconds forward */
  if (tv.tv_sec < last_tv.tv_sec || (tv.tv_sec == last_tv.tv_sec && tv.tv_usec < last_tv.tv_usec)
      || tv.tv_sec - last_tv.tv_sec > 60) {
    OLSR_WARN(LOG_TIMER, "Time jump (%d.%06d to %d.%06d)\n",
              (int32_t) (last_tv.tv_sec), (int32_t) (last_tv.tv_usec), (int32_t) (tv.tv_sec), (int32_t) (tv.tv_usec));

    t = (last_tv.tv_sec - first_tv.tv_sec) * 1000 + (last_tv.tv_usec - first_tv.tv_usec) / 1000;
    t++;                        /* advance time by one millisecond */

    first_tv = tv;
    first_tv.tv_sec -= (t / 1000);
    first_tv.tv_usec -= ((t % 1000) * 1000);

    if (first_tv.tv_usec < 0) {
      first_tv.tv_sec--;
      first_tv.tv_usec += 1000000;
    }
    last_tv = tv;
    now_times =  t;
  }
  last_tv = tv;
  now_times = (tv.tv_sec - first_tv.tv_sec) * 1000 + (tv.tv_usec - first_tv.tv_usec) / 1000;
  return 0;
}

/**
 * Calculates the current time in the internal OLSR representation
 * @return current time
 */
uint32_t
olsr_clock_getNow(void) {
  return now_times;
}

/**
 * Returns the number of milliseconds until the timestamp will happen
 * @param absolute timestamp
 * @return milliseconds until event will happen, negative if it already
 *   happened.
 */
int32_t
olsr_clock_getRelative(uint32_t absolute)
{
  uint32_t diff;
  if (absolute > now_times) {
    diff = absolute - now_times;

    /* overflow ? */
    if (diff > (1u << 31)) {
      return -(int32_t) (0xffffffff - diff);
    }
    return (int32_t) (diff);
  }

  diff = now_times - absolute;
  /* overflow ? */
  if (diff > (1u << 31)) {
    return (int32_t) (0xffffffff - diff);
  }
  return -(int32_t) (diff);
}

/**
 * Checks if a timestamp has already happened
 * @param absolute timestamp
 * @return true if the event already happened, false otherwise
 */
bool
olsr_clock_isPast(uint32_t absolute)
{
  if (absolute > now_times) {
    return absolute - now_times > (1u << 31);
  }

  return now_times - absolute <= (1u << 31);
}

/**
 * converts an unsigned integer value into a string representation
 * (divided by 1000)
 * @param buffer pointer to time string buffer
 * @param t current OLSR time
 * @return pointer to time string
 */
char *
olsr_clock_to_string(struct millitxt_buf *buffer, uint32_t t) {
  sprintf(buffer->buf, "%u.%03u", t/1000, t%1000);
  return buffer->buf;
}

/**
 * converts a floating point text into a unsigned integer representation
 * (multiplied by 1000)
 * @param txt pointer to text representation
 * @return integer representation
 */
uint32_t olsr_clock_parse_string(char *txt) {
  uint32_t t = 0;
  int fractionDigits = 0;
  bool frac = false;

  while (fractionDigits < 3 && *txt) {
    if (*txt == '.' && !frac) {
      frac = true;
      txt++;
    }

    if (!isdigit(*txt)) {
      break;
    }

    t = t * 10 + (*txt++ - '0');
    if (frac) {
      fractionDigits++;
    }
  }

  while (fractionDigits++ < 3) {
    t *= 10;
  }
  return t;
}


/**
 * Format an absolute wallclock system time string.
 * May be called upto 4 times in a single printf() statement.
 * Displays microsecond resolution.
 * @param buf pointer to timeval buffer
 * @return buffer to a formatted system time string.
 */
const char *
olsr_clock_getWallclockString(struct timeval_buf *buf)
{
  struct timeval now;
  int sec = 0, usec = 0;

  if (os_system_gettimeofday(&now) == 0) {
    sec = (int)now.tv_sec + olsr_get_timezone();
    usec = (int)now.tv_usec;
  }
  snprintf(buf->buf, sizeof(buf), "%02d:%02d:%02d.%06d",
      (sec % 86400) / 3600, (sec % 3600) / 60, sec % 60, usec);

  return buf->buf;
}

/**
 * Format an relative non-wallclock system time string.
 * Displays millisecond resolution.
 *
 * @param buf string buffer for creating output
 * @param clk OLSR timestamp
 * @return buffer to a formatted system time string.
 */
const char *
olsr_clock_toClockString(struct timeval_buf *buf, uint32_t clk)
{
  unsigned int msec = clk % MSEC_PER_SEC;
  unsigned int sec = clk / MSEC_PER_SEC;

  snprintf(buf->buf, sizeof(buf),
      "%02u:%02u:%02u.%03u", sec / 3600, (sec % 3600) / 60, (sec % 60), msec);

  return buf->buf;
}

/**
 * Use gmtime() and localtime() to keep things simple.
 * taken and modified from www.tcpdump.org.
 *
 * @return difference between gmt and local time in seconds.
 */
static int
olsr_get_timezone(void)
{
#define OLSR_TIMEZONE_UNINITIALIZED -1
  static int time_diff = OLSR_TIMEZONE_UNINITIALIZED;
  if (time_diff == OLSR_TIMEZONE_UNINITIALIZED) {
    int dir;
    struct timeval tv;
    time_t t;
    struct tm gmt;
    struct tm *loc;

    if (os_system_gettimeofday(&tv)) {
      OLSR_WARN(LOG_TIMER, "Cannot read internal clock: %s (%d)",
          strerror(errno), errno);
      return 0;
    }

    t = tv.tv_sec;
    gmt = *gmtime(&t);
    loc = localtime(&t);
    time_diff = (loc->tm_hour - gmt.tm_hour) * 60 * 60 + (loc->tm_min - gmt.tm_min) * 60;

    /*
     * If the year or julian day is different, we span 00:00 GMT
     * and must add or subtract a day. Check the year first to
     * avoid problems when the julian day wraps.
     */
    dir = loc->tm_year - gmt.tm_year;
    if (!dir) {
      dir = loc->tm_yday - gmt.tm_yday;
    }

    time_diff += dir * 24 * 60 * 60;
  }
  return time_diff;
}
