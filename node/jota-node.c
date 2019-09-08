#include "jota-node.h"

#if UIP_STATISTICS == 1
extern struct uip_stats uip_stat;
#endif

extern struct jota_peer_t peers[JOTA_NBR_OF_PEERS];
extern unsigned int __nbr_of_peers;

static struct {
  // uint8_t piece_completed[JOTA_PIECE_COUNT];
  // uint8_t piece_downloading[JOTA_PIECE_COUNT];
  JOTA_PIECE_BITFIELD_TYPE piece_completed;
  JOTA_PIECE_BITFIELD_TYPE piece_downloading;
} me;

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x01 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x80 ? '1' : '0') 

#define JOTA_MAX_UPLOADERS 3
#define JOTA_MAX_DOWNLOADERS 3

/* Bitfield Utilities */
/*---------------------------------------------------------------------------*/
// #define BIT_SET(val, bitIndex) (val |= (1 << bitIndex))
// #define BIT_CLEAR(val, bitIndex) (val &= ~(1 << bitIndex))
// #define BIT_TOGGLE(val, bitIndex) (val ^= (1 << bitIndex))
// #define BIT_IS_SET(val, bitIndex) (val & (1 << bitIndex))

void jota_bitfield_set_bit(JOTA_PIECE_BITFIELD_TYPE *val, unsigned int bit)
{
  JOTA_PIECE_BITFIELD_TYPE tmp = 1;
  tmp <<= bit;
  *val |= tmp;
}

void jota_bitfield_clear_bit(JOTA_PIECE_BITFIELD_TYPE *val, unsigned int bit)
{
  JOTA_PIECE_BITFIELD_TYPE tmp = 1;
  tmp <<= bit;
  *val &= ~tmp;
}

bool jota_bitfield_bit_is_set(JOTA_PIECE_BITFIELD_TYPE *val, unsigned int bit)
{
  JOTA_PIECE_BITFIELD_TYPE tmp = 1;
  tmp <<= bit;
  return *val & tmp;
}
/*---------------------------------------------------------------------------*/

static struct uip_udp_conn *server_conn;
static int __nbr_of_my_uploaders = 0;
static int __nbr_of_my_downloaders = 0;

PROCESS(jota_udp_server_process, "JOTA UDP Server Process");
PROCESS(jota_node_process, "JOTA Node Process");
#ifndef JOTA_BORDER_ROUTER
AUTOSTART_PROCESSES(&jota_udp_server_process, &jota_node_process);
#endif /* JOTA_BORDER_ROUTER */
/*---------------------------------------------------------------------------*/
// // For energest module
// static unsigned long
// to_seconds(uint64_t time)
// {
//   return (unsigned long)(time / ENERGEST_SECOND);
// }
static void
map_peers_to_neighbors()
{
  if(__nbr_of_peers >= JOTA_NBR_OF_PEERS) return;

  struct jota_peer_t *peer;
  uip_ds6_nbr_t *nbr;
  uip_ipaddr_t ipaddr;

  // printf("nbr ");
  for(nbr = uip_ds6_nbr_head();
      nbr != NULL;
      nbr = uip_ds6_nbr_next(nbr))
  {
    // TO-DO: dirty hack to change prefix from `fe80` to `fd00`
    uip_ipaddr_copy(&ipaddr, &nbr->ipaddr);

    ipaddr.u16[0] = 253;
    ipaddr.u16[1] = 0;
    ipaddr.u16[2] = 0;
    ipaddr.u16[3] = 0;

    if(jota_get_peer_by_ipaddr(&ipaddr) != NULL) continue;

    peer = &peers[__nbr_of_peers++];
    // printf("__nbr_of_peers: %d\n", __nbr_of_peers);

    uip_ipaddr_copy(&peer->ipaddr, &ipaddr);

    // uiplib_ipaddr_print(&peer->ipaddr);
    // printf("\n");
    // printf("%u ", peer->ipaddr.u8[15]);

    peer->udp_conn = udp_new(&peer->ipaddr, UIP_HTONS(JOTA_CONN_PORT), NULL);

#ifdef JOTA_LOW_POWER
    peer->is_neighbor = true;
#endif
  }
  // printf("\n");
}
/*---------------------------------------------------------------------------*/
static int
random_piece_and_peer()
{
  // Download slot available
  if(__nbr_of_my_uploaders > JOTA_MAX_UPLOADERS) return -1;

  if(__nbr_of_peers == 0) return -1;

  int i;
  // uint8_t masked_possessions[JOTA_PIECE_COUNT] = { '0' };
  JOTA_PIECE_BITFIELD_TYPE masked_possessions = 0;

  int avail_peers[JOTA_NBR_OF_PEERS] = { -1 };
  unsigned int avail_peers_count = 0;

  for(i = 0; i < __nbr_of_peers; i++) {

    struct jota_peer_t *peer = &peers[i];

    if(!peer->peer_choking &&
      peer->am_interested == false &&
      peer->downloading_piece_index == -1 &&
      peer->state == JOTA_CONN_STATE_HANDSHAKED)
    {
      // for(piece_index = 0; piece_index < JOTA_PIECE_COUNT; piece_index++) {
      //   masked_possessions[piece_index] = masked_possessions[piece_index] == '1' || peer->piece_completed[piece_index] == '1' ? '1' : '0';
      // }
      masked_possessions |= peer->piece_completed;
      avail_peers[avail_peers_count++] = i;
    }
  }

  if(avail_peers_count == 0) return -1;

  // for(piece_index = 0; piece_index < JOTA_PIECE_COUNT; piece_index++) {
  //   masked_possessions[piece_index] = me.piece_completed[piece_index] == '1' || me.piece_downloading[piece_index] == '1' ? '0' : masked_possessions[piece_index];
  // }
  masked_possessions -= me.piece_completed | me.piece_downloading;

  // printf("(%d) masked_possessions: ", __nbr_of_peers);
  unsigned int unpossessed_count = 0;
  for(i = 0; i < JOTA_PIECE_COUNT; i++) {
    // if(masked_possessions[i] == '1') unpossessed_count++;
    if(jota_bitfield_bit_is_set(&masked_possessions, i)) unpossessed_count++;
    
    // if(jota_bitfield_bit_is_set(&masked_possessions, i)) printf("1");
    // else printf("0");
  }
  // printf("\n");
  
  if(unpossessed_count == 0) return -1;

  // srand(clock_time());
  // int n_rand = (rand() % unpossessed_count) + 1;
  unsigned short n_rand = (random_rand() % unpossessed_count) + 1;

  // find next 'randomed' unpossessed
  for(i = 0; i < JOTA_PIECE_COUNT; i++) {
    // if(masked_possessions[i] == '1') n_rand--;
    if(jota_bitfield_bit_is_set(&masked_possessions, i)) n_rand--;

    if(n_rand <= 0) break;
  }

  if(i >= JOTA_PIECE_COUNT) return -1;

  // random peer
  // srand(clock_time());
  // n_rand = rand() % avail_peers_count;
  n_rand = random_rand() % avail_peers_count;

  struct jota_peer_t *peer = &peers[avail_peers[n_rand]];

  if(!peer->peer_choking &&
      peer->am_interested == false &&
      peer->downloading_piece_index == -1 &&
      peer->state == JOTA_CONN_STATE_HANDSHAKED &&
      // peer->piece_completed[i] == '1')
      jota_bitfield_bit_is_set(&peer->piece_completed, i))
  {
    peer->am_interested = true;
    peer->downloading_piece_index = i;
    return 0;
  }

  return -1;
}
/*---------------------------------------------------------------------------*/
static int
jota_deserialize(char *msg, char (*result)[JT_SERIALIZE_RESULT_LEN], int max_result)
{
  int i = 0;

  // char *msg = strdup(cmsg);
  char *token = strtok(msg, ":");

  while(token != NULL)
  {
    strncpy(result[i++], token, JT_SERIALIZE_RESULT_LEN);
    if(i >= max_result) break;
    token = strtok(NULL, ":");
  }
  // free(msg);
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

    // printf("Received '%.*s' [", uip_datalen(), (char *)uip_appdata);
    // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
    // printf("]\n");

    struct jota_peer_t *peer = jota_get_peer_by_ipaddr(&UIP_IP_BUF->srcipaddr);
    if(peer == NULL) {
      printf("undefined peer [");
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("]\n");
      goto finally;
    }

    uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
    server_conn->rport = UIP_UDP_BUF->srcport;

    peer->txing = false;
    peer->num_losses = 0;
    
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
      // printf(" [%" PRIu32 "]\n", peer->piece_completed);

      // Skip sending ACK to an ACK
      if(memcmp(serialized, JT_ACK_HANDSHAKE_MSG, strlen(JT_ACK_HANDSHAKE_MSG)) == 0) goto finally;

      // Send ACK_HANDSHAKE back
      // uint8_t mbuf[strlen(JT_ACK_HANDSHAKE_MSG) + 1 + JOTA_PIECE_COUNT + 1];
      // size_t mbuflen = sprintf((char *)mbuf, "%s:%.*s", JT_ACK_HANDSHAKE_MSG, JOTA_PIECE_COUNT, &me.piece_completed[0]);

      uint8_t mbuf[strlen(JT_ACK_HANDSHAKE_MSG) + 20 + 1];
      size_t mbuflen = sprintf((char *)mbuf, "%s:%" PRIu32, JT_ACK_HANDSHAKE_MSG, me.piece_completed);
      
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
      jota_bitfield_clear_bit(&me.piece_downloading, piece_index);
      if(__nbr_of_my_uploaders > 0) __nbr_of_my_uploaders--;

      // TO-DO: Verify piece index
      bool integrity = my_crc == src_crc;
      if(integrity)
        // me.piece_completed[piece_index] = '1';
        jota_bitfield_set_bit(&me.piece_completed, piece_index);
      else
        printf("integrity false\n");

      printf("Received PIECE (%d) from ", piece_index);
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("\n["BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN"]",
        BYTE_TO_BINARY(me.piece_completed), BYTE_TO_BINARY(me.piece_completed >> 8),
        BYTE_TO_BINARY(me.piece_completed >> 16), BYTE_TO_BINARY(me.piece_completed >> 24)
      );
      // printf("\ncompleted: [%" PRIu32 "] downloading: [%" PRIu32 "]\n", me.piece_completed, me.piece_downloading);
      printf("\n");

      peer->state = JOTA_CONN_STATE_HANDSHAKED;
      peer->am_interested = false;
      peer->downloading_piece_index = -1;

      // Check if completed
      // if(memchr((const char *)&me.piece_completed[0], '0', JOTA_PIECE_COUNT) == NULL)
      if(me.piece_completed == JOTA_PIECE_COMPLETED_VALUE)
      {
// #if UIP_STATISTICS == 1
//         printf("Download completed [dropped %d, forwarded %d]\n", uip_stat.ip.drop, uip_stat.ip.forwarded);
// #else
//         printf("Download completed\n");
// #endif
        printf("The end\n");

        // energest_flush();

        // printf("Energest:\n");
        // printf(" CPU          %4lus LPM      %4lus DEEP LPM %4lus  Total time %lus\n",
        //         to_seconds(energest_type_time(ENERGEST_TYPE_CPU)),
        //         to_seconds(energest_type_time(ENERGEST_TYPE_LPM)),
        //         to_seconds(energest_type_time(ENERGEST_TYPE_DEEP_LPM)),
        //         to_seconds(ENERGEST_GET_TOTAL_TIME()));
        // printf(" Radio LISTEN %4lus TRANSMIT %4lus OFF      %4lus\n",
        //         to_seconds(energest_type_time(ENERGEST_TYPE_LISTEN)),
        //         to_seconds(energest_type_time(ENERGEST_TYPE_TRANSMIT)),
        //         to_seconds(ENERGEST_GET_TOTAL_TIME()
        //                   - energest_type_time(ENERGEST_TYPE_TRANSMIT)
        //                   - energest_type_time(ENERGEST_TYPE_LISTEN)));
      }
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

  printf("UIP_CONF_BUFFER_SIZE = %d\n", UIP_CONF_BUFFER_SIZE);

#ifndef JOTA_BORDER_ROUTER
  // memset(&me.piece_completed[0], '0', JOTA_PIECE_COUNT);
  me.piece_completed = 0;
#else
  // memset(&me.piece_completed[0], '1', JOTA_PIECE_COUNT);
  me.piece_completed = JOTA_PIECE_COMPLETED_VALUE;
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
  
  struct jota_peer_t *peer;
  static int i;

  jota_peers_init();

  static struct etimer delayed_start_tmr;
  etimer_set(&delayed_start_tmr, 70 * CLOCK_SECOND);
  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&delayed_start_tmr));
    break;
  }
  
  map_peers_to_neighbors();

  etimer_set(&delayed_start_tmr, (node_id * 10) * CLOCK_SECOND);
  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&delayed_start_tmr));
    break;
  }

  static struct etimer interval_tmr;
  static unsigned int interval_time = 1;
  etimer_set(&interval_tmr, interval_time * CLOCK_SECOND);

  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&interval_tmr));
    etimer_set(&interval_tmr, interval_time * CLOCK_SECOND);

    // printf("mapping peers...");
    map_peers_to_neighbors();
    // printf("done\n");

    // Completed downloading, won't transmit anymore
    // if(memchr((const char *)&me.piece_completed[0], '0', JOTA_PIECE_COUNT) == NULL)
    if(me.piece_completed == JOTA_PIECE_COMPLETED_VALUE)
      PROCESS_EXIT();
    
    // PROCESS_PAUSE();
    // for(i = 0; i < JOTA_PIECE_COUNT / 8; i++)
    //   if(me.piece_completed[i] != UINT8_MAX) goto tx;
    // goto choke;

    /* Check if all peers are unreachable */
    for(i = 0; i < __nbr_of_peers; i++)
    {
      peer = &peers[i];
      if(peer->num_losses == 0) goto tx;
    }
    interval_time += 5;

tx:
    // printf("randomize...");
    // random_piece_and_peer();
    if(random_piece_and_peer() < 0)
      interval_time += 1;
    else
      interval_time = 1;
    // printf("done\n");
    
    for(i = 0; i < __nbr_of_peers; i++)
    {
      // // Skip for ourselves
      // if(i == node_id - 1) continue;
      
      peer = &peers[i];

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
        if(JOTA_TX_TIMEOUT + (peer->num_losses * 2 * CLOCK_SECOND) < diff)
        {
          peer->txing = false;
          peer->num_losses += 1;
          
          printf("No responses in time, retransmitting [");
          uiplib_ipaddr_print(&peer->ipaddr);
          printf("] [%u]\n", peer->num_losses);

          // TO-DO: May need verification
          if(peer->state == JOTA_CONN_STATE_HANDSHAKED)
          {
            if(__nbr_of_my_uploaders > 0) __nbr_of_my_uploaders--;
            peer->am_interested = false;
          }
          else if(peer->state == JOTA_CONN_STATE_INTERESTING)
          {
            // me.piece_downloading[peer->downloading_piece_index] = '0';
            jota_bitfield_clear_bit(&me.piece_downloading, peer->downloading_piece_index);
            peer->state = JOTA_CONN_STATE_HANDSHAKED;
          }
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
        size_t mbuflen = sprintf((char *)mbuf, "%s:%" PRIu32, JT_HANDSHAKE_MSG, me.piece_completed);

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

        if(peer->am_interested && peer->downloading_piece_index != -1)
        {
          __nbr_of_my_uploaders++;
          
          uint8_t mbuf[strlen(JT_INTEREST_MSG) + 1 + 2 + 1];
          size_t mbuflen = sprintf((char *)mbuf, "%s:%02d", JT_INTEREST_MSG, peer->downloading_piece_index);

          printf("Sending INTEREST (%d) to ", peer->downloading_piece_index);
          uiplib_ipaddr_print(&peer->ipaddr);
          printf("\n");

          uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
          peer->txing = true;
          peer->last_tx = clock_time();
        }
      }
      // Just Requested
      else if(peer->state == JOTA_CONN_STATE_INTERESTING)
      {
        if(peer->peer_choking == 0)
        {
          // me.piece_downloading[peer->downloading_piece_index] = '1';
          jota_bitfield_set_bit(&me.piece_downloading, peer->downloading_piece_index);

          printf("Sending REQUEST to ");
          uiplib_ipaddr_print(&peer->ipaddr);
          printf("\n");
          
          uint8_t mbuf[strlen(JT_REQUEST_MSG) + 1];
          size_t mbuflen = sprintf((char *)mbuf, "%s", JT_REQUEST_MSG);

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

    // // Print energy consumption periodically
    // if(etimer_expired(&energest_tmr))
    // {
    //   energest_flush();

    //   printf("Energest:\n");
    //   printf(" CPU          %4lus LPM      %4lus DEEP LPM %4lus  Total time %lus\n",
    //           to_seconds(energest_type_time(ENERGEST_TYPE_CPU)),
    //           to_seconds(energest_type_time(ENERGEST_TYPE_LPM)),
    //           to_seconds(energest_type_time(ENERGEST_TYPE_DEEP_LPM)),
    //           to_seconds(ENERGEST_GET_TOTAL_TIME()));
    //   printf(" Radio LISTEN %4lus TRANSMIT %4lus OFF      %4lus\n",
    //           to_seconds(energest_type_time(ENERGEST_TYPE_LISTEN)),
    //           to_seconds(energest_type_time(ENERGEST_TYPE_TRANSMIT)),
    //           to_seconds(ENERGEST_GET_TOTAL_TIME()
    //                     - energest_type_time(ENERGEST_TYPE_TRANSMIT)
    //                     - energest_type_time(ENERGEST_TYPE_LISTEN)));
      
    //   etimer_reset(&energest_tmr);
    // }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
