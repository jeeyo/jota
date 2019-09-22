#include "jota-torrent.h"

struct jota_peer_t peers[JOTA_NBR_OF_PEERS];
struct jota_peer_t *phead = NULL;
unsigned int __nbr_of_peers = 0;

void jota_insert_peer_to_list(uip_ipaddr_t *ipaddr)
{
  struct jota_peer_t *peer = (struct jota_peer_t *)malloc(sizeof(struct jota_peer_t));
  jota_reset_peer(peer);
  uip_ipaddr_copy(&peer->ipaddr, ipaddr);
  peer->udp_conn = udp_new(&peer->ipaddr, UIP_HTONS(JOTA_CONN_PORT), NULL);

  if(phead == NULL) {
    phead = peer;
    phead->next = NULL;
  } else {
    peer->next = phead;
    phead = peer;
  }
  __nbr_of_peers++;
}

int jota_remove_peer_from_list(struct jota_peer_t *p)
{
  if(p == NULL) return -1;
  
  struct jota_peer_t *tmp = phead;
  struct jota_peer_t *prev = NULL;

  if(phead == p) {
    phead = p->next;
    free(p);
    
    __nbr_of_peers--;
    return 0;
  }

	while(tmp != NULL && tmp != p)
	{
    prev = tmp;
		tmp = tmp->next;
  };

  // Skip if the peer is not found
  if(tmp == NULL) return -1;

  // Remove the peer from the list
  prev->next = tmp->next;
  free(tmp);

  __nbr_of_peers--;
  return 0;
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
struct jota_completed_peer_t *pcompleted_head = NULL;

struct jota_completed_peer_t *jota_completed_peer_get(uip_ipaddr_t *ipaddr)
{
  if(ipaddr == NULL) return NULL;

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
  if(ipaddr == NULL) return;

  struct jota_completed_peer_t *peer = (struct jota_completed_peer_t *)malloc(sizeof(struct jota_completed_peer_t));
  uip_ipaddr_copy(&peer->ipaddr, ipaddr);
  peer->next = NULL;

  if(pcompleted_head == NULL) {
    pcompleted_head = peer;
    pcompleted_head->next = NULL;
  } else {
    peer->next = pcompleted_head;
    pcompleted_head = peer;
  }
}

/* Queue unassigned peers */
struct jota_unassigned_peer_t *punassigned_head = NULL;

bool jota_unassigned_peer_is_exists(uip_ipaddr_t *ipaddr)
{
  if(ipaddr == NULL) return false;

  struct jota_unassigned_peer_t *peer = punassigned_head;

	while(peer != NULL)
	{
    if(uip_ipaddr_cmp(ipaddr, &peer->ipaddr)) return true;
		peer = peer->next;
  };
  return false;
}

void jota_unassigned_peer_enqueue(uip_ipaddr_t *ipaddr)
{
  if(ipaddr == NULL) return;

  struct jota_unassigned_peer_t *peer = (struct jota_unassigned_peer_t *)malloc(sizeof(struct jota_unassigned_peer_t));
  uip_ipaddr_copy(&peer->ipaddr, ipaddr);
  peer->next = NULL;

  if(punassigned_head == NULL) {
    punassigned_head = peer;
    punassigned_head->next = NULL;
  } else {

    struct jota_unassigned_peer_t *p = punassigned_head;
    while(p->next != NULL) {
      p = p->next;
    }
    p->next = peer;
  }
}

int jota_unassigned_peer_dequeue(uip_ipaddr_t *ipaddr)
{
  if(punassigned_head == NULL) return -1;

  struct jota_unassigned_peer_t *tmp = punassigned_head;
  uip_ipaddr_copy(ipaddr, &tmp->ipaddr);

  punassigned_head = punassigned_head->next;
  free(tmp);
  return 0;
}