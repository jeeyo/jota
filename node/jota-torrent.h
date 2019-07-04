#ifndef _JOTA_TORRENT_H_
#define _JOTA_TORRENT_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "jota-node.h"

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

// You have to manually check if maximum connections reached before accepting a new connection
#define JOTA_MAX_PEERS 20           // number of jota_peer_t (to keep information of peers)
#define JOTA_MAX_PEER_CONNS 5       // number of tcp_socket (to keep connection to peers)
#define JOTA_MAX_UPLOAD_SLOTS 4

#define JT_BROADCAST_TRIGGER_TEXT "DEADBEEF"
#define JT_PEER_PIECE_REQUEST "CAFEFEED"
#define JT_PEER_PIECE_RESPONSE "ABADCAFE"

/*
  1x BLOCK = 256 Bytes
  32x BLOCK = 1 PIECE (32 Bytes)
*/
#define JOTA_FILE_SIZE (1 * 1024)
#define JOTA_PIECE_SIZE (32)                  // 32 B

#if ((JOTA_FILE_SIZE % JOTA_PIECE_SIZE) != 0)
  #define JOTA_PIECE_COUNT ((unsigned int)((JOTA_FILE_SIZE / JOTA_PIECE_SIZE) + 1)
#else
  #define JOTA_PIECE_COUNT ((unsigned int)(JOTA_FILE_SIZE / JOTA_PIECE_SIZE))
#endif

#define JOTA_CONN_BUFFER_SIZE 128
enum jota_conn_state_t {
  JOTA_CONN_STATE_IDLE,
  JOTA_CONN_STATE_TXING,
};

#define JOTA_PEER_FLAG_HANDSHAKED 0x01
#define JOTA_PEER_FLAG_DOWNLOADING_FROM 0x02
#define JOTA_PEER_FLAG_UPLOADING_TO 0x04

// struct jota_tcp_socket_t {
//   struct tcp_socket s;
//   enum jota_conn_state_t conn_state;
//   uint8_t inputbuf[JOTA_CONN_BUFFER_SIZE];
//   uint8_t outputbuf[JOTA_CONN_BUFFER_SIZE];
// };

struct jota_peer_t {
  uip_ipaddr_t ipaddr;
  struct uip_udp_conn *udp_conn;
  uint16_t link_metric;

  uint8_t piece_completed[JOTA_PIECE_COUNT];
  uint8_t piece_downloading[JOTA_PIECE_COUNT];

  bool am_choking;
  bool am_interested;
  bool peer_choking;
  bool peer_interested;

  uint8_t flags;
  enum jota_conn_state_t state;
  clock_time_t last_tx;
};

void jota_peers_init();
struct jota_peer_t *jota_get_peer_by_ipaddr(uip_ipaddr_t *ipaddr);
// struct jota_peer_t *jota_get_peer_by_socket(struct jota_tcp_socket_t *s);
// struct jota_peer_t *jota_get_available_peer();
// struct jota_peer_t *jota_get_peer_or_create_by_socket(struct jota_tcp_socket_t *s);
void jota_reset_peer(struct jota_peer_t *p);

#endif /* _JOTA_TORRENT_H_ */