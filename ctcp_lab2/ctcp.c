/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"


typedef struct {

  // last byte send in send segment, that received ack
	uint32_t last_byte_send_ack;  
	// last byte send in send segment, that not received ack
	uint32_t last_byte_send_not_ack;    
	// check the EOF
	bool check_EOF;
	
	// list contains unacknowledge segments 
	linked_list_t *send_segment;
}ctcp_state_send_t;


typedef struct{

  uint32_t recv_base;
	// check the FIN
	bool check_FIN;  
	// buffer segments that conteains segment no inorder             
	linked_list_t *buffer_segment;
}ctcp_state_receive_t;

/**
 * Packet data
 *
 */
typedef struct {
  ctcp_segment_t *segment;
  long last_time_send;
  uint8_t num_retransmit;
}packet_t;
/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */
struct ctcp_state {
    struct ctcp_state *next;  /* Next in linked list */
    struct ctcp_state **prev; /* Prev in linked list */

    conn_t *conn;             /* Connection object -- needed in order to figure
                               out destination when sending */
    //linked_list_t *segments;  /* Linked list of segments sent to this connection.
                                //    It may be useful to have multiple linked lists
                                //    for unacknowledged segments, segments that
                                //    haven't been sent, etc. Lab 1 uses the
                                //    stop-and-wait protocol and therefore does not
                                //    necessarily need a linked list. You may remove
                                //    this if this is the case for you */
    
	  // Last byte read from STDIN, that is ready to be sent
    uint32_t last_read_ready_for_sent_seqno;

    ctcp_state_send_t *send_state;
    ctcp_state_receive_t *receive_state;

    ctcp_config_t *ctcp_config;
  /* FIXME: Add other needed fields. */
};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */

/* Send ACK segment  when received data segment*/
void ctcp_send_ACK_segment(ctcp_state_t *state, packet_t *packet);

/* Send data segment  */
void ctcp_send_segment(ctcp_state_t *state, packet_t *packet);

/* Send all available packet in send segment */
void ctcp_transmit_window_segment(ctcp_state_t *state);

/* Remove acknowledged segments from the NAK segment linked list */
void ctcp_remove_segment_after_receive_ack(ctcp_state_t *state, ctcp_segment_t *segment);

/* Function utilized to organize received segments to maintain the sequence of the output segment linked list*/
void add_inorder_in_receive_list(ctcp_state_t *state, ctcp_segment_t *segment);

int32_t calc_byte_continuous_in_buffer(ctcp_state_t *state);

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
  /* FIXME: Do any other initialization here. */

  state->send_state = (ctcp_state_send_t*)calloc(sizeof(ctcp_state_send_t),1);
  state->send_state->last_byte_send_ack = 0;
	state->send_state->last_byte_send_not_ack = 0;
	state->send_state->check_EOF = false;
	state->send_state->send_segment = ll_create();

  state->receive_state = (ctcp_state_receive_t*)calloc(sizeof(ctcp_state_receive_t),1);
  state->receive_state->recv_base = 1;
	state->receive_state->check_FIN = false;
	state->receive_state->buffer_segment = ll_create();

  state->ctcp_config = cfg;
  state->last_read_ready_for_sent_seqno = 0;

  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */

  free(state);
  end_client();
}

void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  int byte_sent,bytes_read;
	char *buff;
	packet_t *packet;
  buff = (char*)calloc(MAX_SEG_DATA_SIZE,1);
	if (state->send_state->check_EOF)
		return;
	while ((bytes_read = conn_input(state->conn, buff, MAX_SEG_DATA_SIZE)) > 0) { 
    byte_sent = bytes_read;

		packet = calloc(1,sizeof(packet_t) + byte_sent); 
    packet->segment = (ctcp_segment_t*)calloc(sizeof(ctcp_segment_t)+ byte_sent,1);
		packet->segment->seqno = htonl(state->last_read_ready_for_sent_seqno + 1);
		packet->segment->len = htons(sizeof(ctcp_segment_t) + byte_sent);
    packet->segment->window = htons(state->ctcp_config->recv_window);
		buff[byte_sent] = '\0';
		memcpy(packet->segment->data, buff, byte_sent);
		/* Add packet to segment send*/
		ll_add(state->send_state->send_segment, packet);
		state->last_read_ready_for_sent_seqno += byte_sent;

	}
	/* Read EOF */
	if (bytes_read == -1) {      
		packet = calloc(1, sizeof(packet));
    packet->segment = (ctcp_segment_t*)calloc(sizeof(ctcp_segment_t),1);
		packet->segment->seqno  = htonl(state->last_read_ready_for_sent_seqno + 1);
		packet->segment->len    = htons((uint16_t)sizeof(ctcp_segment_t));
    packet->segment->window = htons(state->ctcp_config->recv_window);
		packet->segment->flags |= TH_FIN;
		ll_add(state->send_state->send_segment, packet);
		state->send_state->check_EOF = true;
	} 

	ctcp_transmit_window_segment(state);
}


void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
  packet_t *packet_recv = NULL;
  packet_t *packet_ack = NULL;
  uint16_t checksum_recv = 0;
  uint16_t checksum_check = 0;
  uint16_t data_len = 0;

  // Handle truncated
  if (segment == NULL)
  {
    return;
  }
  if (len < ntohs(segment->len))
  {
    free(segment);
    return;
  }

  // handle checksum
  checksum_recv = segment->cksum;
  segment->cksum = 0;
  checksum_check = cksum(segment,ntohs(segment->len));
  segment->cksum = checksum_check;

  if (checksum_recv != checksum_check)
  {
    free(segment);
    return;
  }

  data_len = ntohs(segment->len) - sizeof(ctcp_segment_t);

  if (data_len == 0 && (segment->flags & TH_ACK))
  {
    ctcp_remove_segment_after_receive_ack(state,segment);
  }
  
  if (segment->flags & TH_FIN)
  {
    fprintf(stderr,"receive FIN\n");
    packet_recv = (packet_t*)calloc(1,sizeof(packet_t));
    packet_recv->segment = (ctcp_segment_t*)calloc(1,sizeof(ctcp_segment_t));
    packet_recv->segment->ackno = htonl(ntohl(segment->seqno) + 1);
    packet_recv->segment->seqno = segment->ackno;
    ctcp_send_ACK_segment(state,packet_recv);
    fprintf(stderr,"receive FIN\n");

    state->receive_state->check_FIN = true;
    if (conn_bufspace(state->conn) > 1)
    {
      int ret = conn_output(state->conn,"",0);
      if (ret == -1)
      {
        ctcp_destroy(state);
        return;
      }
    }

  }
  else if (data_len > 0)
  {
    // If segment is inorder, will send this segment
    if (ntohl(segment->seqno) == state->receive_state->recv_base)
    {
      packet_ack = (packet_t*)calloc(1,sizeof(packet_t));
      packet_ack->segment = (ctcp_segment_t*)calloc(sizeof(ctcp_segment_t),1);
      packet_ack->segment->ackno = htonl((ntohl(segment->seqno) + data_len));
      packet_ack->segment->seqno = segment->ackno;

      ctcp_send_ACK_segment(state,packet_ack);
      conn_output(state->conn,segment->data,data_len);
      state->receive_state->recv_base += data_len;
    }
    // If segment is not inorder, will add to buffer segment
    else 
    {
      add_inorder_in_receive_list(state,segment);
    }

  }

  ctcp_output(state);

  if (state->receive_state->check_FIN && (segment->flags & TH_ACK))
  {
    ctcp_destroy(state);
    return;
  }

}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  ll_node_t *node;
  packet_t * packet_ack;
  ctcp_segment_t *segment_output;
  int32_t output_seqno;
  uint16_t data_len;

  bool check = false;

  output_seqno = calc_byte_continuous_in_buffer(state);
  if (output_seqno == -1)
  {
    return;
  }
  node = ll_front(state->receive_state->buffer_segment);
  do
  {
    segment_output = (ctcp_segment_t*)node->object;
    data_len = segment_output->len - sizeof(ctcp_segment_t);
    if (segment_output->seqno + data_len <= output_seqno)
    {
      packet_ack = (packet_t*)calloc(1,sizeof(packet_t));
      packet_ack->segment->ackno = htonl((ntohl(segment_output->seqno) + data_len));
      packet_ack->segment->seqno = segment_output->ackno;
      ctcp_send_ACK_segment(state,packet_ack);
      state->receive_state->recv_base += data_len;
      conn_output(state->conn,segment_output->data,ntohs(segment_output->len));
      check = true;
    }
    else
    {
      check = false;
    }
  }while(check);

}

void ctcp_timer() {
  /* FIXME */
  ctcp_state_t *current_state;
	if (state_list ==  NULL)
		return;
  current_state = state_list;
  while (current_state)
  {
    ctcp_transmit_window_segment(current_state);
    current_state = current_state->next;
  }
}

void ctcp_transmit_window_segment(ctcp_state_t *state)
{
  uint16_t data_len = 0;
  if(ll_length(state->send_state->send_segment) == 0)
  {
    return;
  }

  ll_node_t *node = ll_front(state->send_state->send_segment);
  uint32_t last_seqno_max_segment;
  if (node == NULL)
  {
    return;
  }

  while (node)
  {
    packet_t *packet_nak = (packet_t*) node->object;
    data_len = ntohs(packet_nak->segment->len) - sizeof(ctcp_segment_t);
    last_seqno_max_segment = state->send_state->last_byte_send_ack + state->ctcp_config->send_window;
    
    if ((ntohl(packet_nak->segment->seqno) + data_len - 1) > last_seqno_max_segment)
    {
      return;
    }

    if (packet_nak->num_retransmit == 0)
    {
      ctcp_send_segment(state,packet_nak);
      state->send_state->last_byte_send_not_ack += data_len;
    }

    if ((current_time() - packet_nak->last_time_send) > state->ctcp_config->rt_timeout)
    {
      if (state->send_state->last_byte_send_not_ack != state->send_state->last_byte_send_ack)
      {
        if(packet_nak->num_retransmit >= MAX_NUM_XMITS)
        {
          ctcp_destroy(state);
          return;
        }
        ctcp_send_segment(state,packet_nak);
      }
    }
    node = node->next;
  }
}

void ctcp_send_segment(ctcp_state_t *state, packet_t *packet){
	packet->segment->ackno  = htonl(state->receive_state->recv_base);
	packet->segment->flags  |= TH_ACK;
	packet->segment->cksum  = 0;
	packet->segment->cksum  = cksum(packet->segment, ntohs(packet->segment->len));
	int byte_sent = conn_send(state->conn, packet->segment, ntohs(packet->segment->len));
	packet->last_time_send = current_time();
	if (byte_sent <  ntohs(packet->segment->len)){
		return;
	}

	if (byte_sent == -1){
		ctcp_destroy(state);
		return;
	}
	packet->num_retransmit ++;
}

void ctcp_remove_segment_after_receive_ack(ctcp_state_t *state, ctcp_segment_t *segment)
{
  uint16_t data_len;
  ll_node_t *node = ll_front(state->send_state->send_segment);
  while(node != NULL)
  {
    packet_t *packet = (packet_t*)node->object;
    data_len = ntohs(packet->segment->len) - sizeof(ctcp_segment_t);
    //fprintf(stderr,"check %d %d\n",ntohl(packet->segment->seqno) + data_len,ntohl(segment->ackno));
    if ((ntohl(packet->segment->seqno) + data_len) == ntohl(segment->ackno))
    {
      state->send_state->last_byte_send_ack += data_len;
      free(packet);
      ll_remove(state->send_state->send_segment,node);
      break;
    }
    else
    {
      node = node->next;
    }
  }
}

void ctcp_send_ACK_segment(ctcp_state_t *state, packet_t *packet){
	packet->segment->len = htons(sizeof(ctcp_segment_t));
	packet->segment->flags  |= TH_ACK;
	packet->segment->window = htons(state->ctcp_config->recv_window);
	packet->segment->cksum  = 0;
	packet->segment->cksum  = cksum(packet->segment, ntohs(packet->segment->len));
	conn_send(state->conn, packet->segment, sizeof(ctcp_segment_t));
}

void add_inorder_in_receive_list(ctcp_state_t *state, ctcp_segment_t *segment)
{
  if(ll_length(state->receive_state->buffer_segment) == 0)
  {
    ll_add(state->receive_state->buffer_segment,segment);
  }
  else if (ll_length(state->receive_state->buffer_segment) == 1)
  {
    ll_node_t *first_node = ll_front(state->receive_state->buffer_segment);
    ctcp_segment_t *first_segment = (ctcp_segment_t*) first_node->object;
    if (ntohl(segment->seqno) > ntohl(first_segment->seqno))
    {
      ll_add(state->receive_state->buffer_segment,segment);
    }
    if (ntohl(segment->seqno) < ntohl(first_segment->seqno))
    {
      ll_add_front(state->receive_state->buffer_segment,segment);
    }
  }
  else
  {
    ll_node_t *current_node = ll_front(state->receive_state->buffer_segment);
		ctcp_segment_t *current_segment = (ctcp_segment_t *) current_node->object;
		ctcp_segment_t *next_segment = (ctcp_segment_t *) current_node->next->object;
		while (current_node != NULL){
			if ((ntohl(segment->seqno) > ntohl(current_segment->seqno)) ||
				(ntohl(segment->seqno) < ntohl(next_segment->seqno))){
				ll_add_after(state->receive_state->buffer_segment, current_node, segment);
				break;
			}
			current_node = current_node->next;
		}
  }
}

int32_t calc_byte_continuous_in_buffer(ctcp_state_t *state)
{
  uint16_t data_len;
  ll_node_t *node;
  ll_node_t *node_next;
  ctcp_segment_t *segment_data;
  ctcp_segment_t *segment_data_next;

  int32_t ret_seqno;
  uint16_t len_buffer = ll_length(state->receive_state->buffer_segment);
  //bool check_continuous = true;
  if (len_buffer == 0)
  {
    return -1;
  }

  node = ll_front(state->receive_state->buffer_segment);
  if (node == NULL)
  {
    return -1;
  }

  segment_data = (ctcp_segment_t*)node->object;
  data_len = segment_data->len - sizeof(ctcp_segment_t);
  //fprintf(stderr,"seqno %d %d\n",ntohl(segment_data->seqno), ntohs(segment_data->len));

  if(len_buffer == 1)
  {
    ret_seqno = ntohl(segment_data->seqno) + data_len;
  }
  else if (len_buffer > 1)
  {
  do
    {
      data_len = segment_data->len - sizeof(ctcp_segment_t);
      node_next = node->next;

      segment_data_next = (ctcp_segment_t*)node_next->object;
      if (ntohl(segment_data->seqno) + data_len == ntohl(segment_data_next->seqno))
      {
        ret_seqno = ntohl(segment_data->seqno) + data_len;
      }

      node = node->next;
    }while(node);
  }

  return ret_seqno;
}
