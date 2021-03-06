#include "data_transfer.h"
#include "try_find_peer.h"
#include "node_list.h"
#include <stdlib.h>
#include <stdio.h>
#include "spiffy.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "my_time.h"
#include "peer_storage.h"
#include <string.h>

//packet_type = 2
GET_packet_t *construct_GET_packet(chunk_hash  *hash){
    GET_packet_t *packet = (GET_packet_t *)malloc(sizeof(GET_packet_t) );
    uint16_t packet_len = 16 + 20;
    construct_packet_header(&packet->header, 2, packet_len, 0, 0);
    set_packet_hashes(hash, &packet->hash);
    return packet;
}
DATA_packet_t *construct_DATA_packet(uint8_t *data, int data_num, int seq_num){
    DATA_packet_t *packet = (DATA_packet_t *)malloc(sizeof(DATA_packet_t) + data_num * sizeof(uint8_t));
    uint16_t packet_len = 16 + data_num;
    construct_packet_header(&packet->header, 3, packet_len, seq_num, 0);
    for(int i = 0; i < data_num; i++){
        packet->data[i] = data[i];
    }
    return packet;
}

void add_tunnel_to_sender(chunk_hash *hash, struct sockaddr_in *addr, GET_packet_sender_t *sender){
    if(!cmp_two_sock(addr, &sender->addr)){
        fprintf(stderr, "tunnel don't belong this sender\n");
        return;
    }
    sender->tunnels = (GET_packet_tunnel_t *)realloc(sender->tunnels, (sender->tunnel_num + 1)*sizeof(GET_packet_tunnel_t));
    GET_packet_tunnel_t tunnel = build_GET_packet_tunnel(hash, *addr);
    sender->tunnels[sender->tunnel_num] = tunnel;
    sender->tunnel_num += 1;
    //free_GET_tunnel(tunnel);
}

GET_packet_sender_t *build_GET_packet_sender(struct sockaddr_in *addr, chunk_hash *hash){
    GET_packet_sender_t *sender = (GET_packet_sender_t *)malloc(sizeof(GET_packet_sender_t));
    sender->addr = *addr;
    sender->tunnels = (GET_packet_tunnel_t *)malloc(sizeof(GET_packet_tunnel_t));
    sender->tunnel_num = 1;
    sender->cursor = 0;
    *sender->tunnels = build_GET_packet_tunnel(hash, *addr);
    //add_tunnel_to_sender(hash, addr, sender);
    return sender;
}

GET_packet_tunnel_t build_GET_packet_tunnel(chunk_hash *hash, struct sockaddr_in addr){
    GET_packet_tunnel_t tunnel;// = (GET_packet_tunnel_t *)malloc(sizeof(GET_packet_tunnel_t));
    GET_packet_t *packet = construct_GET_packet(hash);
    init_GET_packet_tunnel(&tunnel, packet, addr);
    return tunnel;
}
void init_GET_packet_tunnel(GET_packet_tunnel_t *tunnel, GET_packet_t *packet, struct sockaddr_in addr){
    tunnel->packet = packet;
    tunnel->addr = addr;
    tunnel->begin_sent = 0;
    tunnel->retransmit_time = 0;
    tunnel->have_been_acked = 0;
    tunnel->chunk = NULL;
}

GET_packet_sender_t *init_GET_packet_sender(hash_addr_map_t *maps, int map_num){
    GET_packet_sender_t *sender = (GET_packet_sender_t *)malloc(sizeof(GET_packet_sender_t));
    sender->tunnels = construct_GET_tunnel(maps, map_num);
    sender->tunnel_num = map_num;
    //sender->chunks = (chunk_t *)malloc(sizeof(chunk_t) * map_num);
    sender->cursor = 0;
    return sender;
}

GET_packet_tunnel_t *construct_GET_tunnel(hash_addr_map_t *maps, int map_num){
    GET_packet_tunnel_t *tunnels = (GET_packet_tunnel_t *)malloc(sizeof(GET_packet_tunnel_t) * map_num);
    int i;
    hash_addr_map_t *the_map;
    GET_packet_tunnel_t *the_tunnel;
    struct sockaddr_in addr;
    for(i = 0; i < map_num; i++){
        the_tunnel = (tunnels + i);
        the_map = (maps + i);
        GET_packet_t *packet = construct_GET_packet(&the_map->hash);
        addr = get_addr_from_node_list(the_map->node_list);
        init_GET_packet_tunnel(the_tunnel, packet, addr);
    }
    return tunnels;
}
void send_GET_tunnel(int sockfd, GET_packet_tunnel_t *tunnel){
    if(tunnel == NULL){
        fprintf(stderr, "the tunnel doesn't exist\n");
    }
    tunnel->begin_sent = millitime(NULL);
    tunnel->retransmit_time += 1;
    spiffy_sendto(sockfd, tunnel->packet, tunnel->packet->header.packet_len, 0, (struct sockaddr *)(&(tunnel->addr)), sizeof(tunnel->addr));
}

void free_GET_tunnel(GET_packet_tunnel_t *tunnel){
    free(tunnel->packet);
    free(tunnel->chunk);
    free(tunnel);
}
void print_sockaddr(struct sockaddr_in addr){
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), str, INET_ADDRSTRLEN);
    fprintf(stderr, "%s: %d\n", str, ntohs(addr.sin_port));
}

void print_GET_packet(GET_packet_t *packet){
    fprintf(stderr, "the packet type is: %d", packet->header.packet_type);
    print_packet_header(&packet->header);
    print_chunk_hash(packet->hash);
}

void print_GET_packet_tunnel(GET_packet_tunnel_t *t){
    print_GET_packet(t->packet);
    fprintf(stderr, "the tunnel to addr: ");
    print_sockaddr(t->addr);
    fprintf(stderr, "begin_time is: %ld\n", t->begin_sent);
    mytime_t now = millitime(NULL);
    fprintf(stderr, "time spent: %ld\n", now - t->begin_sent);
    fprintf(stderr, "limit times: %d\n", t->retransmit_time);
    if(t->chunk != NULL){
        print_chunk(t->chunk);
    }
}


int check_time_out_in_GET_tunnnel_after_last_sent(GET_packet_tunnel_t *t){
    mytime_t now = millitime(NULL);
    mytime_t begin = t->begin_sent;
    if(now - begin > 5000)   return 1;
    return 0;
}

int check_GET_tunnel_retransmit_time(GET_packet_tunnel_t *t){
    return t->retransmit_time;
}

int check_transfer_with_bin_hash(transfer_t *t, chunk_hash *h){
    return check_chunk_with_bin_hash(*t->chunk, h->binary_hash);
}

//construct data packet from transfer
void send_DATA_packet_from_transfer(int sockfd, transfer_t *t, struct sockaddr_in from){
    uint8_t data[PACKET_DATA_SIZE];
    DATA_packet_t *d;
    int size;
    fprintf(stderr,"data:");
    //print_chunk(t->chunk);
    fprintf(stderr, "\n");
    if(t->next_to_send < MAX_SEQ_NUM - 1){
        for(int i = 0; i < PACKET_DATA_SIZE; i++){
            data[i] = t->chunk->data[t->next_to_send * PACKET_DATA_SIZE+i];
        }
        size = PACKET_DATA_SIZE;
    } else if(t->next_to_send == MAX_SEQ_NUM - 1){
        for(int i = t->next_to_send * PACKET_DATA_SIZE; i < DATA_CHUNK_SIZE ; i++){
            int j = i - t->next_to_send * PACKET_DATA_SIZE;
            data[j] = t->chunk->data[i];
        }
        size = DATA_CHUNK_SIZE - t->next_to_send * PACKET_DATA_SIZE;
    } else{
        return;
    }
    d = construct_DATA_packet(data, size, t->seq_num);
    spiffy_sendto(sockfd, d, d->header.packet_len, 0, (struct sockaddr *)(&from), sizeof(struct sockaddr_in));
    free(d);
}
// seq_num means the order of data, start from first, actually the data start from zero
void send_DATA_packet_from_transfer_by_seq(transfer_t *t, uint32_t seq_num, int sockfd, struct sockaddr_in from){
    DATA_packet_t *d;
    int size = PACKET_DATA_SIZE;
    uint8_t data[PACKET_DATA_SIZE];
    uint32_t data_position = seq_num - 1;
    // uint32_t next_to_send = data_position +1;
    memcpy(data, &(t->chunk->data[data_position*PACKET_DATA_SIZE]), PACKET_DATA_SIZE);
    d = construct_DATA_packet(data, size, seq_num);
    spiffy_sendto(sockfd, d, d->header.packet_len, 0, (struct sockaddr *)(&from), sizeof(struct sockaddr_in));
    free(d);
}

void set_data_been_acked(int ack_num, transfer_t *t){
    //t->chunk->seq_bits[ack_num - 1] = 2;
    for(int i = 0; i < ack_num; i++){
        t->chunk->seq_bits[i] = 2;
    }
}
void send_DATA_packet_in_window_with_bug(int sockfd, transfer_t *t, struct sockaddr_in from){
    flow_window_t win = t->sender_window;
    chunk_t *c = t->chunk;
    int i = win.seq_index;
    //t->time_stamp = millitime(NULL);
    //mytime_t current_time;
    for(i = win.begin; i < win.begin + win.window_size; i++){
        if((c->seq_bits[i] == 0)&& (i != 0) && (i != 7) && (i!= 300) && (i!= 400) && (i != 399)){
            c->seq_bits[i] = 1;
            t->next_to_send = i;
            t->seq_num = t->next_to_send + 1;
            send_DATA_packet_from_transfer(sockfd, t, from);
        } else if(c->seq_bits[i] == 1 || c->seq_bits[i] == 2){
            //fprintf(stderr, "it have been sent\n");
            if(c->seq_bits[i] == 1){
                fprintf(stderr, "the data has been sent but not acked\n");
            } else if(c->seq_bits[i] == 2){
                fprintf(stderr, "the data has been acked\n");
            }
        }
    }
}

void send_DATA_packet_in_window_all(int sockfd, transfer_t *t, struct sockaddr_in from){
    flow_window_t *win = &t->sender_window;
    chunk_t *c = t->chunk;
    //int i = win.seq_index;
    if(win->seq_index < win->begin){
        win->seq_index = win->begin;
    }
    for(; win->seq_index < win->begin + win->window_size && win->seq_index < MAX_SEQ_NUM; win->seq_index++){
        c->seq_bits[win->seq_index] = 1;
        t->next_to_send = win->seq_index;
        t->seq_num = t->next_to_send + 1;
        send_DATA_packet_from_transfer(sockfd, t, from);
    }
    fprintf(stderr, "window size: %d\n", win->window_size);
}

void send_DATA_packet_in_window(int sockfd, transfer_t *t, struct sockaddr_in from){
    flow_window_t *win = &t->sender_window;
    chunk_t *c = t->chunk;
    int i; //= win.seq_index;
    int cnt = 0;
    //t->time_stamp = millitime(NULL);
    //mytime_t current_time;
    for(i = win->begin; i < win->begin + win->window_size && i < MAX_SEQ_NUM; i++){
        if((c->seq_bits[i] == 0) ){// && (i != 0) && (i != 7) && (i!= 300) && (i!= 400) && (i != 399)){
            c->seq_bits[i] = 1;
            t->next_to_send = i;
            t->seq_num = t->next_to_send + 1;
            send_DATA_packet_from_transfer(sockfd, t, from);
            cnt += 1;
        } else if(c->seq_bits[i] == 1 || c->seq_bits[i] == 2){
            //fprintf(stderr, "it have been sent\n");
            if(c->seq_bits[i] == 1){
                //fprintf(stderr, "the data has been sent but not acked\n");
                if(t->retransmit_time >= 3){
                    //c->seq_bits[i] = 1;
                     t->next_to_send = i;
                     t->seq_num = t->next_to_send + 1;
                     send_DATA_packet_from_transfer(sockfd, t, from);
                     cnt += 1;
                }
            } else if(c->seq_bits[i] == 2){
                fprintf(stderr, "the data has been acked\n");
            }
        }
    }
    fprintf(stderr, "window size: %d\n", win->window_size);
    if((cnt < win->window_size) && (i < MAX_SEQ_NUM) && (win->seq_index < MAX_SEQ_NUM)){
        for(i = 0; i <= win->window_size - cnt; i++){
            t->next_to_send = win->seq_index + i;
            t->seq_num = t->next_to_send + 1;
            send_DATA_packet_from_transfer(sockfd, t, from);
        }
        win->seq_index += i;
    }
}

void create_output_file(char *output_file, GET_packet_sender_t *sender){
    FILE *fp = fopen(output_file, "w+");
    chunk_t *c;
    for(int i = 0; i < sender->tunnel_num; i++){
        c = (sender->tunnels + i)->chunk;
        fwrite(c->data, 1, DATA_CHUNK_SIZE, fp);
    }
    fclose(fp);
}

int transfer_has_timeout(transfer_t *t){
    mytime_t now = millitime(NULL);
    if((now - t->time_stamp) > t->rtt){
        fprintf(stderr, "time has used out\n");
        t->time_stamp = now;
        return 1;
    }
    t->time_stamp = now;
    return 0;
}

void init_transfer(transfer_t *the_transfer, chunk_hash *hash, bt_config_t *config, struct sockaddr_in to){
    //transfer_t *the_transfer = (transfer_t *)malloc(sizeof(transfer_t));
    the_transfer->chunk = (chunk_t *)malloc(sizeof(chunk_t));
    *(the_transfer->chunk) = load_chunk_from_tar(hash, config);
    the_transfer->next_to_send = 0;
    the_transfer->seq_num = 1;
    the_transfer->rtt = 0;
    init_flow_window(&the_transfer->sender_window);
    the_transfer->to = to;
    the_transfer->retransmit_time = 0;
    the_transfer->start_time = millitime(NULL);
    the_transfer->time_stamp = millitime(NULL);
    the_transfer->start_time_for_CA = 0;
    the_transfer->time_out_num = 0;
    
    //ps->transfer_num += 1
    // return the_transfer;
}

int cmp_transfer_by_sockaddr(const void *a, const void *b){
    return cmp_two_sock((struct sockaddr_in *)a, &(((transfer_t *)b)->to));
}

int cmp_sender_by_sockaddr(const void *a, const void *b){
    return cmp_two_sock((struct sockaddr_in *)a, &(((GET_packet_sender_t *)b)->addr));
}

int remove_transfer(void *data){
    transfer_t *t = (transfer_t *)data;
    free(t->chunk);
    free(t);
    return 0;
}

int remove_sender(void *data){
    int i;
    GET_packet_tunnel_t *tunnel;
    GET_packet_sender_t *s = (GET_packet_sender_t *)data;
    if(s->tunnel_num > 0){
        for(i = 0; i < s->tunnel_num; i++){
            tunnel = s->tunnels + i;
            free(tunnel->packet);
            free(tunnel->chunk);
        }
        free(s->tunnels);
    }
    free(s);
    return 0;
}

int test_sender_exist(void *data){
    if(data == NULL)  return 0;
    return 1;
}

GET_packet_tunnel_t *find_corresponding_tunnel(GET_packet_sender_t *sender, chunk_hash *hash){
    int i;
    GET_packet_tunnel_t *tunnel;
    for(i = 0; i < sender->tunnel_num; i++){
        tunnel = sender->tunnels + i;
        if(two_hash_equal(*hash, tunnel->packet->hash)){
            return tunnel;
        }
    }
    return NULL;
}