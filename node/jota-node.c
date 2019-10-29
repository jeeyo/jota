#include "jota-node.h"

#if UIP_STATISTICS == 1
extern struct uip_stats uip_stat;
#endif

extern struct jota_peer_t *phead;
extern unsigned int __nbr_of_peers;

extern bool downtime;

static struct {
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

/* Bitfield Utilities */
/*---------------------------------------------------------------------------*/
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

bool jota_bitfield_bit_is_set(JOTA_PIECE_BITFIELD_TYPE val, unsigned int bit)
{
  JOTA_PIECE_BITFIELD_TYPE tmp = 1;
  tmp <<= bit;
  return val & tmp;
}
/*---------------------------------------------------------------------------*/
#define JOTA_CMP_BUF_SIZE 64

struct cmp_buf_t {
  uint8_t buf[JOTA_CMP_BUF_SIZE];
  unsigned int idx;
};

static bool cmp_buf_reader(cmp_ctx_t *ctx, void *data, size_t limit)
{
  struct cmp_buf_t *buf = (struct cmp_buf_t *)ctx->buf;
  
  unsigned int bytes_to_read = JOTA_CMP_BUF_SIZE - buf->idx > limit ? limit : JOTA_CMP_BUF_SIZE - buf->idx;
  memcpy(data, buf->buf + buf->idx, bytes_to_read);

  buf->idx += bytes_to_read;
  return true;
}

static size_t cmp_buf_writer(cmp_ctx_t *ctx, const void *data, size_t count)
{
  struct cmp_buf_t *buf = (struct cmp_buf_t *)ctx->buf;

  unsigned int bytes_to_write = JOTA_CMP_BUF_SIZE - buf->idx > count ? count : JOTA_CMP_BUF_SIZE - buf->idx;
  memcpy(buf->buf + buf->idx, data, bytes_to_write);

  buf->idx += bytes_to_write;
  return bytes_to_write;
}
/*---------------------------------------------------------------------------*/
static struct uip_udp_conn *server_conn;

PROCESS(jota_udp_server_process, "JOTA UDP Server Process");
PROCESS(jota_node_process, "JOTA Node Process");
#ifndef JOTA_BORDER_ROUTER
AUTOSTART_PROCESSES(&jota_udp_server_process, &jota_node_process);
#endif /* JOTA_BORDER_ROUTER */
/*---------------------------------------------------------------------------*/
static void
map_peers_to_neighbors()
{
  if(__nbr_of_peers >= JOTA_NBR_OF_PEERS) return;

  struct jota_peer_t *peer;
  uip_ds6_nbr_t *nbr;
  uip_ipaddr_t ipaddr;

  if(me.piece_completed == JOTA_PIECE_COMPLETED_VALUE)
  {
    while(__nbr_of_peers < JOTA_NBR_OF_PEERS && jota_unassigned_peer_dequeue(&ipaddr) > -1 && jota_completed_peer_get(&ipaddr) == NULL)
    {
      jota_insert_peer_to_list(&ipaddr);

      // printf("added ");
      // uiplib_ipaddr_print(&ipaddr);
      // printf("\n");
    }
  }
  else
  {
  //   while(__nbr_of_peers < JOTA_NBR_OF_PEERS)
  //   {
  //     unsigned short suffix = (random_rand() % JOTA_NBR_OF_PEERS) + 1;

  //     // Skip ourselves
  //     if(suffix == node_id) continue;

  //     // // TO-DO: Dirty hack for different IP prefix for different Mote Type
  //     // if(suffix == 1)
  //     // {
  //     //   /* Cooja Root */
  //     //   ipaddr.u16[0] = 0x80fe;
  //     //   ipaddr.u16[1] = 0x0;
  //     //   ipaddr.u16[2] = 0x0;
  //     //   ipaddr.u16[3] = 0x0;
  //     //   ipaddr.u16[4] = 0x102;
  //     //   ipaddr.u16[5] = 0x100;
  //     //   ipaddr.u16[6] = 0x100;
  //     //   ipaddr.u16[7] = 0x100;
  //     // }
  //     // else
  //     // {
  //       /* Z1 Node */
  //       ipaddr.u16[0] = 0x80fe;
  //       ipaddr.u16[1] = 0x0;
  //       ipaddr.u16[2] = 0x0;
  //       ipaddr.u16[3] = 0x0;
  //       ipaddr.u16[4] = 0xcc3;
  //       ipaddr.u16[5] = 0x0;
  //       ipaddr.u16[6] = 0x0;
  //       ipaddr.u8[14] = 0x0;
  //       // ipaddr.u8[15] = 0x4;
  //       ipaddr.u8[15] = suffix;
  //     // }

    for(nbr = uip_ds6_nbr_head();
        nbr != NULL;
        nbr = uip_ds6_nbr_next(nbr))
    {
      // TO-DO: dirty hack to change prefix from `fe80` to `fd00`
      uip_ipaddr_copy(&ipaddr, &nbr->ipaddr);

      peer = jota_get_peer_by_ipaddr(&ipaddr);
      if(peer != NULL) return;
      if(__nbr_of_peers >= JOTA_NBR_OF_PEERS) return;

      jota_insert_peer_to_list(&ipaddr);

      // printf("added ");
      // uiplib_ipaddr_print(&ipaddr);
      // printf("\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
static int
random_piece_and_peer()
{
  if(__nbr_of_peers == 0) return -1;

  unsigned int my_uploaders = 0;

  int i = 0;
  JOTA_PIECE_BITFIELD_TYPE masked_possessions = 0;

  // struct jota_peer_t *avail_peers[JOTA_NBR_OF_PEERS];
  unsigned int avail_peers_count = 0;

  struct jota_peer_t *peer = phead;
  while(peer != NULL) {

    if(!peer->peer_choking &&
      !peer->am_interested &&
      peer->downloading_piece_index == -1 &&
      peer->piece_completed != 0 &&
      peer->state == JOTA_CONN_STATE_HANDSHAKED)
    {
      masked_possessions |= peer->piece_completed;
      // avail_peers[avail_peers_count++] = peer;
      avail_peers_count++;
    }

    if(peer->am_interested) my_uploaders++;

    peer = peer->next;
  }
  if(my_uploaders >= JOTA_MAX_UPLOADERS) return -2;

  // printf("Uploaders %d\n", my_uploaders);

  if(avail_peers_count == 0) return -3;

  masked_possessions &= ~(me.piece_completed|me.piece_downloading);

  unsigned int unpossessed_count = 0;
  for(i = 0; i < JOTA_PIECE_COUNT; i++) {
    if(jota_bitfield_bit_is_set(masked_possessions, i)) unpossessed_count++;
  }
  
  if(unpossessed_count == 0) return -4;

  // // random peer
  // n_rand = random_rand() % avail_peers_count;
  // peer = avail_peers[n_rand];

  peer = phead;
  while(peer != NULL && avail_peers_count > 0)
  {
    unsigned short n_rand = (random_rand() % unpossessed_count) + 1;

    // find next 'randomed' unpossessed
    for(i = 0; i < JOTA_PIECE_COUNT; i++) {
      if(jota_bitfield_bit_is_set(masked_possessions, i)) n_rand--;
      if(n_rand <= 0) break;
    }

    if(i >= JOTA_PIECE_COUNT) return -5;

    if(!peer->peer_choking &&
        !peer->am_interested &&
        peer->downloading_piece_index == -1 &&
        peer->state == JOTA_CONN_STATE_HANDSHAKED &&
        jota_bitfield_bit_is_set(peer->piece_completed, i) &&
        !jota_bitfield_bit_is_set(me.piece_completed, i) &&
        !jota_bitfield_bit_is_set(me.piece_downloading, i))
    {
      peer->am_interested = true;
      peer->downloading_piece_index = i;
      peer->downloading_block_index = 0;
      jota_bitfield_set_bit(&me.piece_downloading, peer->downloading_piece_index);

      avail_peers_count--;
      peer = phead;
      continue;
    }
    peer = peer->next;
  }

  return i;
}
/*---------------------------------------------------------------------------*/
void txing_false(struct jota_peer_t *peer)
{
  peer->last_rx = clock_time();
  peer->txing = false;
  peer->num_losses = 0;
}

static void
tcpip_handler(void)
{
  if(uip_newdata())
  {
    cmp_ctx_t cmp;
    struct cmp_buf_t cmp_buf;
    cmp_buf.idx = 0;

    unsigned int bytes_to_read = uip_datalen() > JOTA_CMP_BUF_SIZE ? JOTA_CMP_BUF_SIZE : uip_datalen();
    memcpy(cmp_buf.buf, uip_appdata, bytes_to_read);

    uint16_t packet_type = -1;

    cmp_init(&cmp, &cmp_buf, cmp_buf_reader, NULL, cmp_buf_writer);
    if(!cmp_read_u16(&cmp, &packet_type)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

    // printf("Received from ");
    // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
    // printf(" (%u)\n", packet_type);

    struct jota_peer_t *peer = jota_get_peer_by_ipaddr(&UIP_IP_BUF->srcipaddr);
    if(peer == NULL)
    {
      // printf("undefined peer (");
      // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      // printf(")\n");
      
      // if(UIP_IP_BUF->srcipaddr.u16[0] == 253 &&
      //     UIP_IP_BUF->srcipaddr.u16[1] == 0 &&
      //     UIP_IP_BUF->srcipaddr.u16[2] == 0 &&
      //     UIP_IP_BUF->srcipaddr.u16[3] == 0)
      // {
        if(!jota_unassigned_peer_is_exists(&UIP_IP_BUF->srcipaddr))
          jota_unassigned_peer_enqueue(&UIP_IP_BUF->srcipaddr);
      // }
      
      goto finally;
    }

    uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
    server_conn->rport = UIP_UDP_BUF->srcport;

    if(packet_type == JT_HANDSHAKE_MSG || packet_type == JT_ACK_HANDSHAKE_MSG)
    {
      /*
       * 0 = JT_HANDSHAKE_MSG
       * 1 = Bitfield of possession
       */
      if(peer->state == JOTA_CONN_STATE_HANDSHAKING) txing_false(peer);

      if(!cmp_read_u32(&cmp, &peer->piece_completed)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      peer->state = JOTA_CONN_STATE_HANDSHAKED;
      peer->last_handshaked = clock_time();
      
      // printf("Received HANDSHAKE from ");
      // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      // printf(" (%lu)\n", peer->piece_completed);

      // Check if completed
      if(me.piece_completed == JOTA_PIECE_COMPLETED_VALUE && peer->piece_completed == JOTA_PIECE_COMPLETED_VALUE && jota_completed_peer_get(&peer->ipaddr) == NULL) {
        jota_completed_peer_new(&peer->ipaddr);

        // uiplib_ipaddr_print(&peer->ipaddr);
        // printf(" is added to completed list\n");
      }

      // Skip sending ACK to an ACK
      if(packet_type == JT_ACK_HANDSHAKE_MSG) goto finally;

      // Send ACK_HANDSHAKE back
      cmp_buf.idx = 0;
      if(!cmp_write_u16(&cmp, JT_ACK_HANDSHAKE_MSG)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      if(!cmp_write_u32(&cmp, me.piece_completed)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

      uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
    }
    else if(packet_type == JT_INTEREST_MSG)
    {
      /*
       * 0 = JT_INTEREST_MSG
       * 1 = Piece Index
       */
      if(peer->state == JOTA_CONN_STATE_HANDSHAKED) txing_false(peer);

      if(!cmp_read_s16(&cmp, &peer->uploading_piece_index)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

      // TO-DO: Perhaps combine them to am_choking?
      unsigned int my_downloaders = 0;
      struct jota_peer_t *p = phead;
      while(p != NULL) {
        if(p->peer_interested && !p->am_choking) my_downloaders++;
        p = p->next;
      }

      int max_downloaders = (me.piece_completed == JOTA_PIECE_COMPLETED_VALUE) ? JOTA_MAX_DOWNLOADERS + JOTA_MAX_UPLOADERS : JOTA_MAX_DOWNLOADERS;
      if(my_downloaders < max_downloaders) peer->am_choking = false;

      peer->peer_interested = true;

      // printf("Received INTEREST (%d) from ", peer->uploading_piece_index);
      // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      // printf("\n");

      // Send CHOKE back (1 = Choke, 0 = Unchoke)
      cmp_buf.idx = 0;
      if(!cmp_write_u16(&cmp, JT_CHOKE_MSG)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      if(!cmp_write_u8(&cmp, peer->am_choking)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      
      uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
    }
    else if(packet_type == JT_CHOKE_MSG)
    {
      /*
       * 0 = JT_CHOKE_MSG
       * 1 = 1 = Choked, 0 = Unchoked
       */
      if(peer->state == JOTA_CONN_STATE_INTEREST_DECLARING) txing_false(peer);
      
      if(!cmp_read_u8(&cmp, (uint8_t *)&peer->peer_choking)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      peer->state = JOTA_CONN_STATE_INTEREST_DECLARED;

      // if(peer->peer_choking) {
      //   printf("Choked by ");
      //   uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      //   printf("\n");
      // }

      // printf("Received CHOKE (%d) from ", peer->peer_choking);
      // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      // printf("\n");
    }
    else if(packet_type == JT_REQUEST_MSG)
    {
      /*
       * 0 = JT_REQUEST_MSG
       * 1 = Block Index
       */
      int16_t uploading_block_index;
      if(!cmp_read_s16(&cmp, &uploading_block_index)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      
      // printf("Received REQUEST (%d (%d)) from ", peer->uploading_piece_index, uploading_block_index);
      // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      // printf("\n");
      
      // TO-DO: Remove declaring `piece_content`
      uint8_t block_content[JOTA_BLOCK_SIZE];
      memset(block_content, '0' + (peer->uploading_piece_index % 10), JOTA_BLOCK_SIZE);
      uint16_t block_checksum = crc16_data(block_content, JOTA_BLOCK_SIZE, 0);
      
      cmp_buf.idx = 0;
      if(!cmp_write_u16(&cmp, JT_BLOCK_MSG)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      if(!cmp_write_s16(&cmp, peer->uploading_piece_index)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      if(!cmp_write_s16(&cmp, uploading_block_index)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      if(!cmp_write_bin(&cmp, block_content, JOTA_BLOCK_SIZE)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
      if(!cmp_write_u16(&cmp, block_checksum))  printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

      peer->num_blocks_uploaded++;
      
      uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
      peer->txing = true;
      peer->last_tx = clock_time();
    }
    else if(packet_type == JT_BLOCK_MSG)
    {
      /*
       * 0 = JT_BLOCK_MSG
       * 1 = Piece Index
       * 2 = Block Index
       * 2 = Block Binary
       * 3 = Checksum
       */
      if(peer->state == JOTA_CONN_STATE_DOWNLOADING) txing_false(peer);

      int16_t piece_index;
      if(!cmp_read_s16(&cmp, &piece_index)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

      int16_t block_index;
      if(!cmp_read_s16(&cmp, &block_index)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

      uint8_t piece[JOTA_BLOCK_SIZE];
      uint32_t piece_size = JOTA_BLOCK_SIZE;
      if(!cmp_read_bin(&cmp, piece, &piece_size)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

      uint16_t my_crc = crc16_data(piece, JOTA_BLOCK_SIZE, 0);
      uint16_t src_crc;
      if(!cmp_read_u16(&cmp, &src_crc))  printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

      // TO-DO: Verify piece index
      bool integrity = (my_crc == src_crc) && (piece_size == JOTA_BLOCK_SIZE);
      if(!integrity) {
        printf("integrity false\n");
        peer->downloading_block_index--;
        goto finally;
      }

      // printf("Received BLOCK (%d (%d)) from ", piece_index, block_index);
      // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      // printf("\n");

      peer->state = JOTA_CONN_STATE_INTEREST_DECLARED;

      // Completed this PIECE
      if(block_index >= JOTA_BLOCK_COUNT - 1)
      {
        jota_bitfield_set_bit(&me.piece_completed, piece_index);
        jota_bitfield_clear_bit(&me.piece_downloading, piece_index);

        printf("x["BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN"]x\n",
          BYTE_TO_BINARY(me.piece_completed), BYTE_TO_BINARY(me.piece_completed >> 8),
          BYTE_TO_BINARY(me.piece_completed >> 16), BYTE_TO_BINARY(me.piece_completed >> 24)
        );

        peer->state = JOTA_CONN_STATE_HANDSHAKED;
        peer->am_interested = false;
        peer->downloading_piece_index = -1;
        peer->downloading_block_index = 0;

        // Check if completed
        if(me.piece_completed == JOTA_PIECE_COMPLETED_VALUE)
        {
          printf("The end\n");
  // #if UIP_STATISTICS == 1
  //         printf("*** The end [dropped %d, forwarded %d]\n", uip_stat.ip.drop, uip_stat.ip.forwarded);
  // #else
  //         printf("*** The end\n");
  // #endif
        }
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

    if(downtime) continue;

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

  static cmp_ctx_t cmp;
  static struct cmp_buf_t cmp_buf;

  static struct etimer delayed_start_tmr;
  etimer_set(&delayed_start_tmr, 60 * CLOCK_SECOND);
  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&delayed_start_tmr));
    break;
  }
  
  map_peers_to_neighbors();

  // etimer_set(&delayed_start_tmr, (node_id * 10) * CLOCK_SECOND);
  // while(1)
  // {
  //   PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&delayed_start_tmr));
  //   break;
  // }

  static struct etimer interval_tmr;
  static unsigned int interval_time = 1;
  etimer_set(&interval_tmr, interval_time * CLOCK_SECOND);

#ifdef JOTA_SIMULATE_DOWNTIME
  static struct etimer downtime_tmr;
  etimer_set(&downtime_tmr, JOTA_DOWNTIME_TIMEOUT);
#endif

  static struct etimer nbr_tmr;
  etimer_set(&nbr_tmr, 10 * CLOCK_SECOND);

  static struct etimer optimistic_unchoke_tmr;
  etimer_set(&optimistic_unchoke_tmr, JOTA_OPTIMISTIC_UNCHOKE_TIMEOUT);

  static struct etimer energest_tmr;
  etimer_set(&energest_tmr, 30 * CLOCK_SECOND);

  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&interval_tmr));
    etimer_set(&interval_tmr, interval_time * CLOCK_SECOND);

#ifdef JOTA_SIMULATE_DOWNTIME
    if(etimer_expired(&downtime_tmr))
    {
      downtime = false;
      etimer_set(&downtime_tmr, JOTA_DOWNTIME_TIMEOUT);

      int downtime_rand = random_rand() % 100;
      if(downtime_rand < JOTA_DOWNTIME_CHANCE_PERCENTAGE) {
        printf("downtime\n");
        downtime = true;
        etimer_set(&downtime_tmr, JOTA_DOWNTIME_DURATION);
      }
    }
#endif

    if(etimer_expired(&nbr_tmr)) {
      map_peers_to_neighbors();
      etimer_reset(&nbr_tmr);
    }

    if(etimer_expired(&optimistic_unchoke_tmr)) {
      
      unsigned int max_blocks_uploaded = 0;
      struct jota_peer_t *top_downloader = NULL;

      struct jota_peer_t *lucky_men[JOTA_NBR_OF_PEERS];
      unsigned int lucky_men_count = 0;

      peer = phead;
      while(peer != NULL)
      {
        if(peer->num_blocks_uploaded > max_blocks_uploaded) {
          max_blocks_uploaded = peer->num_blocks_uploaded;
          top_downloader = peer;
        }

        if(peer->peer_interested && peer->am_choking) {
          lucky_men[lucky_men_count++] = peer;
        }

        peer = peer->next;
      }

      if(top_downloader != NULL) {
        top_downloader->num_blocks_uploaded = 0;
        top_downloader->am_choking = true;

        if(lucky_men_count > 0) {
          peer = lucky_men[random_rand() % lucky_men_count];
          peer->am_choking = false;
        }
      }

      etimer_reset(&optimistic_unchoke_tmr);
    }

    if(etimer_expired(&energest_tmr))
    {
      energest_flush();

      printf("*** CPU [RUN %lluus / LPM %lluus / DEEP LPM %lluus / Total time %lluus] Radio [LISTEN %lluus / TRANSMIT %lluus / OFF %lluus]\n",
              energest_type_time(ENERGEST_TYPE_CPU),
              energest_type_time(ENERGEST_TYPE_LPM),
              energest_type_time(ENERGEST_TYPE_DEEP_LPM),
              ENERGEST_GET_TOTAL_TIME(),
              energest_type_time(ENERGEST_TYPE_LISTEN),
              energest_type_time(ENERGEST_TYPE_TRANSMIT),
              ENERGEST_GET_TOTAL_TIME()
                        - energest_type_time(ENERGEST_TYPE_TRANSMIT)
                        - energest_type_time(ENERGEST_TYPE_LISTEN));

      etimer_reset(&energest_tmr);
    }

    if(downtime) continue;

    // Me completed and won't random pieces and peers
    if(me.piece_completed != JOTA_PIECE_COMPLETED_VALUE)
    {
      // int rpp = random_piece_and_peer();
      // if(rpp < 1)
      //   printf("random_piece_and_peer() returned %d\n", rpp);
      random_piece_and_peer();
    }

    peer = phead;
    while(peer != NULL)
    {
      /* Completed peers are useless to fellow completed peers */
      if(me.piece_completed == JOTA_PIECE_COMPLETED_VALUE && jota_completed_peer_get(&peer->ipaddr) != NULL)
      {
        // printf("removed ");
        // uiplib_ipaddr_print(&peer->ipaddr);
        // printf(" since it is completed\n");
        
        struct jota_peer_t *next = peer->next;
        jota_remove_peer_from_list(peer);
        peer = next;
        continue;
      }
      else if(peer->num_losses >= JOTA_MAX_LOSSES)
      {
        // printf("removed ");
        // uiplib_ipaddr_print(&peer->ipaddr);
        // printf(" due to too many losses\n");

        if(peer->downloading_piece_index > -1) {
          jota_bitfield_clear_bit(&me.piece_downloading, peer->downloading_piece_index);
        }
        
        struct jota_peer_t *next = peer->next;
        jota_remove_peer_from_list(peer);
        peer = next;
        continue;
      }

      /* Transmission timeout handler */
      if(peer->txing)
      {
        clock_time_t diff = (clock_time() - peer->last_tx) + 1;
        if(JOTA_TX_TIMEOUT < diff)
        {
          peer->txing = false;
          peer->num_losses++;
          
          // printf("TX Failed (");
          // uiplib_ipaddr_print(&peer->ipaddr);
          // printf(") (%u) (%d)\n", peer->num_losses, peer->state);

          if(peer->state == JOTA_CONN_STATE_HANDSHAKING)
          {
            peer->state = JOTA_CONN_STATE_IDLE;
          }
          else if(peer->state == JOTA_CONN_STATE_INTEREST_DECLARING)
          {
            peer->peer_choking = 0;
            peer->am_interested = false;
            jota_bitfield_clear_bit(&me.piece_downloading, peer->downloading_piece_index);
            peer->downloading_piece_index = -1;
          }
          else if(peer->state == JOTA_CONN_STATE_DOWNLOADING)
          {
            peer->downloading_block_index--;
            peer->state = JOTA_CONN_STATE_INTEREST_DECLARED;
          }
        }
        goto next;
      }

      // Not Handshaked
      if(peer->state == JOTA_CONN_STATE_IDLE)
      {
        if(peer->peer_choking)
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

        cmp_init(&cmp, &cmp_buf, NULL, NULL, cmp_buf_writer);
        cmp_buf.idx = 0;
        if(!cmp_write_u16(&cmp, JT_HANDSHAKE_MSG)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
        if(!cmp_write_u32(&cmp, me.piece_completed)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

        // printf("Sending HANDSHAKE to ");
        // uiplib_ipaddr_print(&peer->ipaddr);
        // printf("\n");

        peer->state = JOTA_CONN_STATE_HANDSHAKING;

        uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
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
          goto next;
        }

        if(peer->am_interested && peer->downloading_piece_index != -1)
        {
          // printf("Sending INTEREST (%d) to ", peer->downloading_piece_index);
          // uiplib_ipaddr_print(&peer->ipaddr);
          // printf("\n");

          cmp_init(&cmp, &cmp_buf, NULL, NULL, cmp_buf_writer);
          cmp_buf.idx = 0;
          if(!cmp_write_u16(&cmp, JT_INTEREST_MSG)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
          if(!cmp_write_s16(&cmp, peer->downloading_piece_index)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

          peer->state = JOTA_CONN_STATE_INTEREST_DECLARING;

          uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
          peer->txing = true;
          peer->last_tx = clock_time();
        }
      }
      // Just Requested
      else if(peer->state == JOTA_CONN_STATE_INTEREST_DECLARED)
      {
        if(peer->peer_choking)
        {
          if(peer->last_choked == 0)
            peer->last_choked = clock_time();
          else
          {
            clock_time_t diff = (clock_time() - peer->last_choked) + 1;
            if(JOTA_CHOKED_TIMEOUT < diff) {
              peer->last_choked = 0;
              peer->peer_choking = 0;
              peer->am_interested = false;
              jota_bitfield_clear_bit(&me.piece_downloading, peer->downloading_piece_index);
              peer->downloading_piece_index = -1;
              peer->state = JOTA_CONN_STATE_HANDSHAKED;
            }
          }
        }
        else
        {
          if(peer->downloading_block_index < JOTA_BLOCK_COUNT)
          {
            // printf("Sending REQUEST (%d (%d)) to ", peer->downloading_piece_index, peer->downloading_block_index);
            // uiplib_ipaddr_print(&peer->ipaddr);
            // printf("\n");
            
            cmp_init(&cmp, &cmp_buf, NULL, NULL, cmp_buf_writer);
            cmp_buf.idx = 0;
            if(!cmp_write_u16(&cmp, JT_REQUEST_MSG)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);
            if(!cmp_write_s16(&cmp, peer->downloading_block_index++)) printf("%s (%d)", cmp_strerror(&cmp), __LINE__);

            peer->state = JOTA_CONN_STATE_DOWNLOADING;

            uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
            peer->txing = true;
            peer->last_tx = clock_time();
          }
        }
      }
next:
      peer = peer->next;
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
