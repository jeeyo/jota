#include "jota-node.h"

#if UIP_STATISTICS == 1
extern struct uip_stats uip_stat;
#endif

extern struct jota_peer_t peers[JOTA_NBR_OF_PEERS];

static struct {
  // uint8_t piece_completed[JOTA_PIECE_COUNT];
  // uint8_t piece_downloading[JOTA_PIECE_COUNT];
  uint64_t piece_completed;
  uint64_t piece_downloading;
} me;

// divide by 5 before use
static int jota_pseudo_random_c_value[] = {
  0,      // 0%
  38,     // 5%
  150,    // 10%
  320,    // 15%
  560,    // 20%
  850,    // 25%
  1200,   // 30%
  1600,   // 35%
  2000,   // 40%
  2500,   // 45%
  3000,   // 50%
  3600,   // 55%
  4200,   // 60%
  4800,   // 65%
  5700,   // 70%
  6700,   // 75%
  7500,   // 80%
  8200,   // 85%
  8900,   // 90%
  9500,   // 95%
  10000,  // 100%
};

#define JOTA_MAX_UPLOADERS 3
#define JOTA_MAX_DOWNLOADERS 3

#define BIT_SET(val, bitIndex) (val |= (1 << bitIndex))
#define BIT_CLEAR(val, bitIndex) (val &= ~(1 << bitIndex))
#define BIT_TOGGLE(val, bitIndex) (val ^= (1 << bitIndex))
#define BIT_IS_SET(val, bitIndex) (val & (1 << bitIndex))

static struct uip_udp_conn *server_conn;
static int __nbr_of_my_uploaders = 0;
static int __nbr_of_my_downloaders = 0;

PROCESS(jota_udp_server_process, "JOTA UDP Server Process");
PROCESS(jota_node_process, "JOTA Node Process");
#ifndef JOTA_BORDER_ROUTER
AUTOSTART_PROCESSES(&jota_udp_server_process, &jota_node_process);
// AUTOSTART_PROCESSES(&jota_node_process);
#endif /* JOTA_BORDER_ROUTER */
/*---------------------------------------------------------------------------*/
// // For energest module
// static unsigned long
// to_seconds(uint64_t time)
// {
//   return (unsigned long)(time / ENERGEST_SECOND);
// }
/*---------------------------------------------------------------------------*/
static bool
random_peer_to_download()
{
  // count number of peers that are HANDSHAKED
  int nbr_of_handshakeds = 0;
  int i;
  for(i = 0; i < JOTA_NBR_OF_PEERS; i++) {
    struct jota_peer_t *peer = &peers[i];
    if(peer->state == JOTA_CONN_STATE_HANDSHAKED) nbr_of_handshakeds++;
  }

  // TO-DO: Check the HANDSHAKEDs possessions
  // percentage of chance
  int chance = (nbr_of_handshakeds / (float)JOTA_NBR_OF_PEERS) * 100;

  /* Dota 2 Pseudo Random
   * P(N) = N * C
   * chance = n * c;
   */
  static int n = 0;

  // round up to multiple of five
  chance = chance + ((5 - (chance % 5)) % 5);

  // find c value from table
  int c = jota_pseudo_random_c_value[chance / 5];

  // calculate chance
  int pn = n * c;

  // do random
  // srand(clock());
  srand(clock_time());
  int n_rand = rand() % 10000;

  if(n_rand < pn) {
    n = 0;
    return true;
  }
  else {
    n++;
    return false;
  }

  // return n_rand < chance;
}
/*---------------------------------------------------------------------------*/
static int
random_unpossessed_piece_from_peer(struct jota_peer_t *peer)
{
  int piece_index = -1;
  int i;

  // count all unpossessed and find first unpossessed
  int unpossessed_count = 0;
  for(i = JOTA_PIECE_COUNT; i > 0; i--)
    // if(peer->piece_completed[i] == '1' &&
    //     me.piece_completed[i] == '0' &&
    //     me.piece_downloading[i] == '0') unpossessed_count++;
    if(BIT_IS_SET(peer->piece_completed, i) &&
      !(BIT_IS_SET(me.piece_completed, i)) &&
      !(BIT_IS_SET(me.piece_downloading, i))) unpossessed_count++;
  
  if(unpossessed_count == 0) return -1;
  
  // srand(clock());
  srand(clock_time());
  int n_rand = (rand() % unpossessed_count) + 1;
  
  // find next 'randomed' unpossessed
  for(piece_index = i; piece_index < JOTA_PIECE_COUNT; piece_index++)
  {
    // if(peer->piece_completed[piece_index] == '1' &&
    //   me.piece_completed[piece_index] == '0' &&
    //   me.piece_downloading[piece_index] == '0') n_rand--;
    if(BIT_IS_SET(peer->piece_completed, i) &&
      !(BIT_IS_SET(me.piece_completed, i)) &&
      !(BIT_IS_SET(me.piece_downloading, i))) n_rand--;
    
    if(n_rand <= 0) break;
  }

  if(piece_index >= JOTA_PIECE_COUNT) return -1;
  return piece_index;
}
/*---------------------------------------------------------------------------*/
static int
jota_deserialize(const char *cmsg, char (*result)[JT_SERIALIZE_RESULT_LEN], int max_result)
{
  int i = 0;

  char *msg = strdup(cmsg);
  char *token = strtok(msg, ":");

  while(token != NULL)
  {
    strncpy(result[i++], token, JT_SERIALIZE_RESULT_LEN);
    if(i >= max_result) break;
    token = strtok(NULL, ":");
  }
  free(msg);
  return i - 1;
}
/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
  if(uip_newdata()) {

    char *endptr;

    char serialized[uip_datalen() + 1];
    memcpy(serialized, uip_appdata, uip_datalen());
    serialized[uip_datalen()] = '\0';

    struct jota_peer_t *peer = jota_get_peer_by_ipaddr(&UIP_IP_BUF->srcipaddr);
    if(peer == NULL) {
      printf("undefined peer [");
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("]\n");
      goto finally;
    }

    // printf("Received '%.*s' [", uip_datalen(), (char *)uip_appdata);
    // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
    // printf("]\n");

    uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
    server_conn->rport = UIP_UDP_BUF->srcport;

    peer->txing = false;
    
    if(memcmp(serialized, JT_HANDSHAKE_MSG, strlen(JT_HANDSHAKE_MSG)) == 0 || memcmp(serialized, JT_ACK_HANDSHAKE_MSG, strlen(JT_ACK_HANDSHAKE_MSG)) == 0)
    {
      /*
       * 0 = JT_HANDSHAKE_MSG
       * 1 = Bitfield of possession
       */
      char result[2][JT_SERIALIZE_RESULT_LEN];
      jota_deserialize(serialized, result, 2);

      // TO-DO: Bound check
      // memcpy(&peer->piece_completed[0], &result[1][0], JOTA_PIECE_COUNT);
      peer->piece_completed = strtoul(result[1], &endptr, 10);
      peer->state = JOTA_CONN_STATE_HANDSHAKED;
      peer->last_handshaked = clock_time();
      
      // printf("Received HANDSHAKE from ");
      // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      // printf(" [%" PRIu64 "]\n", peer->piece_completed);

      // Skip sending ACK to an ACK
      if(memcmp(serialized, JT_ACK_HANDSHAKE_MSG, strlen(JT_ACK_HANDSHAKE_MSG)) == 0) goto finally;

      // Send ACK_HANDSHAKE back
      // uint8_t mbuf[strlen(JT_ACK_HANDSHAKE_MSG) + 1 + JOTA_PIECE_COUNT + 1];
      // size_t mbuflen = sprintf((char *)mbuf, "%s:%.*s", JT_ACK_HANDSHAKE_MSG, JOTA_PIECE_COUNT, &me.piece_completed[0]);

      uint8_t mbuf[strlen(JT_ACK_HANDSHAKE_MSG) + 20 + 1];
      size_t mbuflen = sprintf((char *)mbuf, "%s:%" PRIu64, JT_ACK_HANDSHAKE_MSG, me.piece_completed);
      
      uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
    }
    else if(memcmp(serialized, JT_INTEREST_MSG, strlen(JT_INTEREST_MSG)) == 0)
    {
      /*
       * 0 = JT_INTEREST_MSG
       * 1 = Piece Index
       */
      char result[2][JT_SERIALIZE_RESULT_LEN];
      jota_deserialize(serialized, result, 2);

      peer->uploading_piece_index = strtoul(result[1], &endptr, 10);
      peer->peer_interested = true;

      // TO-DO: Perhaps combine them to am_choking?
      int choking = peer->am_choking || (__nbr_of_my_downloaders >= JOTA_MAX_DOWNLOADERS);
      // int choking = peer->am_choking;

// #ifdef JOTA_LOW_POWER
//       choking = peer->is_neighbor && choking;
// #endif

      if(!choking) __nbr_of_my_downloaders++;

      // printf("Received INTEREST (%d) from ", peer->uploading_piece_index);
      // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      // printf("\n");

      // Send CHOKE back (1 = Choke, 0 = Unchoke)
      uint8_t mbuf[strlen(JT_CHOKE_MSG) + 1 + 1 + 1];
      size_t mbuflen = sprintf((char *)mbuf, "%s:%1d", JT_CHOKE_MSG, choking);

      uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
    }
    else if(memcmp(serialized, JT_CHOKE_MSG, strlen(JT_CHOKE_MSG)) == 0)
    {
      /*
       * 0 = JT_CHOKE_MSG
       * 1 = 1 = Choked, 0 = Unchoked
       */
      char result[2][JT_SERIALIZE_RESULT_LEN];
      jota_deserialize(serialized, result, 2);

      peer->peer_choking = strtoul(result[1], &endptr, 10);
      peer->state = JOTA_CONN_STATE_INTERESTING;

      printf("Received CHOKE (%d) from ", peer->peer_choking);
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("\n");
    }
    else if(memcmp(serialized, JT_REQUEST_MSG, strlen(JT_REQUEST_MSG)) == 0)
    {
      /*
       * 0 = JT_REQUEST_MSG
       */
      
      // printf("Received REQUEST from ");
      // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      // printf("\n");
      
      // TO-DO: Remove declaring `piece_content`
      uint8_t piece_content[JOTA_PIECE_SIZE];
      memset(piece_content, '0' + (peer->uploading_piece_index % 10), JOTA_PIECE_SIZE);
      unsigned short piece_checksum = crc16_data(piece_content, JOTA_PIECE_SIZE, 0);

      if(__nbr_of_my_downloaders > 0) __nbr_of_my_downloaders--;

      uint8_t mbuf[strlen(JT_PIECE_MSG) + 1 + 2 + 1 + JOTA_PIECE_SIZE + 1 + 5];
      size_t mbuflen = sprintf((char *)mbuf, "%s:%02d:%s:%05d", JT_PIECE_MSG, peer->uploading_piece_index, piece_content, piece_checksum);

      uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
    }
    else if(memcmp(serialized, JT_PIECE_MSG, strlen(JT_PIECE_MSG)) == 0)
    {
      /*
       * 0 = JT_PIECE_MSG
       * 1 = Piece Index
       * 2 = Piece Binary
       * 3 = Checksum
       */
      char result[4][JT_SERIALIZE_RESULT_LEN];
      jota_deserialize(serialized, result, 4);

      int piece_index = strtoul(result[1], &endptr, 10);

      // TO-DO: Bound check
      unsigned char piece[JOTA_PIECE_SIZE];
      memcpy(&piece[0], &result[2][0], JOTA_PIECE_SIZE);

      unsigned short my_crc = crc16_data(piece, JOTA_PIECE_SIZE, 0);
      unsigned short src_crc = strtoul(result[3], &endptr, 10);

      // me.piece_downloading[piece_index] = '0';
      BIT_CLEAR(me.piece_downloading, piece_index);
      if(__nbr_of_my_uploaders > 0) __nbr_of_my_uploaders--;

      // TO-DO: Verify piece index
      bool integrity = my_crc == src_crc;
      if(integrity)
        // me.piece_completed[piece_index] = '1';
        BIT_SET(me.piece_completed, piece_index);
      else
        printf("integrity false\n");

      printf("Received PIECE (%d) from ", piece_index);
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("\ncompleted: [%" PRIu64 "]\ndownloading: [%" PRIu64 "]\n", me.piece_completed, me.piece_downloading);
      // printf("\n");

      peer->state = JOTA_CONN_STATE_HANDSHAKED;

      // Check if completed
      // if(memchr((const char *)&me.piece_completed[0], '0', JOTA_PIECE_COUNT) == NULL)
      if(me.piece_completed == UINT64_MAX)
#if UIP_STATISTICS == 1
        printf("Download completed [dropped %d, forwarded %d]\n", uip_stat.ip.drop, uip_stat.ip.forwarded);
#else
        printf("Download completed\n");
#endif
    }

finally:
    /* Restore server connection to allow data from any node */
    memset(&server_conn->ripaddr, 0, sizeof(server_conn->ripaddr));
    server_conn->rport = 0;
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(jota_udp_server_process, ev, data)
{
  PROCESS_BEGIN();

#ifndef JOTA_BORDER_ROUTER
  // memset(&me.piece_completed[0], '0', JOTA_PIECE_COUNT);
  me.piece_completed = 0;
#else
  // memset(&me.piece_completed[0], '1', JOTA_PIECE_COUNT);
  me.piece_completed = UINT64_MAX;
#endif
  // memset(&me.piece_downloading[0], '0', JOTA_PIECE_COUNT);
  me.piece_downloading = 0;

  server_conn = udp_new(NULL, 0, NULL);
  udp_bind(server_conn, UIP_HTONS(JOTA_CONN_PORT));

  while(1)
  {
    PROCESS_YIELD();
    
    if(ev == tcpip_event) {
      tcpip_handler();
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(jota_node_process, ev, data)
{
  PROCESS_BEGIN();

  static int i;

  jota_peers_init();

  static struct etimer delayed_start_tmr;
  etimer_set(&delayed_start_tmr, ((60 + (node_id * 10)) * CLOCK_SECOND));
  // etimer_set(&delayed_start_tmr, ((60 + (node_id * 10)) * CLOCK_SECOND));
  // etimer_set(&delayed_start_tmr, 60 * CLOCK_SECOND);

  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&delayed_start_tmr));
    break;
  }

#ifdef JOTA_LOW_POWER
  // Mark neighbors
  uip_ds6_nbr_t *nbr;
  for(nbr = uip_ds6_nbr_head();
      nbr != NULL;
      nbr = uip_ds6_nbr_next(nbr))
  {
    // TO-DO: dirty hack to change prefix from `fe80` to `fd00`
    uip_ipaddr_t ipaddr;
    uip_ipaddr_copy(&ipaddr, &nbr->ipaddr);

    ipaddr.u16[0] = 253;
    ipaddr.u16[1] = 0;
    ipaddr.u16[2] = 0;
    ipaddr.u16[3] = 0;

    printf("nbr ");
    uiplib_ipaddr_print(&ipaddr);
    printf("\n");

    struct jota_peer_t *peer = jota_get_peer_by_ipaddr(&ipaddr);
    if(peer != NULL) peer->is_neighbor = true;
  }
#endif

  static struct etimer interval_tmr;
  etimer_set(&interval_tmr, 5 * CLOCK_SECOND);

  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&interval_tmr));
    etimer_reset(&interval_tmr);

    // Completed downloading, won't transmit anymore
    // if(memchr((const char *)&me.piece_completed[0], '0', JOTA_PIECE_COUNT) == NULL) {
    if(me.piece_completed == UINT64_MAX)
      continue;
    
    // PROCESS_PAUSE();
    // for(i = 0; i < JOTA_PIECE_COUNT / 8; i++)
    //   if(me.piece_completed[i] != UINT8_MAX) goto tx;
    // goto choke;

// tx:
    for(i = 0; i < JOTA_NBR_OF_PEERS; i++)
    {
      // Skip for ourselves
      if(i == node_id - 1) continue;
      
      struct jota_peer_t *peer = &peers[i];

#ifdef JOTA_LOW_POWER
      // printf("nbr (%d) [", peer->is_neighbor);
      // uiplib_ipaddr_print(&peer->ipaddr);
      // printf("]\n");

      if(!peer->is_neighbor) continue;
#endif

      // Transmission timeout handler
      if(peer->txing == true)
      {
        clock_time_t diff = (clock_time() - peer->last_tx) + 1;
        if(JOTA_TX_TIMEOUT < diff) {
          
          printf("No responses in time, retransmitting [");
          uiplib_ipaddr_print(&peer->ipaddr);
          printf("] %d\n", peer->state);
          
          // TO-DO: May need verification
          if(peer->state == JOTA_CONN_STATE_HANDSHAKED)
          {
            if(__nbr_of_my_uploaders > 0) __nbr_of_my_uploaders--;
            peer->am_interested = false;
          }
          else if(peer->state == JOTA_CONN_STATE_INTERESTING)
          {
            // me.piece_downloading[peer->downloading_piece_index] = '0';
            BIT_CLEAR(me.piece_downloading, peer->downloading_piece_index);
            peer->state = JOTA_CONN_STATE_HANDSHAKED;
          }
          peer->txing = false;
        }
        continue;
      }

      // Not Handshaked
      if(peer->state == JOTA_CONN_STATE_IDLE)
      {
        if(peer->peer_choking == 1)
        {
          if(peer->last_choked == 0)
            peer->last_choked = clock_time();
          else {
            clock_time_t diff = (clock_time() - peer->last_choked) + 1;
            if(JOTA_CHOKED_TIMEOUT < diff) {
              peer->last_choked = 0;
              peer->peer_choking = 0;
            }
          }
        }

        // uint8_t mbuf[strlen(JT_HANDSHAKE_MSG) + 1 + JOTA_PIECE_COUNT + 1];
        // size_t mbuflen = sprintf((char *)mbuf, "%s:%.*s", JT_HANDSHAKE_MSG, JOTA_PIECE_COUNT, &me.piece_completed[0]);

        uint8_t mbuf[strlen(JT_HANDSHAKE_MSG) + 20 + 1];
        size_t mbuflen = sprintf((char *)mbuf, "%s:%" PRIu64, JT_HANDSHAKE_MSG, me.piece_completed);

        // printf("Sending HANDSHAKE to ");
        // uiplib_ipaddr_print(&peer->ipaddr);
        // printf(" [%.*s (%d bytes)]\n", mbuflen, mbuf, mbuflen);

        uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
        peer->txing = true;
        peer->last_tx = clock_time();
      }
      // Just Handshaked
      else if(peer->state == JOTA_CONN_STATE_HANDSHAKED)
      {
        // Check if we should update their possession bitfield again
        clock_time_t diff = (clock_time() - peer->last_handshaked) + 1;
        if(JOTA_HANDSHAKED_TIMEOUT < diff) {
          peer->state = JOTA_CONN_STATE_IDLE;
          continue;
        }

        // Download slot available
        if(__nbr_of_my_uploaders > JOTA_MAX_UPLOADERS) { printf("download slot not available\n"); continue; }

        // God wants this peer to be downloaded from
        // if(peer->am_interested == true) continue;
        if(!random_peer_to_download()) continue;

        peer->downloading_piece_index = random_unpossessed_piece_from_peer(peer);
        if(peer->downloading_piece_index > -1)
        {
          __nbr_of_my_uploaders++;

          peer->am_interested = true;
          
          uint8_t mbuf[strlen(JT_INTEREST_MSG) + 1 + 2 + 1];
          size_t mbuflen = sprintf((char *)mbuf, "%s:%02d", JT_INTEREST_MSG, peer->downloading_piece_index);

          printf("Sending INTEREST (%d) to ", peer->downloading_piece_index);
          uiplib_ipaddr_print(&peer->ipaddr);
          printf("\n");

          uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
          peer->txing = true;
          peer->last_tx = clock_time();
        } else {
          printf("downloading_piece_index: -1 [");
          uiplib_ipaddr_print(&peer->ipaddr);
          printf("]\n");
        }
      }
      // Just Requested
      else if(peer->state == JOTA_CONN_STATE_INTERESTING)
      {
        if(peer->peer_choking == 0)
        {
          uint8_t mbuf[strlen(JT_REQUEST_MSG) + 1];
          size_t mbuflen = sprintf((char *)mbuf, "%s", JT_REQUEST_MSG);

          // me.piece_downloading[peer->downloading_piece_index] = '1';
          BIT_SET(me.piece_downloading, peer->downloading_piece_index);

          printf("Sending REQUEST to ");
          uiplib_ipaddr_print(&peer->ipaddr);
          printf("\n");

          uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
          peer->txing = true;
          peer->last_tx = clock_time();
        }
        else
        {
          if(__nbr_of_my_uploaders > 0) __nbr_of_my_uploaders--;

          if(peer->last_choked == 0)
            peer->last_choked = clock_time();
          else {
            clock_time_t diff = (clock_time() - peer->last_choked) + 1;
            if(JOTA_CHOKED_TIMEOUT < diff) {
              peer->last_choked = 0;
              peer->peer_choking = 0;
              peer->state = JOTA_CONN_STATE_HANDSHAKED;
            }
          }
        }
      }
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
