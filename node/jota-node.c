#include "jota-node.h"

#if UIP_STATISTICS == 1
extern struct uip_stats uip_stat;
#endif

extern struct jota_peer_t *phead;
extern unsigned int __nbr_of_peers;

static struct {
  JOTA_PIECE_BITFIELD_TYPE piece_completed;
  JOTA_PIECE_BITFIELD_TYPE piece_downloading;
} me;

#define I_AM_COMPLETED (me.piece_completed == JOTA_PIECE_COMPLETED_VALUE)

JOTA_PIECE_BITFIELD_TYPE rarest_pieces = 0;

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

uip_ds6_addr_t *my_lladdr;

#if JOTA_INTERMITTENT_OFF
static bool mac_temporarily_off = false;
#endif

static bool mac_permanent_off = false;

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

  // struct jota_peer_t *peer;
  uip_ds6_nbr_t *nbr;
  uip_ipaddr_t ipaddr;

  while(__nbr_of_peers < JOTA_NBR_OF_PEERS && jota_unassigned_peer_dequeue(&ipaddr) > -1 && jota_completed_peer_get(&ipaddr) == NULL)
  {
    jota_insert_peer_to_list(&ipaddr);
    printf("added %u from unassigned peers\n", ipaddr.u8[15]);
  }

  if(__nbr_of_peers < JOTA_NBR_OF_PEERS)
  {
    for(nbr = uip_ds6_nbr_head();
        nbr != NULL;
        nbr = uip_ds6_nbr_next(nbr))
    {
      uip_ipaddr_copy(&ipaddr, &nbr->ipaddr);

      // if(jota_completed_peer_get(ipaddr) != NULL) continue;
      if(jota_get_peer_by_ipaddr(ipaddr) != NULL) continue;
      if(__nbr_of_peers >= JOTA_NBR_OF_PEERS) break;

      if(jota_blacklist_peer_is_exists(ipaddr)) {
        jota_blacklist_peer_remove(ipaddr);
        printf("%u found in blacklist\n", ipaddr.u8[15]);
        continue;
      }

      jota_insert_peer_to_list(ipaddr);
      printf("added %u\n", ipaddr.u8[15]);
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
  rarest_pieces = 0;

  unsigned int avail_peers_count = 0;

  struct jota_peer_t *peer = phead;
  while(peer != NULL)
  {
    if(!peer->peer_choking &&
      !peer->am_interested &&
      peer->downloading_piece_index == -1 &&
      peer->piece_completed != 0 &&
      peer->dstate == JOTA_CONN_STATE_HANDSHAKED)
    {
      masked_possessions |= peer->piece_completed;
      rarest_pieces ^= peer->piece_completed;
      avail_peers_count++;
    }

    if(peer->dstate == JOTA_CONN_STATE_DOWNLOADING) my_uploaders++;

    peer = peer->next;
  }
  if(my_uploaders >= JOTA_MAX_UPLOADERS) return -2;

  if(avail_peers_count == 0) return -3;

  masked_possessions &= ~(me.piece_completed|me.piece_downloading);
  rarest_pieces &= ~(me.piece_completed|me.piece_downloading);

  unsigned int possessed_count = 0;
  for(i = 0; i < JOTA_PIECE_COUNT; i++) {
    if(jota_bitfield_bit_is_set(me.piece_completed, i)) possessed_count++;
  }

  if(rarest_pieces != 0) masked_possessions = rarest_pieces;

  unsigned int unpossessed_count = 0;
  for(i = 0; i < JOTA_PIECE_COUNT; i++) {
    if(jota_bitfield_bit_is_set(masked_possessions, i)) unpossessed_count++;
  }

  if(unpossessed_count == 0) return -4;

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
        peer->dstate == JOTA_CONN_STATE_HANDSHAKED &&
        jota_bitfield_bit_is_set(peer->piece_completed, i))
    {
      peer->am_interested = true;
      peer->downloading_piece_index = i;
      peer->downloading_block_index = 0;
      jota_bitfield_set_bit(&me.piece_downloading, peer->downloading_piece_index);

      avail_peers_count--;
      peer = phead;
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
    if(!cmp_read_u16(&cmp, &packet_type)) return;

    // printf("Received from ");
    // uiplib_ipaddr_print(&UIP_IP_BUF->srcipaddr);
    // printf(" (%u)\n", packet_type);

    struct jota_peer_t *peer = jota_get_peer_by_ipaddr(UIP_IP_BUF->srcipaddr);
    if(peer == NULL)
    {
      if(!jota_unassigned_peer_is_exists(&UIP_IP_BUF->srcipaddr))
        jota_unassigned_peer_enqueue(&UIP_IP_BUF->srcipaddr);
      
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
      if(peer->dstate == JOTA_CONN_STATE_HANDSHAKING) txing_false(peer);

      JOTA_PIECE_BITFIELD_TYPE new_possession = 0;
      if(!cmp_read_u32(&cmp, &new_possession)) return;

      printf("Received HANDSHAKE from %d (%lu)", UIP_IP_BUF->srcipaddr.u8[15], new_possession);

#if JOTA_EARLY_OFF

      uint32_t his_friends_count = 0;
      uint8_t his_friend_ip_suffix;

      // Check whether the peer is ok if we leave
      if(I_AM_COMPLETED && cmp_read_array(&cmp, &his_friends_count) && his_friends_count > 0)
      {
        printf(" ( NBR: ");

        uip_ipaddr_t friend_ip;
        uip_ipaddr_copy(&friend_ip, &peer->ipaddr);

        JOTA_PIECE_BITFIELD_TYPE friend_possession = new_possession;

        int i;
        for(i = 0; i < MIN(his_friends_count, JOTA_NBR_OF_PEERS); i++) {
          if(!cmp_read_u8(&cmp, &his_friend_ip_suffix)) continue;
          printf("%u ", his_friend_ip_suffix);

          friend_ip.u8[15] = his_friend_ip_suffix;

          if(jota_completed_peer_get(friend_ip) != NULL)
          {
            // There is an another completed peer
            peer->ok = true;
            break;
          }
          else
          {
            struct jota_peer_t *friend = jota_get_peer_by_ipaddr(friend_ip);
            if(friend == NULL) continue;

            friend_possession |= friend->piece_completed;

            if(friend_possession == JOTA_PIECE_COMPLETED_VALUE) {
              // His friends can help him if we leave
              peer->ok = true;
              break;
            }
          }
        }
        printf(")");

        if(peer->ok) printf(" (ok)\n");
        else printf("\n");
      }
      else
#endif
        printf("\n");

      peer->dstate = JOTA_CONN_STATE_HANDSHAKED;
      peer->ustate = JOTA_CONN_STATE_HANDSHAKED;

      peer->last_handshaked = clock_time();

      // Check if completed
      if(I_AM_COMPLETED && new_possession == JOTA_PIECE_COMPLETED_VALUE && jota_completed_peer_get(peer->ipaddr) == NULL) {
        jota_completed_peer_new(peer->ipaddr);

        // uiplib_ipaddr_print(&peer->ipaddr);
        // printf(" is added to completed list\n");
      }

      if(new_possession == peer->piece_completed) {
        peer->last_handshaked = clock_time() + (15 * CLOCK_SECOND);
      }

      peer->piece_completed = new_possession;

      // if(peer->piece_completed == 0) peer->num_zero_handshakes++;
      // else peer->num_zero_handshakes = 0;

      // Skip sending ACK to an ACK
      if(packet_type == JT_ACK_HANDSHAKE_MSG) goto finally;

      // Send ACK_HANDSHAKE back
      cmp_buf.idx = 0;
      if(!cmp_write_u16(&cmp, JT_ACK_HANDSHAKE_MSG)) goto finally;
      if(!cmp_write_u32(&cmp, me.piece_completed)) goto finally;

      uint8_t my_friends[JOTA_NBR_OF_PEERS];
      uint32_t my_friends_count = 0;
      struct jota_peer_t *friend = phead;
      while(friend != NULL) {
        if(friend->num_blocks_downloaded > 0)
          my_friends[my_friends_count++] = friend->ipaddr.u8[15];
        friend = friend->next;
      }

      if(!cmp_write_array(&cmp, my_friends_count)) goto finally;

      int i;
      for(i = 0; i < MIN(my_friends_count, JOTA_NBR_OF_PEERS); i++) {
        if(!cmp_write_u8(&cmp, my_friends[i])) continue;
      }

      uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
    }
    else if(packet_type == JT_INTEREST_MSG)
    {
      /*
       * 0 = JT_INTEREST_MSG
       * 1 = Piece Index
       */

      if(!cmp_read_s16(&cmp, &peer->uploading_piece_index)) goto finally;

      // TO-DO: Perhaps combine them to am_choking?
      unsigned int my_downloaders = 0;
      struct jota_peer_t *p = phead;
      while(p != NULL) {
        if(p->ustate == JOTA_CONN_STATE_UPLOADING) my_downloaders++;
        p = p->next;
      }

      bool choke = peer->am_choking;
      int max_downloaders = (I_AM_COMPLETED) ? JOTA_MAX_DOWNLOADERS + JOTA_MAX_UPLOADERS : JOTA_MAX_DOWNLOADERS;
      if(my_downloaders >= max_downloaders) choke = true;

      peer->peer_interested = true;
      peer->am_choking = false;

      printf("Received INTEREST (%d) from %d\n", peer->uploading_piece_index, UIP_IP_BUF->srcipaddr.u8[15]);

      // Send CHOKE back (1 = Choke, 0 = Unchoke)
      cmp_buf.idx = 0;
      if(!cmp_write_u16(&cmp, JT_CHOKE_MSG)) goto finally;
      if(!cmp_write_u8(&cmp, choke)) goto finally;
      
      uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
    }
    else if(packet_type == JT_CHOKE_MSG)
    {
      /*
       * 0 = JT_CHOKE_MSG
       * 1 = 1 = Choked, 0 = Unchoked
       */
      if(peer->dstate == JOTA_CONN_STATE_INTEREST_DECLARING) txing_false(peer);
      
      if(!cmp_read_u8(&cmp, (uint8_t *)&peer->peer_choking)) goto finally;
      peer->dstate = JOTA_CONN_STATE_INTEREST_DECLARED;

      if(peer->peer_choking)
        printf("Received CHOKE (%d) from %d\n", peer->peer_choking, UIP_IP_BUF->srcipaddr.u8[15]);
    }
    else if(packet_type == JT_REQUEST_MSG)
    {
      /*
       * 0 = JT_REQUEST_MSG
       */
      // printf("Received REQUEST (%d) from %d\n", peer->uploading_piece_index, UIP_IP_BUF->srcipaddr.u8[15]);
      peer->ustate = JOTA_CONN_STATE_UPLOADING;
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
      if(peer->dstate == JOTA_CONN_STATE_DOWNLOADING) txing_false(peer);

      int16_t piece_index;
      if(!cmp_read_s16(&cmp, &piece_index)) goto finally;

      int16_t block_index;
      if(!cmp_read_s16(&cmp, &block_index)) goto finally;

      uint8_t piece[JOTA_BLOCK_SIZE];
      uint32_t piece_size = JOTA_BLOCK_SIZE;
      if(!cmp_read_bin(&cmp, piece, &piece_size)) goto finally;

      uint16_t my_crc = crc16_data(piece, JOTA_BLOCK_SIZE, 0);
      uint16_t src_crc;
      if(!cmp_read_u16(&cmp, &src_crc)) goto finally;

      // TO-DO: Verify piece index
      bool integrity = (my_crc == src_crc) && (piece_size == JOTA_BLOCK_SIZE);
      if(!integrity) {
        printf("integrity false\n");
        peer->downloading_block_index--;
        goto finally;
      }

      peer->num_blocks_downloaded++;

      // printf("Received BLOCK (%d (%d)) from %d\n", piece_index, block_index, UIP_IP_BUF->srcipaddr.u8[15]);

      // Completed this PIECE
      if(block_index >= JOTA_BLOCK_COUNT - 1)
      {
        peer->dstate = JOTA_CONN_STATE_HANDSHAKED;
        peer->am_interested = false;
        peer->downloading_piece_index = -1;
        peer->downloading_block_index = 0;

        jota_bitfield_clear_bit(&me.piece_downloading, piece_index);

        // Check if completed
        if(me.piece_completed != JOTA_PIECE_COMPLETED_VALUE)
        {
          jota_bitfield_set_bit(&me.piece_completed, piece_index);

          printf("x["BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN"]x (%d) from %d\n",
            BYTE_TO_BINARY(me.piece_completed), BYTE_TO_BINARY(me.piece_completed >> 8),
            BYTE_TO_BINARY(me.piece_completed >> 16), BYTE_TO_BINARY(me.piece_completed >> 24),
            piece_index, peer->ipaddr.u8[15]
          );

          if(I_AM_COMPLETED)
            printf("The end\n");
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

  static struct etimer energest_tmr;
  etimer_set(&energest_tmr, 30 * CLOCK_SECOND);

  while(1)
  {
    PROCESS_YIELD();

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

    if(mac_permanent_off) continue;
#if JOTA_INTERMITTENT_OFF
    if(mac_temporarily_off) continue;
#endif

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

  static struct jota_peer_t *peer;

  static cmp_ctx_t cmp;
  static struct cmp_buf_t cmp_buf;

  static struct etimer delayed_start_tmr;
  etimer_set(&delayed_start_tmr, 60 * CLOCK_SECOND);
  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&delayed_start_tmr));
    break;
  }

  my_lladdr = uip_ds6_get_link_local(-1);
  printf("My IPv6 Address: ");
  uiplib_ipaddr_print(&my_lladdr->ipaddr);
  printf("\n");

  map_peers_to_neighbors();

  static struct etimer interval_tmr;
  static unsigned int interval_time = 1 * CLOCK_SECOND / 10;
  etimer_set(&interval_tmr, interval_time);

  static struct etimer nbr_tmr;
  etimer_set(&nbr_tmr, JOTA_NBR_REFRESH_TIMEOUT);

#if JOTA_LATE_ON
  static struct etimer late_on_tmr;
#endif

#if JOTA_INTERMITTENT_OFF
  static struct etimer lp_tmr;
  etimer_set(&lp_tmr, JOTA_INTERMITTENT_OFF_TIMEOUT);
#endif

  while(1)
  {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&interval_tmr));
    etimer_reset(&interval_tmr);

    if(etimer_expired(&nbr_tmr)) {
      map_peers_to_neighbors();

#if JOTA_EARLY_OFF
    if(I_AM_COMPLETED && !mac_permanent_off)
    {
      // i'm thinking it is ok to leave
      bool it_is_ok_to_leave = true;

      // prove me wrong
      struct jota_peer_t *friend = phead;
      while(friend != NULL) {
        if(!friend->ok && friend->num_blocks_uploaded > 0) {
          it_is_ok_to_leave = false;
          break;
        }
        friend = friend->next;
      }

      // see?
      if(it_is_ok_to_leave) {
        printf("early off()\n");
        NETSTACK_MAC.off();
        mac_permanent_off = true;
      }
    }
#endif
      etimer_reset(&nbr_tmr);
    }

#if JOTA_LATE_ON
  // Me and everyone around me got no pieces ya know
  if(me.piece_completed == 0)
  {
    if(mac_temporarily_off)
    {
      printf("late on wake\n");
      NETSTACK_MAC.on();
      mac_temporarily_off = false;
      etimer_set(&late_on_tmr, JOTA_INTERMITTENT_OFF_TIMEOUT);
    }
    else
    {
      JOTA_PIECE_BITFIELD_TYPE masked = me.piece_completed;

      struct jota_peer_t *p = phead;
      while(p != NULL) {
        masked ^= p->piece_completed;
        p = p->next;
      }

      if(masked == 0) {
        printf("late on sleep\n");
        NETSTACK_MAC.off();
        mac_temporarily_off = true;
        etimer_set(&late_on_tmr, JOTA_INTERMITTENT_OFF_DURATION);
      }
  }
#endif

    // Me and everyone around me completed all pieces
    // TO-DO: unneccessary loops in jota_is_all_completed()
    // if(I_AM_COMPLETED && jota_is_all_completed() && !mac_permanent_off)
    if(I_AM_COMPLETED && !mac_permanent_off && jota_is_all_completed())
    {
      printf("off()\n");
      NETSTACK_MAC.off();
      mac_permanent_off = true;
    }
    else if(!mac_permanent_off)
    {
#if JOTA_INTERMITTENT_OFF
      if(etimer_expired(&lp_tmr))
      {
        if(mac_temporarily_off)
        {
          printf("wake\n");
          NETSTACK_MAC.on();
          mac_temporarily_off = false;
          etimer_set(&lp_tmr, JOTA_INTERMITTENT_OFF_TIMEOUT);
        }
        else
        {
          unsigned int my_uploaders = 0;
          unsigned int my_downloaders = 0;

          struct jota_peer_t *p = phead;
          while(p != NULL) {
            if(p->ustate == JOTA_CONN_STATE_UPLOADING) my_downloaders++;
            if(p->dstate == JOTA_CONN_STATE_DOWNLOADING) my_uploaders++;
            p = p->next;
          }

          if(my_uploaders == 0 && my_downloaders == 0) {
            int sleep_rand = random_rand() % 100;
            if(sleep_rand < JOTA_INTERMITTENT_OFF_CHANCE) {
              printf("sleep\n");
              NETSTACK_MAC.off();
              mac_temporarily_off = true;
              etimer_set(&lp_tmr, JOTA_INTERMITTENT_OFF_DURATION);
            }
          }
        }
      }
#endif
    }

    if(mac_permanent_off) continue;
#if JOTA_INTERMITTENT_OFF
    if(mac_temporarily_off) continue;
#endif

    // Me completed and won't random pieces and peers
    if(!(I_AM_COMPLETED))
      random_piece_and_peer();

    peer = phead;
    while(peer != NULL)
    {
      /* Completed peers are useless to fellow completed peers */
      if(I_AM_COMPLETED && jota_completed_peer_get(peer->ipaddr) != NULL)
      {
        uint8_t ipsuffix = peer->ipaddr.u8[15];
        
        struct jota_peer_t *next = peer->next;

        if(jota_remove_peer_from_list(peer) != -1);
          printf("removed %u since it is completed\n", ipsuffix);

        peer = next;
        continue;
      }
      else if(peer->num_losses >= JOTA_MAX_LOSSES)
      {
        if(peer->downloading_piece_index > -1) {
          jota_bitfield_clear_bit(&me.piece_downloading, peer->downloading_piece_index);
        }

        // printf("checking ");
        // uiplib_ipaddr_print(&peer->ipaddr);
        // printf(" if it belongs to the blacklist\n");
        if(!jota_blacklist_peer_is_exists(peer->ipaddr))
        {
          jota_blacklist_peer_add(peer->ipaddr);
          printf("%u added to blacklist\n", peer->ipaddr.u8[15]);
        }

        uint8_t ipsuffix = peer->ipaddr.u8[15];

        struct jota_peer_t *next = peer->next;

        if(jota_remove_peer_from_list(peer) != -1);
          printf("removed %u due to too many losses\n", ipsuffix);

        peer = next;
        continue;
      }
      // else if(peer->num_zero_handshakes >= JOTA_MAX_ZERO_HANDSHAKES)
      // {
      //   printf("removed %u due to too many zero handshakes\n", peer->ipaddr.u8[15]);

      //   struct jota_peer_t *next = peer->next;
      //   jota_remove_peer_from_list(peer);
      //   peer = next;
      //   continue;
      // }

      /* Transmission timeout handler */
      if(peer->txing)
      {
        if(clock_time() - peer->last_tx > JOTA_TX_TIMEOUT)
        {
          peer->txing = false;
          peer->num_losses++;

          printf("TX Failed to %d (%u) (%d)\n", peer->ipaddr.u8[15], peer->num_losses, peer->dstate);

          if(peer->dstate == JOTA_CONN_STATE_HANDSHAKING ||
            peer->dstate == JOTA_CONN_STATE_INTEREST_DECLARING ||
            peer->dstate == JOTA_CONN_STATE_DOWNLOADING)
          {
            peer->dstate = JOTA_CONN_STATE_IDLE;
          }

          if(peer->downloading_piece_index > -1) {
            peer->am_interested = false;
            jota_bitfield_clear_bit(&me.piece_downloading, peer->downloading_piece_index);
            peer->downloading_piece_index = -1;
            peer->downloading_block_index = 0;
          }
        }
        goto next;
      }

      // Not Handshaked
      if(peer->dstate == JOTA_CONN_STATE_IDLE)
      {
        if(peer->peer_choking)
        {
          if(peer->last_choked == 0)
            peer->last_choked = clock_time();
          else {
            clock_time_t diff = (clock_time() - peer->last_choked) + 1;
            if(JOTA_CHOKED_TIMEOUT < diff) {
              peer->last_choked = 0;
              peer->peer_choking = false;
            }
          }
          goto next;
        }

        cmp_init(&cmp, &cmp_buf, NULL, NULL, cmp_buf_writer);
        cmp_buf.idx = 0;
        if(!cmp_write_u16(&cmp, JT_HANDSHAKE_MSG)) goto next;
        if(!cmp_write_u32(&cmp, me.piece_completed)) goto next;

        uint8_t my_friends[JOTA_NBR_OF_PEERS];
        uint32_t my_friends_count = 0;
        struct jota_peer_t *friend = phead;
        while(friend != NULL) {
          if(friend->num_blocks_downloaded > 0)
            my_friends[my_friends_count++] = friend->ipaddr.u8[15];
          friend = friend->next;
        }

        if(!cmp_write_array(&cmp, my_friends_count)) goto next;

        int i;
        for(i = 0; i < MIN(my_friends_count, JOTA_NBR_OF_PEERS); i++) {
          if(!cmp_write_u8(&cmp, my_friends[i])) goto next;
        }

        // printf("Sending HANDSHAKE to %d\n", peer->ipaddr.u8[15]);

        peer->dstate = JOTA_CONN_STATE_HANDSHAKING;

        uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
        peer->txing = true;
        peer->last_tx = clock_time();
      }
      // Just Handshaked
      else if(peer->dstate == JOTA_CONN_STATE_HANDSHAKED)
      {
        // Check if we should update their possession bitfield again
        if(peer->piece_completed != JOTA_PIECE_COMPLETED_VALUE &&
          // me.piece_completed != JOTA_PIECE_COMPLETED_VALUE &&
          clock_time() + JOTA_HANDSHAKED_TIMEOUT > peer->last_handshaked)
        {
          peer->dstate = JOTA_CONN_STATE_IDLE;
          goto next;
        }

        if(peer->am_interested && peer->downloading_piece_index != -1)
        {
          printf("Sending INTEREST (%d) to %d\n", peer->downloading_piece_index, peer->ipaddr.u8[15]);

          cmp_init(&cmp, &cmp_buf, NULL, NULL, cmp_buf_writer);
          cmp_buf.idx = 0;
          if(!cmp_write_u16(&cmp, JT_INTEREST_MSG)) goto next;
          if(!cmp_write_s16(&cmp, peer->downloading_piece_index)) goto next;

          peer->dstate = JOTA_CONN_STATE_INTEREST_DECLARING;

          uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
          peer->txing = true;
          peer->last_tx = clock_time();
        }
      }
      // Just Requested
      else if(peer->dstate == JOTA_CONN_STATE_INTEREST_DECLARED)
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
              peer->dstate = JOTA_CONN_STATE_HANDSHAKED;
            }
          }
        }
        else
        {
          cmp_init(&cmp, &cmp_buf, NULL, NULL, cmp_buf_writer);
          cmp_buf.idx = 0;
          if(!cmp_write_u16(&cmp, JT_REQUEST_MSG)) goto next;

          // printf("Sending REQUEST (%d) to %d\n", peer->downloading_piece_index, peer->ipaddr.u8[15]);

          peer->dstate = JOTA_CONN_STATE_DOWNLOADING;

          uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
          peer->txing = true;
          peer->last_tx = clock_time();
        }
      }

      // Uploading
      if(peer->ustate == JOTA_CONN_STATE_UPLOADING)
      {
        // TO-DO: Remove declaring `piece_content`
        uint8_t block_content[JOTA_BLOCK_SIZE];
        memset(block_content, '0' + (peer->uploading_piece_index % 10), JOTA_BLOCK_SIZE);
        uint16_t block_checksum = crc16_data(block_content, JOTA_BLOCK_SIZE, 0);
        
        cmp_buf.idx = 0;
        if(!cmp_write_u16(&cmp, JT_BLOCK_MSG)) goto next;
        if(!cmp_write_s16(&cmp, peer->uploading_piece_index)) goto next;
        if(!cmp_write_s16(&cmp, peer->uploading_block_index)) goto next;
        if(!cmp_write_bin(&cmp, block_content, JOTA_BLOCK_SIZE)) goto next;
        if(!cmp_write_u16(&cmp, block_checksum)) goto next;

        peer->uploading_block_index++;
        peer->num_blocks_uploaded++;

        if(peer->uploading_block_index >= JOTA_BLOCK_COUNT)
        {
          peer->ustate = JOTA_CONN_STATE_HANDSHAKED;
          peer->uploading_piece_index = -1;
          peer->uploading_block_index = 0;
          peer->peer_interested = false;
        }

        uip_udp_packet_send(peer->udp_conn, cmp_buf.buf, cmp_buf.idx);
      }
next:
      peer = peer->next;
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
