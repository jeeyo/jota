#include "jota-torrent.h"

struct jota_peer_t peers[JOTA_NBR_OF_PEERS];
struct jota_peer_t *phead = NULL;
unsigned int __nbr_of_peers = 0;

void jota_remove_peer_from_list(struct jota_peer_t *p)
{
  if(p == NULL) return;
  
  struct jota_peer_t *tmp = phead;
  struct jota_peer_t *prev = NULL;

  if(phead == p) {
    phead = p->next;
    free(p);
    return;
  }

	while(tmp != NULL && tmp != p)
	{
    prev = tmp;
		tmp = p->next;
  };

  // Skip if the peer is not found
  if(tmp == NULL) return;

  // Remove the peer from the list
  prev->next = tmp->next;
  free(tmp);
}

struct jota_peer_t *jota_get_peer_by_ipaddr(uip_ipaddr_t *ipaddr)
{
  struct jota_peer_t *peer = phead;

	while(peer != NULL)
	{
    if(uip_ipaddr_cmp(ipaddr, &peer->ipaddr)) return peer;
		peer = peer->next;
  };
  return NULL;
}

void jota_reset_peer(struct jota_peer_t *p)
{
  memset(&p->ipaddr, 0, sizeof(uip_ipaddr_t));
  p->udp_conn = NULL;

  p->piece_completed = 0;
  
  p->am_choking = 0;
  p->am_interested = 0;
  p->peer_choking = 0;
  p->peer_interested = 0;

  p->last_handshaked = 0;
  p->last_choked = 0;

  p->state = JOTA_CONN_STATE_IDLE;

  p->uploading_piece_index = -1;
  p->downloading_piece_index = -1;
  p->downloading_block_index = 0;

  p->last_rx = 0;

  p->txing = false;
  p->last_tx = 0;
  p->num_losses = 0;

  p->next = NULL;
}

/* Store completed peers */
struct jota_completed_peer_t {
  uip_ipaddr_t ipaddr;
  struct jota_completed_peer_t *next;
};

struct jota_completed_peer_t *pcompleted_head = NULL;

struct jota_completed_peer_t *jota_completed_peer_get(uip_ipaddr_t *ipaddr)
{
  struct jota_completed_peer_t *peer = pcompleted_head;

	while(peer != NULL)
	{
    if(uip_ipaddr_cmp(ipaddr, &peer->ipaddr)) return peer;
		peer = peer->next;
  };
  return NULL;
}

void jota_completed_peer_new(uip_ipaddr_t *ipaddr)
{
  struct jota_completed_peer_t *peer = (struct jota_completed_peer_t *)malloc(sizeof(struct jota_completed_peer_t));
  uip_ipaddr_copy(&peer->ipaddr, ipaddr);

  if(pcompleted_head == NULL) {
    pcompleted_head = peer;
    phead->next = NULL;
  } else {
    peer->next = pcompleted_head;
    pcompleted_head = peer;
  }
}