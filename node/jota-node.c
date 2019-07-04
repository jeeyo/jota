#include "jota-node.h"

#include "net/ipv6/tcpip.h"

extern struct jota_peer_t peers[JOTA_MAX_PEERS];

static struct {
  uint8_t piece_completed[JOTA_PIECE_COUNT];
  uint8_t piece_downloading[JOTA_PIECE_COUNT];
} me;

static struct uip_udp_conn *server_conn;
// static int __connected_sockets = 0;

PROCESS(jota_udp_server_process, "JOTA UDP Server Process");
PROCESS(jota_node_process, "JOTA Node Process");
#ifndef JOTA_BORDER_ROUTER
AUTOSTART_PROCESSES(&jota_udp_server_process, &jota_node_process);
// AUTOSTART_PROCESSES(&jota_node_process);
#endif /* JOTA_BORDER_ROUTER */
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

    char serialized[uip_datalen() + 1];
    memcpy(serialized, uip_appdata, uip_datalen());
    serialized[uip_datalen()] = '\0';

    struct jota_peer_t *peer = jota_get_peer_by_ipaddr(&UIP_IP_BUF->srcipaddr);
    if(peer == NULL) {
      printf("NULL peer for ");
      uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("\n");
      goto finally;
    }

    printf("Received '%.*s' from ", uip_datalen(), (char *)uip_appdata);
    uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
    printf("\n");

    uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
    server_conn->rport = UIP_UDP_BUF->srcport;

    if(memcmp(serialized, JT_HANDSHAKE_MSG, strlen(JT_HANDSHAKE_MSG)) == 0)
    {
      char result[2][JT_SERIALIZE_RESULT_LEN];
      jota_deserialize(serialized, result, 2);

      memcpy(&peer->piece_completed, result[1], JOTA_PIECE_COUNT);

      uint8_t mbuf[strlen(JT_HANDSHAKE_MSG) + JOTA_PIECE_COUNT + 1];
      size_t mbuflen = sprintf((char *)mbuf, "%s:%.*s", JT_HANDSHAKE_MSG, JOTA_PIECE_COUNT, &me.piece_completed[0]);

      uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
      peer->flags |= JOTA_PEER_FLAG_HANDSHAKED;
      peer->state = JOTA_CONN_STATE_IDLE;
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
  etimer_set(&delayed_start_tmr, ((120 + (node_id * 10)) * CLOCK_SECOND));

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && data == &delayed_start_tmr);
    break;
  }

  while(1)
  {
    PROCESS_PAUSE();

    for(i = 0; i < JOTA_NBR_OF_PEERS; i++)
    { 
      if(i == node_id - 1) continue;
      
      struct jota_peer_t *peer = &peers[i];

      // Not Handshaked
      if(!(peer->flags & JOTA_PEER_FLAG_HANDSHAKED))
      {
        uint8_t mbuf[strlen(JT_HANDSHAKE_MSG) + JOTA_PIECE_COUNT + 1];
        size_t mbuflen = sprintf((char *)mbuf, "%s:%.*s", JT_HANDSHAKE_MSG, JOTA_PIECE_COUNT, &me.piece_completed[0]);

        uip_udp_packet_send(peer->udp_conn, mbuf, mbuflen);
        peer->state = JOTA_CONN_STATE_TXING;
        peer->last_tx = clock_time();
      }
      // Not Downloading From
      else if(!(peer->flags & JOTA_PEER_FLAG_DOWNLOADING_FROM))
      {
        // Select a piece here
      }
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
