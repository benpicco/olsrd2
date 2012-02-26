
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

#ifndef _OLSR_CLOCK
#define _OLSR_CLOCK

#include "common/common_types.h"

/* Some defs for juggling with timers */
#define MSEC_PER_SEC 1000
#define USEC_PER_SEC 1000000
#define NSEC_PER_USEC 1000
#define USEC_PER_MSEC 1000

struct millitxt_buf {
  char buf[sizeof("4000000.000")];
};

/* buffer for displaying absolute timestamps */
struct timeval_buf {
  char buf[sizeof("00:00:00.000000")];
};

EXPORT int olsr_clock_init(void) __attribute__((warn_unused_result));
EXPORT void olsr_clock_cleanup(void);
EXPORT int olsr_clock_update(void) __attribute__((warn_unused_result));

EXPORT uint64_t olsr_clock_getNow(void);

EXPORT uint32_t olsr_clock_decode_olsrv1(uint8_t);
EXPORT uint8_t olsr_clock_encode_olsrv1(uint64_t);

EXPORT char *olsr_clock_to_string(struct millitxt_buf *buffer, uint64_t t);
EXPORT uint32_t olsr_clock_parse_string(char *txt);

EXPORT const char *olsr_clock_toClockString(struct timeval_buf *, uint64_t);
EXPORT const char *olsr_clock_getWallclockString(struct timeval_buf *);

/**
 * Returns a timestamp s seconds in the future
 * @param s milliseconds until timestamp
 * @return absolute time when event will happen
 */
static INLINE uint64_t
olsr_clock_get_absolute(uint64_t relative)
{
  return olsr_clock_getNow() + relative;
}

/**
 * Returns the number of milliseconds until the timestamp will happen
 * @param absolute timestamp
 * @return milliseconds until event will happen, negative if it already
 *   happened.
 */
static INLINE int64_t
olsr_clock_getRelative(uint64_t absolute)
{
  return (int64_t)absolute - (int64_t)olsr_clock_getNow();
}

/**
 * Checks if a timestamp has already happened
 * @param absolute timestamp
 * @return true if the event already happened, false otherwise
 */
static INLINE bool
olsr_clock_isPast(uint64_t absolute)
{
  return absolute < olsr_clock_getNow();
}

#endif

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
