#include <stdio.h>
#include "peer_storage.h"
#include "bt_client.h"
#include "bt_parse.h"
#include <time.h>
#include "peer.h"
#include "data_transfer.h"
#include "try_find_peer.h"
void handle_IHAVE_packet(contact_packet_t *packet, struct sockaddr_in from){
    if(get_peer_state() == ASK_RESOURCE_LOCATION){
        check_IHAVE_packet(packet, from);
        if(found_all_resource_locations()){
            set_peer_state(FOUND_ALL_RESOURCE);
            fprintf(stderr, "i have found all resource locations\n");
            free_peer_temp_state_for_GET_in_pool();
        }
    } else if(get_peer_state() == GET_ERROR_FOR_RESOURCE_LOCATION){
        //we need to resend GET tunnel
        GET_packet_sender_t *sender = p->GET_packet_sender;
        if((sender->tunnels + sender->cursor)->packet == NULL){
            fprintf(stderr, "we need to reconstruct sender tunnel\n");
            GET_packet_tunnel_t *tunnel = sender->tunnels + sender->cursor;
            if(packet->hashes_num > 1){
                fprintf(stderr, "the IHAVE packet not for GET ERROR: too many hashes\n");
                return;
            }
            init_GET_packet_tunnel(tunnel, construct_GET_packet(&packet->hashes[0]), from);
            print_GET_packet_tunnel(sender->tunnels + sender->cursor);
        } else{
            fprintf(stderr, "something wrong about GET retransmititon in handle_IHAVE_packet");
        }
    }
}

void handle_client_timeout(int sockfd, bt_config_t *config){
    fprintf(stderr, "handle_client_timeout\n");
    if(get_peer_state() == ASK_RESOURCE_LOCATION){
        send_WHOHAS_packet(sockfd, config);
    }
    else if(get_peer_state() == FOUND_ALL_RESOURCE){
        // print_GET_packet_sender();
        GET_packet_sender_t *sender = p->GET_packet_sender;
        GET_packet_tunnel_t *tunnel = sender->tunnels + sender->cursor;
        if(check_time_out_in_GET_tunnnel_after_last_sent(tunnel) && check_GET_tunnel_retransmit_time(tunnel) < 3){
            fprintf(stderr, "You need to retransmit GET packet for hash: ");
            print_chunk_hash(tunnel->packet->hash);
            send_GET_tunnel(sockfd, tunnel);
            return;
        } else if(check_GET_tunnel_retransmit_time(tunnel) >= 3){
            fprintf(stderr, "you need to find another source in peer for hash: ");
            print_chunk_hash(tunnel->packet->hash);
            add_hash_to_peer_temp_state_for_GET_in_pool(&tunnel->packet->hash);
            set_WHOHAS_cache_in_pool();
            set_peer_state(GET_ERROR_FOR_RESOURCE_LOCATION);
            free_GET_tunnel(tunnel);
            tunnel = (GET_packet_tunnel_t *)malloc(sizeof(GET_packet_tunnel_t));
            tunnel->packet = NULL;
            send_WHOHAS_packet(sockfd, config);
            return;
        }
    }
}