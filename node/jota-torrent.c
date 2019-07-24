#include "jota-torrent.h"

struct jota_peer_t peers[JOTA_NBR_OF_PEERS];

void jota_peers_init()
{
  int i;
  for(i = 0; i < JOTA_NBR_OF_PEERS; i++)
  {
    struct jota_peer_t *peer = &peers[i];
    jota_reset_peer(peer);

    // Pre-assign IP Address
    uip_ipaddr_t *ipaddr = &peer->ipaddr;
    ipaddr->u16[0] = 253;
    ipaddr->u16[1] = 0;
    ipaddr->u16[2] = 0;
    ipaddr->u16[3] = 0;
    ipaddr->u16[4] = 258;
    ipaddr->u16[5] = 256;
    ipaddr->u16[6] = 256;
    ipaddr->u16[7] = 256;

    ipaddr->u8[9] = i + 1;
    ipaddr->u8[11] = i + 1;
    ipaddr->u8[13] = i + 1;
    ipaddr->u8[15] = i + 1;

    peer->udp_conn = udp_new(ipaddr, UIP_HTONS(JOTA_CONN_PORT), NULL);
  }
}

struct jota_peer_t *jota_get_peer_by_ipaddr(uip_ipaddr_t *ipaddr)
{
  int i;
  for(i = 0; i < JOTA_NBR_OF_PEERS; i++)
  {
    struct jota_peer_t *peer = &peers[i];
    if(uip_ipaddr_cmp(ipaddr, &peer->ipaddr)) return peer;
  };
  return NULL;
}

// struct jota_peer_t *jota_get_peer_by_socket(struct jota_tcp_socket_t *s)
// {  
//   int i;
//   for(i = 0; i < JOTA_NBR_OF_PEERS; i++)
//   {
//     struct jota_peer_t *peer = &peers[i];
//     if(peer->socket == s) return peer;
//   }
//   return NULL;
// }

// struct jota_peer_t *jota_get_available_peer()
// {
//   int i;
//   for(i = 0; i < JOTA_NBR_OF_PEERS; i++)
//   {
//     struct jota_peer_t *peer = &peers[i];
//     if(peer->socket == NULL) return peer;
//   }
//   return NULL;
// }

// struct jota_peer_t *jota_get_peer_or_create_by_socket(struct jota_tcp_socket_t *s)
// {
//   struct jota_peer_t *p = jota_get_peer_by_socket(s);
//   if(p != NULL) return p;

//   p = jota_get_available_peer();
//   if(p != NULL) p->socket = s;

//   return p;
// }

void jota_reset_peer(struct jota_peer_t *p)
{
  p->udp_conn = NULL;

  memset(p->piece_completed, '0', JOTA_PIECE_COUNT);
  // p->piece_completed = 0;
  
  // p->am_choking = 1;
  p->am_choking = 0;
  p->am_interested = 0;
  // p->peer_choking = 1;
  p->peer_choking = 0;
  p->peer_interested = 0;

  p->last_choked = 0;

  p->state = JOTA_CONN_STATE_IDLE;
  p->downloading_piece_index = -1;
  p->last_tx = 0;
}