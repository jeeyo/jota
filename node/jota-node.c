#include "jota-node.h"

#include "net/ipv6/tcpip.h"

extern struct jota_peer_t peers[JOTA_MAX_PEERS];

static struct {
  uint8_t piece_completed[JOTA_PIECE_COUNT];
  uint8_t piece_downloading[JOTA_PIECE_COUNT];
} me;

#define JOTA_MAX_UPLOADERS 5
#define JOTA_MAX_DOWNLOADERS 5

static struct uip_udp_conn *server_conn;
static int __nbr_of_my_uploaders = 0;
// static int __nbr_of_my_downloaders = 0;

PROCESS(jota_udp_server_process, "JOTA UDP Server Process");
PROCESS(jota_node_process, "JOTA Node Process");
#ifndef JOTA_BORDER_ROUTER
AUTOSTART_PROCESSES(&jota_udp_server_process, &jota_node_process);
// AUTOSTART_PROCESSES(&jota_node_process);
#endif /* JOTA_BORDER_ROUTER */
/*---------------------------------------------------------------------------*/
static int
random_unpossessed_piece_from_peer(struct jota_peer_t *peer)
{
  int piece_index = -1;
  int i = 5;

  while(i--)
  {
    srand(clock());
    piece_index = rand() % JOTA_PIECE_COUNT;

    if(peer->piece_completed[piece_index] == '1' &&
      me.piece_completed[piece_index] == '0' &&
      me.piece_downloading[piece_index] == '0')
    {
      printf("found piece index %d [", piece_index);
      uiplib_ipaddr_print(&peer->ipaddr);
      printf("]\n");
      return piece_index;
    }
  }
  return -1;
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

    printf("Received '%.*s' [", uip_datalen(), (char *)uip_appdata);
    uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
    printf("]\n");

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
      memcpy(&peer->piece_completed[0], &result[1][0], JOTA_PIECE_COUNT);
      peer->state = JOTA_CONN_STATE_HANDSHAKED;

      // Skip sending ACK to an ACK
      if(memcmp(serialized, JT_ACK_HANDSHAKE_MSG, strlen(JT_ACK_HANDSHAKE_MSG)) == 0) goto finally;

      printf("HANDSHAKE from ");
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("\n");

      // Send ACK_HANDSHAKE back
      uint8_t mbuf[strlen(JT_ACK_HANDSHAKE_MSG) + JOTA_PIECE_COUNT + 1];
      size_t mbuflen = sprintf((char *)mbuf, "%s:%.*s", JT_ACK_HANDSHAKE_MSG, JOTA_PIECE_COUNT, &me.piece_completed[0]);
      
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
      // int choking = peer->am_choking && (__nbr_of_my_downloaders >= JOTA_MAX_DOWNLOADERS);
      int choking = peer->am_choking;

      printf("INTEREST (%d) from ", peer->uploading_piece_index);
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("\n");

      // Send CHOKE back (1 = Choke, 0 = Unchoke)
      uint8_t mbuf[strlen(JT_CHOKE_MSG) + 1 + 1 + 1];
      size_t mbuflen = sprintf((char *)mbuf, "%s:%d", JT_CHOKE_MSG, choking);

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

      printf("CHOKE (%d) from ", peer->peer_choking);
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("\n");
    }
    else if(memcmp(serialized, JT_REQUEST_MSG, strlen(JT_REQUEST_MSG)) == 0)
    {
      printf("REQUEST from ");
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("\n");

      /*
       * 0 = JT_REQUEST_MSG
       */
      
      // TO-DO: Remove declaring `piece_content`
      uint8_t piece_content[JOTA_PIECE_SIZE];
      memset(piece_content, '0' + (peer->uploading_piece_index % 10), JOTA_PIECE_SIZE);
      unsigned short piece_checksum = crc16_data(piece_content, JOTA_PIECE_SIZE, 0);

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

      me.piece_downloading[piece_index] = '0';

      // TO-DO: Verify piece index
      bool integrity = my_crc == src_crc;
      if(integrity)
        me.piece_completed[piece_index] = '1';

      printf("PIECE from ");
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf(" [%.*s]\n", JOTA_PIECE_COUNT, me.piece_completed);

      peer->state = JOTA_CONN_STATE_HANDSHAKED;

      // Check if completed
      if(memchr((const char *)&me.piece_completed[0], '0', JOTA_PIECE_COUNT) == NULL)
        printf("Download completed\n");
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
  memset(&me.piece_completed[0], '0', JOTA_PIECE_COUNT);
  memset(&me.piece_downloading[0], '0', JOTA_PIECE_COUNT);
#else
  memset(&me.piece_completed[0], '1', JOTA_PIECE_COUNT);
  memset(&me.piece_downloading[0], '0', JOTA_PIECE_COUNT);
#endif

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

  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && data == &delayed_start_tmr);
    break;
  }

  static struct etimer choke_tmr;
  etimer_set(&choke_tmr, 60 * CLOCK_SECOND);

  while(1)
  {
    // Completed downloading, won't transmit anymore
    if(memchr((const char *)&me.piece_completed[0], '0', JOTA_PIECE_COUNT) == NULL) {
      PROCESS_PAUSE();
      goto choke;
    }

    for(i = 0; i < JOTA_NBR_OF_PEERS; i++)
    {
      PROCESS_PAUSE();
      
      // Skip for ourselves
      if(i == node_id - 1) continue;
      
      struct jota_peer_t *peer = &peers[i];

      // Transmission timeout handler
      if(peer->txing == true)
      {
        clock_time_t diff = (clock_time() - peer->last_tx) + 1;
        if(JOTA_TX_TIMEOUT < diff) {
          
          printf("No responses in time, retransmitting [");
          uiplib_ipaddr_print(&peer->ipaddr);
          printf("]\n");
          
          // TO-DO: May need verification
          if(peer->state > 0)
            peer->state -= 1;
          peer->txing = false;
        }
        continue;
      }

      // Not Handshaked
      if(peer->state == JOTA_CONN_STATE_IDLE)
      {
        uint8_t mbuf[strlen(JT_HANDSHAKE_MSG) + 1 + JOTA_PIECE_COUNT + 1];
        size_t mbuflen = sprintf((char *)mbuf, "%s:%.*s", JT_HANDSHAKE_MSG, JOTA_PIECE_COUNT, &me.piece_completed[0]);

        uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
        peer->txing = true;
        peer->last_tx = clock_time();
      }
      // Just Handshaked
      else if(peer->state == JOTA_CONN_STATE_HANDSHAKED)
      {
        // Download slot available
        // if(__nbr_of_my_uploaders < JOTA_MAX_UPLOADERS)
        // {
          peer->downloading_piece_index = random_unpossessed_piece_from_peer(peer);
          if(peer->downloading_piece_index > -1)
          {
            __nbr_of_my_uploaders++;

            peer->am_interested = true;
            
            uint8_t mbuf[strlen(JT_INTEREST_MSG) + 1 + 2 + 1];
            size_t mbuflen = sprintf((char *)mbuf, "%s:%02d", JT_INTEREST_MSG, peer->downloading_piece_index);

            uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
            peer->txing = true;
            peer->last_tx = clock_time();
          }
        // }
      }
      // Just Requested
      else if(peer->state == JOTA_CONN_STATE_INTERESTING)
      {
        if(peer->peer_choking == 0)
        {
          // TO-DO: Start download here
          me.piece_downloading[peer->downloading_piece_index] = '1';
          uint8_t mbuf[strlen(JT_REQUEST_MSG) + 1];
          size_t mbuflen = sprintf((char *)mbuf, "%s", JT_REQUEST_MSG);

          uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
          peer->txing = true;
          peer->last_tx = clock_time();
        }
        else
        {
          // TO-DO: Handle being choked, maybe a timer here?
          __nbr_of_my_uploaders--;

          if(peer->last_choked == 0)
            peer->last_choked = clock_time();
          else {
            clock_time_t diff = (clock_time() - peer->last_choked) + 1;
            if(JOTA_CHOKED_TIMEOUT < diff) {
              peer->last_choked = 0;
              peer->state = JOTA_CONN_STATE_HANDSHAKED;
            }
          }
        }
      }
    }

choke:
    // Perform choking algorithm periodically
    if(ev == PROCESS_EVENT_TIMER && data == &choke_tmr)
    {
      // TO-DO
      etimer_reset(&choke_tmr);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
