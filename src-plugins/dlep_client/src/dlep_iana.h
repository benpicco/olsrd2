/*
 * dlep_iana.h
 *
 *  Created on: May 3, 2012
 *      Author: rogge
 */

#ifndef DLEP_IANA_H_
#define DLEP_IANA_H_

enum { DLEP_MESSAGE_ID = 42 };

enum dlep_orders {
  DLEP_ORDER_INTERFACE_DISCOVERY = 1,
  DLEP_ORDER_CONNECT_ROUTER      = 2,
  DLEP_ORDER_NEIGHBOR_UPDATE     = 3,
};

/* DLEP TLV types */
enum dlep_msgtlv_types {
  DLEP_TLV_ORDER           = 192,
  DLEP_TLV_PEER_TYPE       = 193,

  DLEP_TLV_SSID            = 194,
  DLEP_TLV_LAST_SEEN       = 195,
  DLEP_TLV_FREQUENCY       = 196,
  DLEP_TLV_SUPPORTED_RATES = 197,
};

enum dlep_addrtlv_types {
  DLEP_ADDRTLV_SIGNAL     = 192,
  DLEP_ADDRTLV_LAST_SEEN  = 193,
  DLEP_ADDRTLV_RX_BITRATE = 194,
  DLEP_ADDRTLV_RX_BYTES   = 195,
  DLEP_ADDRTLV_RX_PACKETS = 196,
  DLEP_ADDRTLV_TX_BITRATE = 197,
  DLEP_ADDRTLV_TX_BYTES   = 198,
  DLEP_ADDRTLV_TX_PACKETS = 199,
  DLEP_ADDRTLV_TX_RETRIES = 200,
  DLEP_ADDRTLV_TX_FAILED  = 201,
};

#endif /* DLEP_IANA_H_ */
