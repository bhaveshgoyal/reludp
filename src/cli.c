#include "unp.h"
#include "unprtt.h"
#include <setjmp.h>

#define RTT_DEBUG
#define MAX_WSIZE 1024
int recv_window[MAX_WSIZE];
int curr_wsize = 4;

typedef struct buf_entry{
    int recvd;
    char data[MAXLINE];

}buf_entry;

static buf_entry recv_buf[MAX_WSIZE];

static struct rtt_info rttinfo;
static int rttinit = 0;
static struct msghdr msgsend, msgrecv; /* assumed init to 0 */
static struct hdr {
    uint32_t seq;
    uint32_t ts; // timestamp
    uint32_t cuml_ack;
    uint32_t adv; // Window Advertisement
    uint32_t last;

} sendhdr, recvhdr;


static void sig_alrm(int signo);
static sigjmp_buf jmpbuf;

static int eph_port_recv = 0;
static void sig_alrm(int signo)
{
    siglongjmp(jmpbuf, 1);
}

void clear_up(){
    int i;
    for(i = 0; i < MAX_WSIZE; i++){
        recv_window[i] = 0;
        recv_buf[i].recvd = 0;
        memset(recv_buf[i].data, 0, MAX_WSIZE);
    }
    curr_wsize = 4;
    memset(&sendhdr, 0, sizeof(sendhdr));
    memset(&recvhdr, 0, sizeof(recvhdr));

}
// REQ ->; EPH_NUM <-; ACK ->
int eph_cli_handshake(int sockfd, struct sockaddr_in *servaddr, char *serv_ip){

    int servlen = sizeof(*servaddr);

    char cmd[MAXLINE];
    strcat(cmd, "REQ");
    
    char eph_port[MAXLINE];

    int attempt = 1;
    struct timeval tv;

eph_recv:
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    Sendto(sockfd, cmd, strlen(cmd), 0, (SA *)servaddr, servlen);
    Setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int n = recvfrom(sockfd, eph_port, MAXLINE, 0, NULL, NULL);
    if (n < 0){
        if (errno == EWOULDBLOCK) {
            attempt++;
            if (attempt > 5)
                err_sys("Could not communicate with server");
            fprintf(stderr, "Recvfrom socket timeout. Retrying attempt %d\n", attempt);
            goto eph_recv;
        } else
            err_sys("Receiving eph port err while attempting handshake\n");

    }
    else
        eph_port[n] = 0;

//  Disable Timeout
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    Setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    int ephsock = Socket(AF_INET, SOCK_DGRAM, 0);
    
    bzero(servaddr, sizeof(*servaddr));
    (*servaddr).sin_family = AF_INET;
    (*servaddr).sin_port = htons((int) strtol(eph_port, (char **)NULL, 10));
    Inet_pton(AF_INET, serv_ip, &servaddr->sin_addr); 
    eph_port_recv = 1;

    struct sockaddr_in addrinfo;
    int addr_size = sizeof(addrinfo);
    getpeername(sockfd, (SA *)&addrinfo, &addr_size);
    fprintf(stdout, "\n\nClient bound at ephemeral port at server %s:%s\n\n", inet_ntoa(addrinfo.sin_addr), eph_port);
    fflush(stdout);
    
    Sendto(sockfd, "ACK", strlen("ACK"), 0, (SA *)servaddr, servlen);
    return ephsock;

}
int send_ackto_srv(int sockfd, SA *servaddr, socklen_t servaddrlen){

    struct iovec iovsend[1];
    sendhdr.cuml_ack = get_unacked();
    sendhdr.adv = curr_wsize;
    msgsend.msg_name = servaddr;
    msgsend.msg_namelen = servaddrlen;
    msgsend.msg_iov = iovsend;
    msgsend.msg_iovlen = 1;
    iovsend[0].iov_base = &sendhdr;
    iovsend[0].iov_len = sizeof(struct hdr);
//    iovsend[1].iov_base = resp;
  //  iovsend[1].iov_len = resplen;
        printf("Sending Cum Ack for %d\n", get_unacked());
    sendmsg(sockfd, &msgsend, 0);
}

int send_to_srv(int sockfd, char *resp, int resplen, SA *servaddr, socklen_t servaddrlen){
    struct iovec iovsend[2];
    sendhdr.seq++;
    msgsend.msg_name = servaddr;
    msgsend.msg_namelen = servaddrlen;
    msgsend.msg_iov = iovsend;
    msgsend.msg_iovlen = 2;
    iovsend[0].iov_base = &sendhdr;
    iovsend[0].iov_len = sizeof(struct hdr);
    iovsend[1].iov_base = resp;
    iovsend[1].iov_len = resplen;
    static int attempt = 1;
    Signal(SIGALRM, sig_alrm);
    rtt_newpack(&rttinfo);
sendagain:
    printf("Sending Command: %s Attempt %d RTO %0.3f\n", resp, attempt, rttinfo.rtt_rto);
    fflush(stdout);
    sendhdr.ts = rtt_ts(&rttinfo);
    sendmsg(sockfd, &msgsend, 0);
    alarm(rtt_start(&rttinfo));

    if (sigsetjmp(jmpbuf, 1) != 0) {
        if (rtt_timeout(&rttinfo) < 0) {
            err_msg("dg_send_recv: no response from server, giving up");
            rttinit = 0;
            errno = ETIMEDOUT;
            return (-1);
        }
        attempt += 1;
        goto sendagain;
    }

}
int get_unacked(){
    int i = 0;
    for(i = 0; i < MAX_WSIZE; i++){
        if (recv_window[i] == 0)
            return i;
    }
    return curr_wsize;
}
int recv_from_srv(int sockfd, char *recvline, SA *servaddr, socklen_t servaddrlen, int cmdacked){
    struct iovec iovrecv[2];
    msgrecv.msg_name = servaddr;
    msgrecv.msg_namelen = servaddrlen;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 2;
//    if (recvhdr.last == 1){
//        printf("Last packet received");
 //       fflush(stdout);
//    }
    msgrecv.msg_iovlen = 2;
    iovrecv[0].iov_base = &recvhdr;
    iovrecv[0].iov_len = sizeof(struct hdr);
    iovrecv[1].iov_base = recvline;
    iovrecv[1].iov_len = MAXLINE;
    int n = 0;
    do{
        n = recvmsg(sockfd, &msgrecv, 0);
        printf("%s\n", recvline);
  //      printf("\nReceiving %s %d %d %d %d %d\n", recvline, (int)sizeof(struct hdr), n, recvhdr.seq, curr_wsize, recvhdr.last);
        fflush(stdout);
        memset(recvline, 0, sizeof(recvline));
    } while (n < sizeof(struct hdr));
    
    if (cmdacked == 0){ //First Reply from server. Acts as ACK for command
        alarm(0);
        rtt_stop(&rttinfo, rtt_ts(&rttinfo) - recvhdr.ts);
    }

    recv_window[recvhdr.seq] = 1;
    int unacked = get_unacked();

    if (recvhdr.last == 1 || unacked < recvhdr.seq || (((recvhdr.seq+1) % curr_wsize) == 0)){
        if (recvhdr.last == 1){
            sendhdr.last = 1;
        }
        if (((recvhdr.seq+1) % curr_wsize) == 0){
            if (curr_wsize > MAX_WSIZE){
                printf("Max Window Size Reached");
                fflush(stdout);
            }
            else{
               printf("Doubling window size");
                curr_wsize <<= 2;
            }
        }
        send_ackto_srv(sockfd, servaddr, servaddrlen);
    }
    if (recvhdr.last == 1)
        clear_up();
}
int main(int argc, char **argv){
    int sockfd;
    struct sockaddr_in servaddr;

    bzero(&servaddr, sizeof(servaddr));
    
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);


    char sendline[MAXLINE], recvline[MAXLINE];
    int servlen = sizeof(servaddr);

    fd_set readfs;

    int maxfd = max(STDIN_FILENO, sockfd);
    
    printf("$> ");
    fflush(stdout);
    static int cmdacked = 0;
    clear_up();

    while(1){ 
        FD_ZERO(&readfs);
        FD_SET(STDIN_FILENO, &readfs);
        FD_SET(sockfd, &readfs);
        int maxfd = max(STDIN_FILENO, sockfd);
        int status = select(maxfd+1, &readfs, NULL, NULL, NULL);
        if (status < 0){
            fprintf(stderr, "error: Can't select on sockets");
            continue;
        }
        if (FD_ISSET(sockfd, &readfs)){
 //           printf("Select called: ");
 //           fflush(stdout);
            /*
            struct iovec iovrecv[2];
            msgrecv.msg_name = NULL;
            msgrecv.msg_namelen = 0;
            msgrecv.msg_iov = iovrecv;
            msgrecv.msg_iovlen = 2;
            iovrecv[0].iov_base = &recvhdr;
            iovrecv[0].iov_len = sizeof(struct hdr);
            iovrecv[1].iov_base = recvline;
            iovrecv[1].iov_len = MAXLINE;
            int n = 0;
            
            do{
                n = recvmsg(sockfd, &msgrecv, 0);
        printf("recieving %s %d %d %d %d", recvline, (int)sizeof(struct hdr), n, recvhdr.seq, sendhdr.seq);
        fflush(stdout);
            } while (n < sizeof(struct hdr) || recvhdr.seq != sendhdr.seq);
            alarm(0);
            rtt_stop(&rttinfo, rtt_ts(&rttinfo) - recvhdr.ts);
  */              
            recv_from_srv(sockfd, recvline, (SA *)&servaddr, sizeof(servaddr), cmdacked);
            if (cmdacked == 0)
                cmdacked = 1;
//            int n = Recvfrom(sockfd, recvline, MAXLINE, 0, NULL, NULL);
//            recvline[n] = 0;
            Fputs(recvline, stdout);
            fflush(stdout);
            memset(recvline, 0, sizeof(recvline));
        }
        else if (FD_ISSET(STDIN_FILENO, &readfs)){
            if(Fgets(sendline, MAXLINE, stdin) != NULL){
                if (sendline[0] == '\n' || sendline[0] == 0x0){
                    printf("$> ");
                    fflush(stdout);
                    continue;
                }
                if (eph_port_recv == 0){
                    if ((sockfd = eph_cli_handshake(sockfd, &servaddr , argv[1])) > 0){
                        eph_port_recv = 1;
                        rtt_init(&rttinfo);
                        rttinit = 1;
                        rtt_d_flag = 1;
                    }
                    else{
                        fprintf(stderr, "Something went wrong while doing handshake. Abort\n");
                        exit(0);
                    }
                }
                cmdacked = 0;
                
                send_to_srv(sockfd, sendline, strlen(sendline), (SA *)&servaddr, sizeof(servaddr));
                memset(sendline, 0, sizeof(sendline));
                //            Sendto(sockfd, sendline, strlen(sendline), 0, (SA *)&servaddr, sizeof(servaddr));

            }
        }
    }
        return 0;
}
