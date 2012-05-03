/*
 * dlep_outgoing.h
 *
 *  Created on: May 3, 2012
 *      Author: rogge
 */

#ifndef DLEP_OUTGOING_H_
#define DLEP_OUTGOING_H_

int dlep_outgoing_init(void);
void dlep_outgoing_cleanup(void);

void dlep_trigger_metric_update(void);
void dlep_reconfigure_timers(void);

#endif /* DLEP_OUTGOING_H_ */
