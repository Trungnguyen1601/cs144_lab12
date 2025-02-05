/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_linked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"
#include <string.h>

/**
 * Unacknowledged packets
 *
 */

typedef struct unacknowledged_segment{
  ctcp_segment_t *segment;
  long last_time_send;
  uint8_t num_retransmit;
}unacknowledged_segment_t;




/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */


struct ctcp_state {
  struct ctcp_state *next;  /*linked_list_unack_segment = ll_create(); Next in linked list */
  struct ctcp_state **prev; /* Prev in linked list */

  conn_t *conn;             /* Connection object -- needed in order to figure
                               out destination when sending */
  //linked_list_t *segments; /* Linked list of segments sent to this connection.
                              //  It may be useful to have multiple linked lists
                              //  for unacknowledged segments, segments that
                              //  haven't been sent, etc. Lab 1 uses the
                              //  stop-and-wait protocol and therefore does not
                              //  necessarily need a linked list. You may remove
                              //  this if this is the case for you */

  uint32_t seq_no;
  uint32_t ack_no;
  ctcp_segment_t *send_segment;
  ctcp_config_t *config;
  linked_list_t *linked_list_unack_segment;

  bool check_FIN_receive;

  /* FIXME: Add other needed fields. */
};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */
/*
  Funtion
  Segment in network byte order ntohl
*/
void segment_ntoh(ctcp_segment_t *segment)
{
  segment->seqno = ntohl(segment->seqno);
  segment->ackno = ntohl(segment->ackno);
  segment->len = ntohs(segment->len);
  segment->window = ntohs(segment->window);
  segment->flags = ntohl(segment->flags);
  //segment->cksum = ntohs(segment->cksum);


}

void segment_hton(ctcp_segment_t *segment)
{
  segment->seqno = htonl(segment->seqno);
  segment->ackno = htonl(segment->ackno);
  segment->len = htons(segment->len);
  segment->window = htons(segment->window);
  segment->flags = htonl(segment->flags);
  //segment->cksum = htons(segment->cksum);

}

ctcp_segment_t *generate_ACK_segment(ctcp_state_t *state)
{
  ctcp_segment_t * segment;
  uint16_t len_segment = sizeof(ctcp_segment_t);
  segment = (ctcp_segment_t*)calloc(len_segment, 1);

  segment->seqno = state->seq_no;
  segment->ackno = state->ack_no;
  segment->len = len_segment;
  segment->flags |= ACK;
  segment->window = state->config->recv_window;
  segment_ntoh(segment);
  segment->cksum = 0;
  segment->cksum = cksum(segment,len_segment);

  return segment;
}


ctcp_segment_t *generate_FIN_segment(ctcp_state_t *state)
{
  ctcp_segment_t * segment;
  uint16_t len_segment = sizeof(ctcp_segment_t);
  segment = (ctcp_segment_t*)calloc(len_segment, 1);

  segment->seqno = state->seq_no;
  segment->ackno = state->ack_no;
  segment->len = len_segment;
  segment->flags |= FIN;
  segment->window = state->config->recv_window;
  segment_hton(segment);
  segment->cksum = 0;
  segment->cksum = cksum(segment,len_segment);

  return segment;

}

void send_ACK(ctcp_state_t *state)
{
  ctcp_segment_t *segment_ACK = generate_ACK_segment(state);
  //segment_ntoh(segment_ACK);
  if (conn_send(state->conn,segment_ACK, sizeof(ctcp_segment_t)) == -1)
  {
    return ;
  }
}


void send_FIN(ctcp_state_t *state)
{
  ctcp_segment_t *segment_FIN = generate_FIN_segment(state);
  //segment_hton(segment_FIN);
  if (conn_send(state->conn,segment_FIN, sizeof(ctcp_segment_t)) == -1)
  {
    return ;
  }
}

ctcp_segment_t *copy_segment(ctcp_segment_t * segment, uint16_t len)
{
  ctcp_segment_t * segment_copy;
  segment_copy = (ctcp_segment_t*)calloc(len, 1);

  segment_copy->seqno = segment->seqno;
  segment_copy->ackno = segment->seqno;
  segment_copy->len = segment->len;
  segment_copy->flags = segment->flags;
  segment_copy->window = segment->window;
  segment_copy->cksum = segment->cksum;
  memcpy(segment_copy->data,segment->data,len);
  return segment_copy;
}

ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */

  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;
  /* Set fields. */
  state->conn = conn;

  state->seq_no = 1;
  state->ack_no = 1;
  state->config = cfg;
  state->check_FIN_receive = false;

  state->linked_list_unack_segment = ll_create();

  /* FIXME: Do any other initialization here. */
  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);
  ll_destroy(state->linked_list_unack_segment);

  /* FIXME: Do any other cleanup here. */
  free(state->config);
  free(state);
  end_client();
}

void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  char * buffer;
  uint16_t buffer_len = state->config->send_window;
  uint16_t len_segment;
  long last_time_send_segment;
  ctcp_segment_t *segment;
  buffer = (char*)calloc(state->config->send_window,1);

  int32_t data_len = conn_input(state->conn, buffer, buffer_len);
  if (data_len == -1)
  {
    // send FIN
    send_FIN(state);
    state->seq_no = state->seq_no + 1;
  }
  else if (data_len == 0)
  {
    // 
  }
  else
  {
    // size_t num_chunks = buffer_size / chunk_size;
    // size_t remaining_bytes = buffer_size % chunk_size;

    // for 
    len_segment = sizeof(ctcp_segment_t) + data_len;
    segment = (ctcp_segment_t*)calloc(len_segment, 1);

    segment->seqno = state->seq_no;
    segment->ackno = state->ack_no;
    segment->len = len_segment;
    segment->flags |= ACK;
    segment->window = state->config->send_window;
    memcpy(segment->data,buffer,data_len);
    segment_hton(segment);

    segment->cksum = 0;
    segment->cksum = cksum(segment,len_segment);

    int ret = conn_send(state->conn,segment,len_segment);
    last_time_send_segment = current_time();
    //fprintf(stderr,"%ld 12\n",last_time_send_segment);

    if(ret == -1)
    {
      return;
    }
    else
    {
      state->seq_no += data_len;
    }
    unacknowledged_segment_t * unack_segment = (unacknowledged_segment_t*) calloc(sizeof(unacknowledged_segment_t),1);
    unack_segment->num_retransmit = 0;
    unack_segment->last_time_send = last_time_send_segment;
    unack_segment->segment = segment;
    ll_add(state->linked_list_unack_segment,unack_segment);
    
    //free(unack_segment);
  }
  //free(segment);
  free(buffer);
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
  if (segment == NULL)
  {
    return;
  }
  segment_ntoh(segment);

  char * buffer;
  uint16_t cksum_recv;
  uint16_t data_len = len - sizeof(ctcp_segment_t);
  buffer = (char*)calloc(state->config->recv_window,1);
  memcpy(buffer,segment->data,data_len);

  // Truncated

  if (len < segment->len )
  {
    free(segment);
    return;
  }


  segment_hton(segment);
  cksum_recv = segment->cksum;
  segment->cksum = 0;
  uint16_t cksum_recv_check = cksum(segment, ntohs(segment->len));

  if (cksum_recv != cksum_recv_check)
  {
    fprintf(stderr,"1\n");
    free(segment);
    return;
  }
  segment_ntoh(segment);
  //state->send_segment = segment;
  if (segment->seqno != state->ack_no)
  {
    fprintf(stderr,"hihi\n");
    free(segment);
    send_ACK(state);
    return;
  }

  ll_node_t *node = ll_front(state->linked_list_unack_segment);
  if (node)
  {
    ll_remove(state->linked_list_unack_segment,node);
  }

  if (segment->flags & FIN)
  {
    state->ack_no = segment->seqno + 1;
    send_ACK(state);
    if (conn_bufspace(state->conn) > len )
    {
      if(conn_output(state->conn,buffer,0) == -1)
      {
        ctcp_destroy(state);
        return;
      }
    }
    //fprintf(stderr,"hihi");
  }
  else if (segment->flags & ACK)
  {
    state->ack_no = segment->seqno + data_len;
  }
  
  if (data_len > 0)
  {
    send_ACK(state);
    if (conn_bufspace(state->conn) > len )
    {
      if(conn_output(state->conn,buffer,data_len) == -1)
      {
        ctcp_destroy(state);
        return;
      }
    }

  }

  if (segment->flags & FIN)
  {
    state->check_FIN_receive = true;
  }

  if ( (segment->flags & ACK) && state->check_FIN_receive)
  {
    ctcp_destroy(state);
  }
  
  //free(segment);

}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  ctcp_segment_t *segment;
  segment = (ctcp_segment_t*)calloc(state->send_segment->len,1);
  segment = state->send_segment;

  char * buffer;
  uint16_t data_len = state->send_segment->len - sizeof(ctcp_segment_t);
  buffer = (char*)calloc(data_len,1);

  if (state->send_segment->len > conn_bufspace(state->conn))
  {

  }
  else
  {
    if(conn_output(state->conn,buffer,data_len) == -1)
    {
      ctcp_destroy(state);
      return;
    }
    if (segment->flags & FIN)
    {
      state->ack_no = state->send_segment->seqno + 1;
    }
    else
    {
      state->ack_no = state->send_segment->seqno + data_len;
    }


  }

}

void ctcp_timer() {
  /* FIXME */

  // TIMER_INTERVAL 40
  // Time MAX_SEG_LIFETIME_MS 4000

  ctcp_state_t *state_current;
  state_current = state_list;
  ll_node_t *node;

  while (state_current)
  {
    node = ll_front(state_current->linked_list_unack_segment);
    unacknowledged_segment_t *unack_segment_timer;

    while (node)
    {
        unack_segment_timer = (unacknowledged_segment_t*)node->object;
        //fprintf(stderr,"%ld 1 \n",current_time() - unack_segment_timer->last_time_send );
        if ((current_time() - unack_segment_timer->last_time_send)  > state_current->config->rt_timeout)
        {
          // Retransmit segment
          if (unack_segment_timer->num_retransmit >= (MAX_NUM_XMITS))
          {
            ctcp_destroy(state_current);
            return;
          }

          if (conn_send(state_current->conn,unack_segment_timer->segment,ntohs(unack_segment_timer->segment->len) ) == -1)
          {
            //fprintf(stderr, "Error\n");
          }
          unack_segment_timer->num_retransmit ++;
          unack_segment_timer->last_time_send = current_time();
        }
        node = node->next;
    }
    state_current = state_current->next;
  }
  
}