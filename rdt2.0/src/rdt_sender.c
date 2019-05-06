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
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <math.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120 //millisecond 
#define MAX_WINDOW_SIZE 1000

typedef struct {
    struct node* next;
    tcp_packet* pack;
    int num;
} node;

node* makeQueue(){
    node* my = malloc(16);
    my->next = NULL;
    my->pack = NULL;
    my->num = 0;
}
void pop(node* head){
    if (!head->next) return;
    node* tmp = head->next;
    node* tmp2 = tmp->next;
    free(tmp);
    head->next = tmp2;
    head->num--;
}
void push(node* head, tcp_packet* new){
    node* x = head;
    while(x->next!= NULL){
        x = x->next;
    }
    x->next = malloc(10);
    x = x->next;
    x->next = NULL;
    x->pack = new;
    head->num++;
}
int update(node* head, int seqno){
    int count = 0;
    node* x = head->next;
    while (x->pack->hdr.seqno!=seqno){
        if (x->next==NULL){
            return count;
        }
        count++;
        pop(head);
        
    }
    return count;
}
tcp_packet* get(node*head){
    node* tmp = head->next;
    return tmp->pack;
}
int next_seqno=0;
int send_base=0;
//int window_size = 1;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;       
tcp_packet *send_window[MAX_WINDOW_SIZE];
int curWin = 4;
void end(int sockfd){
    tcp_packet* sndpkt = make_packet(0);
    if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }
}

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        VLOG(INFO, "Timeout happened");
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
    }
}


void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for resending unacknowledged packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}


int main (int argc, char **argv)
{
    int portno, len;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;
    FILE *csv_fp;


    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol
    int base_index=0;
    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    int start = 0;
    int khalas = 0;
    int segger = 0;
    int dup = 0; //Dup counter
    int curDup = -1; //current duplicate
    clock_t cStart = clock();        
    csv_fp = fopen("CWND.csv", "w");
    node* head = makeQueue();


    while (1)
    {
        sleep(0.1); // just to lighten up server load
        //filling up the window
        fprintf( csv_fp, "%llu, %i\n", (long long unsigned int)clock(), curWin);

        do{
            len = fread(buffer, 1, DATA_SIZE, fp);
            if ( len <= 0 || khalas)
            {
                khalas=1;
                VLOG(INFO, "End Of File has been reached");
                sndpkt = make_packet(0);
                sndpkt ->hdr.data_size = 0;
                sndpkt ->hdr.seqno = -1;
                push(head, sndpkt);
                head->pack = sndpkt;
                break;
            }
            send_base = next_seqno;
            next_seqno = send_base + len;
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = send_base;
            send_base+=len;
            push(head, (tcp_packet*) sndpkt);
            start++;
            start%=(curWin);
        } while (head->num<curWin);
        tcp_packet* cur = get(head);
        VLOG(DEBUG, "%d", cur->hdr.seqno);
        //Wait for ACK
        do {
            // VLOG(DEBUG, "Sending packet %d to %s", 
            //         send_base, inet_ntoa(serveraddr.sin_addr));
            /*
             * If the sendto is called for the first time, the system will
             * will assign a random port number so that server can send its
             * response to the src port.
             */
            //sending everything in the send window
            node* x = head;
            do {
                x = x->next;
                if(sendto(sockfd, x->pack, TCP_HDR_SIZE + get_data_size(x->pack), 0, 
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }
                
            } while(x->next!=NULL);
            
            start_timer();
            //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
            //struct sockaddr *src_addr, socklen_t *addrlen);

            if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                        (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)buffer;
            assert(get_data_size(recvpkt) <= DATA_SIZE);
            //checking if were getting duplicate acks
            if (curDup == recvpkt->hdr.ackno){
                dup++;
            }
            else{
            curDup = recvpkt->hdr.ackno;
            dup = 0;
            }
            //if we hit 4 dups reduce the window size
            if (dup == 4){
                dup = 0;
                if (curWin/2 >= 4) curWin /= 2;
            }
            // //timeout checker
            if (((clock()-cStart)/CLOCKS_PER_SEC) >= 3){
                if (curWin/2 >= 4) curWin /= 2;
            }
            stop_timer();
            /*resend pack if don't recv ack */
            
        } while(recvpkt->hdr.ackno <= cur->hdr.seqno);
        cStart = clock();
        
        int x = update(head, recvpkt->hdr.ackno);
        curWin+=x;
        
        if (head->pack){
            if(sendto(sockfd, get(head), TCP_HDR_SIZE + get_data_size(get(head)), 0, 
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }
            return 0;
        }
    }
    
    return 0;

}



