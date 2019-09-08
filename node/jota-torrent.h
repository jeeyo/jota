#ifndef _JOTA_TORRENT_H_
#define _JOTA_TORRENT_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "jota-node.h"

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

// #define JOTA_LOW_POWER

/*
  1x BLOCK = 256 Bytes
  32x BLOCK = 1 PIECE (32 Bytes)

  Note: JOTA_PIECE_COUNT must be less than 64 due to uint64_t
*/
#define JOTA_FILE_SIZE (4 * 1024)
// #define JOTA_PIECE_SIZE (64)

// #if ((JOTA_FILE_SIZE % JOTA_PIECE_SIZE) != 0)
//   #define JOTA_PIECE_COUNT ((unsigned int)((JOTA_FILE_SIZE / JOTA_PIECE_SIZE) + 1)
// #else
//   #define JOTA_PIECE_COUNT ((unsigned int)(JOTA_FILE_SIZE / JOTA_PIECE_SIZE))
// #endif
#define JOTA_PIECE_BITFIELD_TYPE uint32_t
#define JOTA_PIECE_COUNT (sizeof(JOTA_PIECE_BITFIELD_TYPE) * 8)
#define JOTA_PIECE_COMPLETED_VALUE UINT32_MAX
// #define JOTA_PIECE_SIZE ((unsigned int)(JOTA_FILE_SIZE / JOTA_PIECE_COUNT))
#define JOTA_PIECE_SIZE (32)

// #define JOTA_CONN_BUFFER_SIZE 128
enum jota_conn_state_t {
  JOTA_CONN_STATE_IDLE = 0,
  JOTA_CONN_STATE_HANDSHAKED = 1,
  JOTA_CONN_STATE_INTERESTING = 2,
};

// #define JOTA_PEER_FLAG_HANDSHAKED 0x01
// #define JOTA_PEER_FLAG_REQUESTING_PIECE 0x02
// #define JOTA_PEER_FLAG_DOWNLOADING_FROM 0x04
// #define JOTA_PEER_FLAG_UPLOADING_TO 0x08

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

#ifdef JOTA_LOW_POWER
  bool is_neighbor;
#endif

  // uint8_t piece_completed[JOTA_PIECE_COUNT];
  JOTA_PIECE_BITFIELD_TYPE piece_completed;

  bool am_choking;      // We are choking this peer
  bool am_interested;   // We are interested in this peer
  bool peer_choking;    // This peer is choking us
  bool peer_interested; // This peer is interested in us

  clock_time_t last_handshaked;
  clock_time_t last_choked;

  enum jota_conn_state_t state;
  int uploading_piece_index;
  int downloading_piece_index;

  bool txing;
  clock_time_t last_tx;
  unsigned int num_losses;
};

void jota_peers_init();
struct jota_peer_t *jota_get_peer_by_ipaddr(uip_ipaddr_t *ipaddr);
// struct jota_peer_t *jota_get_peer_by_socket(struct jota_tcp_socket_t *s);
// struct jota_peer_t *jota_get_available_peer();
// struct jota_peer_t *jota_get_peer_or_create_by_socket(struct jota_tcp_socket_t *s);
void jota_reset_peer(struct jota_peer_t *p);

#endif /* _JOTA_TORRENT_H_ */