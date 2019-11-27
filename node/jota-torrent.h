#ifndef _JOTA_TORRENT_H_
#define _JOTA_TORRENT_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "jota-node.h"

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

#define JOTA_PIECE_BITFIELD_TYPE uint32_t
#define JOTA_PIECE_COUNT (sizeof(JOTA_PIECE_BITFIELD_TYPE) * 8)
#define JOTA_PIECE_COMPLETED_VALUE UINT32_MAX

#define JOTA_BLOCK_COUNT (4)
#define JOTA_BLOCK_SIZE (32)

// #define JOTA_CONN_BUFFER_SIZE 128
enum jota_conn_state_t {
  JOTA_CONN_STATE_IDLE = 0,
  JOTA_CONN_STATE_HANDSHAKING = 1,
  JOTA_CONN_STATE_HANDSHAKED = 2,
  JOTA_CONN_STATE_INTEREST_DECLARING = 3,
  JOTA_CONN_STATE_INTEREST_DECLARED = 4,
  JOTA_CONN_STATE_DOWNLOADING = 5,
  JOTA_CONN_STATE_UPLOADING = 6,
};

struct jota_peer_t
{
  uip_ipaddr_t ipaddr;
  struct uip_udp_conn *udp_conn;

  JOTA_PIECE_BITFIELD_TYPE piece_completed;

  bool am_choking;      // We are choking this peer
  bool am_interested;   // We are interested in this peer
  bool peer_choking;    // This peer is choking us
  bool peer_interested; // This peer is interested in us

  clock_time_t last_handshaked;
  // unsigned int num_zero_handshakes;

  clock_time_t last_choked;

  enum jota_conn_state_t state;

  int16_t uploading_piece_index;
  int16_t uploading_block_index;
  int16_t downloading_piece_index;
  int16_t downloading_block_index;

  unsigned int num_blocks_uploaded;

  clock_time_t last_rx;

  bool txing;
  clock_time_t last_tx;
  unsigned int num_losses;

  struct jota_peer_t *next;
};

void jota_insert_peer_to_list(uip_ipaddr_t *ipaddr);
int jota_remove_peer_from_list(struct jota_peer_t *p);
struct jota_peer_t *jota_get_peer_by_ipaddr(uip_ipaddr_t *ipaddr);
void jota_reset_peer(struct jota_peer_t *p);

struct jota_unassigned_peer_t {
  uip_ipaddr_t ipaddr;
  struct jota_unassigned_peer_t *next;
};
struct jota_completed_peer_t {
  uip_ipaddr_t ipaddr;
  struct jota_completed_peer_t *next;
};

/* Store completed peers */
struct jota_completed_peer_t *jota_completed_peer_get(uip_ipaddr_t *ipaddr);
void jota_completed_peer_new(uip_ipaddr_t *ipaddr);

/* Queue unassigned peers */
bool jota_unassigned_peer_is_exists(uip_ipaddr_t *ipaddr);
void jota_unassigned_peer_enqueue(uip_ipaddr_t *ipaddr);
int jota_unassigned_peer_dequeue(uip_ipaddr_t *ipaddr);

#endif /* _JOTA_TORRENT_H_ */