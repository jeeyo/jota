#ifndef _JOTA_NODE_H_
#define _JOTA_NODE_H_

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

#include "jota-torrent.h"

#include "node-id.h"
#include "crc16.h"
#include "lib/random.h"
#include "sys/energest.h"

#include "cmp.h"

#include <stdio.h>
#include <stdlib.h>

PROCESS_NAME(jota_udp_server_process);
PROCESS_NAME(jota_node_process);

// How many nodes we will download from simultaneously?
#define JOTA_MAX_UPLOADERS 3
// How many nodes we will upload to simultaneously?
#define JOTA_MAX_DOWNLOADERS 2
#define JOTA_NBR_OF_PEERS 5
// #define JOTA_NBR_OF_PEERS (JOTA_MAX_UPLOADERS + JOTA_MAX_DOWNLOADERS)

#define JOTA_CONN_PORT 7300

// #define JT_ACK_MSG "ack"
// #define JT_HANDSHAKE_MSG "handshake"
// #define JT_CHOKE_MSG "choke"
// #define JT_INTEREST_MSG "interest"
// #define JT_REQUEST_MSG "request"
// #define JT_BLOCK_MSG "piece"
// #define JT_ACK_HANDSHAKE_MSG "ackhandshake"

#define JT_ACK_MSG 0
#define JT_HANDSHAKE_MSG 1
#define JT_CHOKE_MSG 2
#define JT_INTEREST_MSG 3
#define JT_REQUEST_MSG 4
#define JT_BLOCK_MSG 5
#define JT_ACK_HANDSHAKE_MSG 6

#define JOTA_TX_TIMEOUT (3 * CLOCK_SECOND)
#define JOTA_CHOKED_TIMEOUT (5 * CLOCK_SECOND)
#define JOTA_HANDSHAKED_TIMEOUT (3 * CLOCK_SECOND)
#define JOTA_OPTIMISTIC_UNCHOKE_TIMEOUT (10 * CLOCK_SECOND)

#define JOTA_DOWNTIME_TIMEOUT (15 * CLOCK_SECOND)
#define JOTA_DOWNTIME_DURATION (15 * CLOCK_SECOND)
#define JOTA_DOWNTIME_CHANCE_PERCENTAGE 5

#define JOTA_MAX_LOSSES 5

#define UNUSED(x)     ((void)(x))

#if !NETSTACK_CONF_WITH_IPV6 || !UIP_CONF_ROUTER || !UIP_CONF_IPV6_RPL
#error "This example can not work with the current contiki configuration"
#error "Check the values of: UIP_CONF_ROUTER, UIP_CONF_IPV6_RPL"
#endif

PROCESS_NAME(jota_node_process);

#endif /* _JOTA_NODE_H_ */