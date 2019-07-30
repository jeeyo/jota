#ifndef _JOTA_NODE_H_
#define _JOTA_NODE_H_

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

#include "jota-torrent.h"

#include "node-id.h"
#include "crc16.h"
#include "lib/random.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

PROCESS_NAME(jota_udp_server_process);
PROCESS_NAME(jota_node_process);

void qsort(void *a, size_t n, size_t es, int (*cmp)());

#define JOTA_NBR_OF_PEERS 8
#define JOTA_CONN_PORT 7300

#define JT_SERIALIZE_RESULT_LEN 128

#define JT_ACK_MSG "ack"
#define JT_HANDSHAKE_MSG "handshake"
#define JT_BITFIELD_MSG "bitfield"
#define JT_CHOKE_MSG "choke"
#define JT_INTEREST_MSG "interest"
#define JT_REQUEST_MSG "request"
#define JT_PIECE_MSG "piece"

#define JT_ACK_HANDSHAKE_MSG "ackhandshake"
#define JT_ACK_PIECE "ackpiece"

#define JOTA_TX_TIMEOUT (10 * CLOCK_SECOND)
#define JOTA_CHOKED_TIMEOUT (30 * CLOCK_SECOND)
#define JOTA_HANDSHAKED_TIMEOUT (60 * CLOCK_SECOND)

#define UNUSED(x)     ((void)(x))

#if !NETSTACK_CONF_WITH_IPV6 || !UIP_CONF_ROUTER || !UIP_CONF_IPV6_RPL
#error "This example can not work with the current contiki configuration"
#error "Check the values of: UIP_CONF_ROUTER, UIP_CONF_IPV6_RPL"
#endif

PROCESS_NAME(jota_node_process);

#endif /* _JOTA_NODE_H_ */