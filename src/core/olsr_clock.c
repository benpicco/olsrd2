
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

#include "os_clock.h"
#include "olsr_logging.h"
#include "olsr_clock.h"
#include "olsr.h"

/* absolute monotonic clock measured in milliseconds compared to start time */
static uint64_t now_times;

/* arbitrary timestamp that represents the time olsr_clock_init() was called */
static uint64_t start_time;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_clock_state);

/**
 * Initialize olsr clock system
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_clock_init(void) {
  if (olsr_subsystem_is_initialized(&_clock_state))
    return 0;

  if (os_clock_gettime64(&start_time)) {
    OLSR_WARN(LOG_TIMER, "OS clock is not working: %s (%d)\n", strerror(errno), errno);
    return -1;
  }

  now_times = 0;

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

  uint64_t now;
  if (os_clock_gettime64(&now)) {
    OLSR_WARN(LOG_TIMER, "OS clock is not working: %s (%d)\n", strerror(errno), errno);
    return -1;
  }

  now_times = now - start_time;
  return 0;
}

/**
 * Calculates the current time in the internal OLSR representation
 * @return current time
 */
uint64_t
olsr_clock_getNow(void) {
  return now_times;
}

/**
 * converts an unsigned integer value into a string representation
 * (divided by 1000)
 * @param buffer pointer to time string buffer
 * @param t current OLSR time
 * @return pointer to time string
 */
char *
olsr_clock_to_string(struct millitxt_buf *buffer, uint64_t t) {
  sprintf(buffer->buf, "%"PRIu64".%03"PRIu64, t/1000, t%1000);
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
 * Format an relative non-wallclock system time string.
 * Displays millisecond resolution.
 *
 * @param buf string buffer for creating output
 * @param clk OLSR timestamp
 * @return buffer to a formatted system time string.
 */
const char *
olsr_clock_toClockString(struct timeval_buf *buf, uint64_t clk)
{
  uint64_t msec = clk % MSEC_PER_SEC;
  uint64_t sec = clk / MSEC_PER_SEC;

  snprintf(buf->buf, sizeof(buf),
      "%"PRIu64":%02"PRIu64":%02"PRIu64".%03"PRIu64"",
      sec / 3600, (sec % 3600) / 60, (sec % 60), msec);

  return buf->buf;
}
