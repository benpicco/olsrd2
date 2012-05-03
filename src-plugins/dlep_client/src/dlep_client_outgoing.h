/*
 * dlep_client_outgoing.h
 *
 *  Created on: May 3, 2012
 *      Author: rogge
 */

#ifndef DLEP_CLIENT_OUTGOING_H_
#define DLEP_CLIENT_OUTGOING_H_

#include "packetbb/pbb_writer.h"

int dlep_client_outgoing_init(void);
void dlep_client_outgoing_cleanup(void);

void dlep_client_registerif(struct pbb_writer_interface *);
void dlep_client_unregisterif(struct pbb_writer_interface *);

void dlep_client_reconfigure_timers(void);

#endif /* DLEP_CLIENT_OUTGOING_H_ */
