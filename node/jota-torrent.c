#include "jota-torrent.h"

struct jota_peer_t *phead = NULL;
unsigned int __nbr_of_peers = 0;

extern uip_ds6_addr_t *my_lladdr;

void jota_insert_peer_to_list(uip_ipaddr_t ipaddr)
{
  struct jota_peer_t *peer = (struct jota_peer_t *)malloc(sizeof(struct jota_peer_t));
  if(peer == NULL) return;

  jota_reset_peer(peer);
  uip_ipaddr_copy(&peer->ipaddr, &ipaddr);
  peer->udp_conn = udp_new(&peer->ipaddr, UIP_HTONS(JOTA_CONN_PORT), NULL);

  if(phead == NULL) {
    phead = peer;
    phead->next = NULL;
  } else {
    struct jota_peer_t *tmp = phead;
    while(tmp->next != NULL) tmp = tmp->next;
    tmp->next = peer;
  }

  __nbr_of_peers++;
}

int jota_remove_peer_from_list(struct jota_peer_t *p)
{
  if(p == NULL) return -1;

  if(phead == p) {
    phead = phead->next;
    free(p);
    __nbr_of_peers--;
    return 0;
  }

  struct jota_peer_t *prev = phead;
  struct jota_peer_t *curr = phead->next;

	while(prev != NULL && curr != NULL) {
    if(curr == p) {
      prev->next = curr->next;
      free(curr);
      __nbr_of_peers--;
      return 0;
    }
    prev = curr;
		curr = curr->next;
  };

  return -1;
}

struct jota_peer_t *jota_get_peer_by_ipaddr(uip_ipaddr_t ipaddr)
{
  struct jota_peer_t *peer = phead;

	while(peer != NULL)
	{
    if(uip_ipaddr_cmp(&ipaddr, &peer->ipaddr)) return peer;
		peer = peer->next;
  };
  return NULL;
}

void jota_reset_peer(struct jota_peer_t *p)
{
  memset(&p->ipaddr, 0, sizeof(uip_ipaddr_t));
  p->udp_conn = NULL;

  p->piece_completed = 0;

  p->am_choking = true;
  p->am_interested = false;
  p->peer_choking = false;
  p->peer_interested = false;

  p->last_handshaked = 0;
  // p->num_zero_handshakes = 0;

  p->last_choked = 0;

  p->ustate = JOTA_CONN_STATE_IDLE;
  p->dstate = JOTA_CONN_STATE_IDLE;

  p->uploading_piece_index = -1;
  p->uploading_block_index = 0;
  p->downloading_piece_index = -1;
  p->downloading_block_index = 0;

  p->num_blocks_uploaded = 0;
  p->num_blocks_downloaded = 0;

  p->last_rx = 0;

  p->txing = false;
  p->last_tx = 0;
  p->num_losses = 0;

  p->ok = false;

  p->next = NULL;
}

/* Store completed peers */
struct jota_completed_peer_t *pcompleted_head = NULL;

struct jota_completed_peer_t *jota_completed_peer_get(uip_ipaddr_t ipaddr)
{
  struct jota_completed_peer_t *peer = pcompleted_head;

	while(peer != NULL)
	{
    if(uip_ipaddr_cmp(&ipaddr, &peer->ipaddr)) return peer;
		peer = peer->next;
  }
  return NULL;
}

void jota_completed_peer_new(uip_ipaddr_t ipaddr)
{
  struct jota_completed_peer_t *peer = (struct jota_completed_peer_t *)malloc(sizeof(struct jota_completed_peer_t));
  if(peer == NULL) return;

  uip_ipaddr_copy(&peer->ipaddr, &ipaddr);
  peer->next = NULL;

  if(pcompleted_head == NULL) {
    pcompleted_head = peer;
    pcompleted_head->next = NULL;
  } else {
    peer->next = pcompleted_head;
    pcompleted_head = peer;
  }
}

bool jota_is_all_completed()
{
  struct jota_peer_t *p = phead;
  while(p != NULL) {
    if(jota_completed_peer_get(p->ipaddr) == NULL) return false;
    p = p->next;
  }
  return true;
}

/* Unassigned peers */
/*
 * Unassigned peers are peers that aren't in our neighbor list but send messages to us
 * they will be queued in case of our slots are available
 */
struct jota_unassigned_peer_t *punassigned_head = NULL;

bool jota_unassigned_peer_is_exists(uip_ipaddr_t ipaddr)
{
  struct jota_unassigned_peer_t *peer = punassigned_head;

	while(peer != NULL)
	{
    if(uip_ipaddr_cmp(&ipaddr, &peer->ipaddr)) return true;
		peer = peer->next;
  };
  return false;
}

void jota_unassigned_peer_enqueue(uip_ipaddr_t ipaddr)
{
  struct jota_unassigned_peer_t *peer = (struct jota_unassigned_peer_t *)malloc(sizeof(struct jota_unassigned_peer_t));
  if(peer == NULL) return;

  uip_ipaddr_copy(&peer->ipaddr, &ipaddr);
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

/* Blacklisted peers */
/*
 * Blacklisted peers are peers that our 5 consecutive messages had not arrived
 * it will not be added to our peer list for a while
 */
struct jota_blacklist_peer_t *pblacklist_head = NULL;

bool jota_blacklist_peer_is_exists(uip_ipaddr_t ipaddr)
{
  struct jota_blacklist_peer_t *peer = pblacklist_head;
	while(peer != NULL)
	{
    if(uip_ipaddr_cmp(&ipaddr, &peer->ipaddr)) return true;
		peer = peer->next;
  }
  return false;
}

void jota_blacklist_peer_add(uip_ipaddr_t ipaddr)
{
  struct jota_blacklist_peer_t *peer = (struct jota_blacklist_peer_t *)malloc(sizeof(struct jota_blacklist_peer_t));
  if(peer == NULL) return;

  uip_ipaddr_copy(&peer->ipaddr, &ipaddr);
  peer->next = NULL;

  if(pblacklist_head == NULL) {
    pblacklist_head = peer;
    pblacklist_head->next = NULL;
  } else {
    peer->next = pblacklist_head;
    pblacklist_head = peer;
  }
}

int jota_blacklist_peer_remove(uip_ipaddr_t ipaddr)
{
  if(uip_ipaddr_cmp(&pblacklist_head->ipaddr, &ipaddr)) {
    struct jota_blacklist_peer_t *tmp = pblacklist_head;
    pblacklist_head = pblacklist_head->next;
    free(tmp);
    return 0;
  }

  struct jota_blacklist_peer_t *prev = pblacklist_head;
  struct jota_blacklist_peer_t *curr = pblacklist_head->next;

	while(prev != NULL && curr != NULL) {
    if(uip_ipaddr_cmp(&ipaddr, &curr->ipaddr)) {
      prev->next = curr->next;
      free(curr);
      return 0;
    }
    prev = curr;
		curr = curr->next;
  };

  return -1;
}