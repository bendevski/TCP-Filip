/*
 * Nabil Rahiman
 * NYU Abudhabi
 * email: nr83@nyu.edu
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>
#include <math.h>
#include "common.h"
#include "packet.h"

#define WINDOW_SIZE 1000
/*
 * You are required to change the implementation to support
 * window size greater than one.
 * In the current implementation window size is one, hence we have
 * only one send and receive packet
 */
tcp_packet *recvpkt;
tcp_packet *sndpkt;

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval; /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;
    /* 
     * check command line arguments 
     */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp  = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

    /* 
     * socket: create the parent socket 
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
     * us rerun the server immediately after we kill it; 
     * otherwise we have to wait about 20 secs. 
     * Eliminates "ERROR on binding: Address already in use" error. 
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
                sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");
    //creating an empty array and filling it with empty packets
    //as packets come in we will fill this array up with them
    //and use it as a window.
    tcp_packet *pile[WINDOW_SIZE];
    for(int i=0;i<WINDOW_SIZE;i++){
        pile[i] = make_packet(MSS_SIZE);
        pile[i]-> hdr.seqno = -1;
        pile[i]-> hdr.data_size = 0;
    }
    int next = 0; //the next sequence number that we are expecting

    clientlen = sizeof(clientaddr);
    while (1) {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        //VLOG(DEBUG, "waiting from server \n");
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *) buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        if ( recvpkt->hdr.data_size == 0) {
            //VLOG(INFO, "End Of File has been reached");
            fclose(fp);
            sndpkt = make_packet(0);
            sndpkt->hdr.ackno = -1;
            sndpkt->hdr.ctr_flags = ACK;
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                    (struct sockaddr *) &clientaddr, clientlen) < 0) {
                error("ERROR in sendto");
            }
            break;
        }
        VLOG(DEBUG, "%d", next);
        VLOG(DEBUG, "%d", recvpkt->hdr.seqno);
        
        //If we receive the packet we are waiting for
        if (next == recvpkt->hdr.seqno){
            /* 
            * sendto: ACK back to the client 
            */
            
            
            gettimeofday(&tp, NULL);
            VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
            sleep(0.1);
            fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
            next = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
            //sorting the packets stored by seqno
            for (int i=0; i<WINDOW_SIZE;i++){
                if (pile[i]->hdr.seqno==-1) break;
                for(int j=i;j<WINDOW_SIZE;j++){
                    if (pile[j]->hdr.seqno==-1) break;
                    
                    if (pile[j]->hdr.seqno < pile[i]->hdr.seqno){
                        tcp_packet* temp = pile[j];
                        pile[j] = pile[i];
                        pile[i] = temp;
                    }
                }
            }
            for (int i = 0; i<WINDOW_SIZE; i++){
                if (pile[i]->hdr.seqno==next){
                    //update next
                    next = pile[i]->hdr.seqno + pile[i]->hdr.data_size;
                    //write to file
                    fseek(fp, pile[i]->hdr.seqno, SEEK_SET);
                    fwrite(pile[i]->data, 1, pile[i]->hdr.data_size, fp);
                }
                pile[i]->hdr.seqno = -1;
                pile[i]->hdr.data_size = 0;
                if (pile[i]->hdr.seqno != next) break;
            }
        }
        //if it's not the packet we are expecting
        else if (next < recvpkt->hdr.seqno){
            int dup =0;
            for (int i = 0; i < WINDOW_SIZE; i++){
                if (pile[i]->hdr.seqno==-1){
                    break;
                }
                if (pile[i]->hdr.seqno==recvpkt->hdr.seqno){
                    dup=1;
                    break;
                }
            }
            if (!dup){
                //add the pckt to the buffer
                for (int i=0;i<WINDOW_SIZE;i++){
                    if (pile[i]->hdr.seqno==-1){
                        //just in case
                        //memset(pile[i], 0, MSS_SIZE);
                        memcpy(pile[i], recvpkt, MSS_SIZE);
                        break;
                    }
                }
            }
        }
        sndpkt = make_packet(0);
        sndpkt->hdr.ackno = next;
        sndpkt->hdr.ctr_flags = ACK;
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                (struct sockaddr *) &clientaddr, clientlen) < 0) {
            error("ERROR in sendto");
        }
        
    }

    return 0;
}