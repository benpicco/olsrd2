/*
 * olsr.c
 *
 *  Created on: Oct 17, 2011
 *      Author: rogge
 */

#include "olsr.h"

static bool _running = true;

/**
 * Call this function to end OLSR because of an error
 */
void
olsr_exit(void) {
  _running = false;
}

/**
 * @return true if OLSR is still running, false if mainloop should
 *   end because of an error.
 */
bool
olsr_is_running(void) {
  return _running;
}
