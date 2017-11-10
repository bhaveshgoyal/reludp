#include "unp.h"
#include "unpifi.h"
#include <dirent.h>
#include "unprtt.h"
#include <unistd.h>
#include <stdlib.h>
#include <setjmp.h>
#define RTT_DEBUG
#define MAX_WSIZE 1024
#define SHARED_PATH "./"
#define FTRANS_TTH 2 //ssth
#define ADDI_PAR 4
#define MULTD_FACT 0.5
#define SSTH 0

//char SHARED_PATH[1024];
//int MAX_WSIZE = 0;

static int curr_wsize = 4; //cwnd
static int frame_burst = 1;
static int winc_val = 0;

static int timer_running = 0;

typedef struct buf_entry{
    int sent;
    struct timeval tv;
    char data[MAXLINE];
    struct rtt_info rttinfo;
}buf_entry;

static buf_entry send_buf[MAX_WSIZE];

static struct rtt_info rttinfo;
static int rttinit = 0;
static struct msghdr msgsend, msgrecv; /* assumed init to 0 */

static struct pkt_hdr {
    uint32_t seq;
    uint32_t ts; // timestamp
    uint32_t cuml_ack;
    uint32_t adv;
    uint32_t last;
}sendhdr, recvhdr;

typedef struct t_args {
    int sockfd;
    struct timeval timeout;
    SA * cliadd;
    socklen_t cliaddlen;
    char *resp;
    int resplen;
    int seq;

}t_args;

int cuml_buf[MAX_WSIZE];
/*
const int DGRAM_SIZE = 512 - sizeof(struct pkt_hdr);

struct packet {
    struct pkt_hdr hdr;
    char body[DGRAM_SIZE];
}
*/
static void sig_alrm(int signo);
static sigjmp_buf jmpbuf;

void clear_up(){
    int i = 0;
    for(i = 0;i < MAX_WSIZE; i++){
        send_buf[i].sent = 0;
        memset(send_buf[i].data, 0, MAX_WSIZE);
    }
    curr_wsize = 4;
    memset(&sendhdr, 0, sizeof(sendhdr));
    memset(&recvhdr, 0, sizeof(recvhdr));
}
int get_frameburst(){
    return frame_burst;
}
int get_wsize(){
    return curr_wsize;
}
int get_woffset(){
    int i = 0;
    for(i = 0; i < MAX_WSIZE; i++){
        if (send_buf[i].sent == 0)
            return i;
    }

}
int get_cumloffset(){
    int i = 0;
    for(i = 0; i < MAX_WSIZE; i++){
        if (cuml_buf[i] == 0)
            return i;
    }

}

int set_cumloffset(int offset){
    int i = 0;
    for(i = 0; i < offset; i++){
        cuml_buf[i] == 1;
    }

}

int eph_serv_handshake(int sockfd, struct sockaddr_in *cliaddr){

    int len = sizeof(*cliaddr);
    struct sockaddr_in ephaddr;
    int eph_sock = Socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&ephaddr, sizeof(ephaddr));
    ephaddr.sin_family = AF_INET;
    ephaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    ephaddr.sin_port = htons(0);

    Bind(eph_sock, (SA *)&ephaddr, sizeof(ephaddr));


    struct sockaddr_in echoaddr;
    int addr_size = sizeof(echoaddr);
    getsockname(eph_sock, (SA *)&echoaddr, &addr_size);
    printf("New UDP client Request: Binding to server at %s:%d\n", inet_ntoa(echoaddr.sin_addr), ntohs(echoaddr.sin_port));
    fflush(stdout);
    
    char ephport_res[MAXLINE];
    sprintf(ephport_res, "%d", ntohs(echoaddr.sin_port));

    struct timeval tv;
    int attempt = 1;

recv_ack:
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    Setsockopt(eph_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    Sendto(eph_sock, ephport_res, strlen(ephport_res), 0, (SA *)cliaddr, len);
    char msg[MAXLINE];
    int r = recvfrom(eph_sock, msg, MAXLINE, 0, (SA *)cliaddr, &len);
    if (r < 0){
        if (errno == EWOULDBLOCK) {
            attempt++;
            if (attempt > 5){
                 err_sys("ServerTimeout: Could not communicate with client\n");  
                Sendto(sockfd, ephport_res, strlen(ephport_res), 0, (SA *)cliaddr, len);
                Sendto(eph_sock, ephport_res, strlen(ephport_res), 0, (SA *)cliaddr, len);
            }
            fprintf(stderr, "Recvfrom socket timeout while fetching ACK. Retrying attempt #%d\n", attempt-1);
            goto recv_ack;
        } else
            err_sys("Recvfrom err while attempting handshake\n");
    }
   
    // Disable Timeout
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    Setsockopt(eph_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (strstr(msg, "ACK"))
        return eph_sock;

    // Unsolicitated , unknown reply recieved
    return -1;
}
static void sig_alrm(int signo)
{
    siglongjmp(jmpbuf, 1);
}

void *recv_handler(void *connParams){
    t_args *args = connParams;
    fd_set readfs;
    int sockfd = (*(args)).sockfd;
    struct timeval timeout = (*(args)).timeout;

    SA * cliaddr = (*(args)).cliadd;
    socklen_t cliaddrlen = (*(args)).cliaddlen;
    char *resp = (*(args)).resp;
    int resplen = (*(args)).resplen;
    int seq = (*(args)).seq;
    printf("Thread seq:  %d %s %d %d\n", sockfd, resp, resplen, (*(args)).seq);
    fflush(stdout);
    struct iovec iovsend[2];
    struct rtt_info seqrttinfo = send_buf[seq].rttinfo;
    iovsend[0].iov_base = &sendhdr;
    iovsend[0].iov_len = sizeof(struct pkt_hdr);
    iovsend[1].iov_base = resp;
    iovsend[1].iov_len = resplen;

    while(1){

        FD_SET(sockfd, &readfs);
        printf("\nTimer INIT for: %d, RTO: %lf\n", seq, send_buf[seq].rttinfo.rtt_rto);
        int status = select(sockfd + 1, &readfs, NULL, NULL, &timeout);
        if (status < 0) {
            printf("\nStatus = %d, Unable to monitor sockets !!! Exiting ...",status);
            return 0;
        }

        if (FD_ISSET(sockfd, &readfs)){
            recv_ackfrom_cli(sockfd, cliaddr, cliaddrlen);
            break;
        }
        else{ //Retransmit Packet. Timeout Occured
            if (rtt_timeout(&(send_buf[seq].rttinfo)) < 0) {
                err_msg("dg_send_recv: no response from server, giving up");
                rttinit = 0;
                errno = ETIMEDOUT;
                curr_wsize *= MULTD_FACT;
                return (0);
            }

            msgsend.msg_name = cliaddr;
            msgsend.msg_namelen = cliaddrlen;
            msgsend.msg_iov = iovsend;
            msgsend.msg_iovlen = 2;
            iovsend[0].iov_base = &sendhdr;
            iovsend[0].iov_len = sizeof(struct pkt_hdr);
            iovsend[1].iov_base = resp;
            iovsend[1].iov_len = resplen;

            sendhdr.seq = seq;
            sendhdr.ts = rtt_ts(&(send_buf[seq].rttinfo));
            if (sendmsg(sockfd, &msgsend, 0) < 0)
                printf("Could not Send PACKET %d %s\n", sockfd, strerror(errno));

            timeout.tv_sec = send_buf[seq].rttinfo.rtt_rto;
            timeout.tv_usec = send_buf[seq].rttinfo.rtt_rto*1000000;
        }

    }
    free(connParams);
    pthread_exit(0);
}
int recv_cmdfrom_cli(int sockfd, SA *cliaddr, socklen_t cliaddrlen, char *recvline){
    struct iovec iovrecv[2];
    msgrecv.msg_name = cliaddr;
    msgrecv.msg_namelen = cliaddrlen;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 2;
    iovrecv[0].iov_base = &recvhdr;
    iovrecv[0].iov_len = sizeof(struct pkt_hdr);
    iovrecv[1].iov_base = recvline;
    iovrecv[1].iov_len = MAXLINE;
    //       do{
    recvmsg(sockfd, &msgrecv, 0);
    //        int acked = recvhdr.cuml_ack;
    //        int wsize_cli = recvhdr.adv;
    printf("recieving cmd: %s\n", recvline);
    fflush(stdout);
    //        }while(n < sizeof(struct hdr));
    //        alarm(0);
    //        rtt_stop(&rttinfo, rtt_ts(&rttinfo) - recvhdr.ts);
}

int recv_ackfrom_cli(int sockfd, SA *cliaddr, socklen_t cliaddrlen){
    char *recvline;
    struct iovec iovrecv[2];
    msgrecv.msg_name = cliaddr;
    msgrecv.msg_namelen = cliaddrlen;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 2;
    iovrecv[0].iov_base = &recvhdr;
    iovrecv[0].iov_len = sizeof(struct pkt_hdr);
    iovrecv[1].iov_base = recvline;
    iovrecv[1].iov_len = MAXLINE;
    
    printf("Waiting for Ack\n"); 
    fflush(stdout);
    int n = 0;
    do{
        n = recvmsg(sockfd, &msgrecv, 0);
    }while(n < sizeof(struct pkt_hdr));

    rtt_stop(&rttinfo, rtt_ts(&(send_buf[recvhdr.seq].rttinfo)) - recvhdr.ts);
    rtt_stop(&(send_buf[recvhdr.seq].rttinfo), rtt_ts(&(send_buf[recvhdr.seq].rttinfo)) - recvhdr.ts);
    
//    timer_running = 0;
    int acked = recvhdr.cuml_ack;
    set_cumloffset(acked);
    int wsize_cli = recvhdr.adv;
    printf("Acked Till %d. Curr Window: %d. Curr window Offset %d\n", acked, curr_wsize, get_woffset());

    if (recvhdr.last == 1){
//        alarm(0);
//        rtt_stop(&rttinfo, rtt_ts(&rttinfo) - recvhdr.ts);
        printf("Clearing up headers. Fin done\n");
        fflush(stdout);
//        clear_up();    
        return;
    }
//    if (acked < get_woffset() || curr_wsize > recvhdr.adv){ //Resend the next to last received seq by client
//        curr_wsize *= MULTD_FACT;
//        if (curr_wsize > SSTH)
//            curr_wsize += 1;
//    }// Reduce Window Size
        
    if (acked < get_woffset()){
        if (send_buf[acked].sent > FTRANS_TTH){ //Fast Retransmit
            printf("Resending frame %d Data: %s\n", acked, send_buf[acked].data);
            send_to_cli(sockfd, send_buf[acked].data, strlen(send_buf[acked].data), acked, cliaddr, cliaddrlen, 1);
        }
        else
            send_buf[acked].sent++;
    }
    else if (acked == curr_wsize) { //Everything in current window received
        if (curr_wsize+ADDI_PAR > MAX_WSIZE ) //TODO Increase window size slowly after reaching threshold
            printf("Max Window Size Reached for Server\n");
        else{
            curr_wsize += ADDI_PAR;
            winc_val = ADDI_PAR;
            printf("Window size increased to %d\n", curr_wsize);
        }
    }
    else if (acked > get_woffset()){
        printf("Oh Snap! Ack Greater than current window Ack %d wsize %d\n", acked, get_woffset());
    }
    
    if (acked != get_woffset()){
    printf("Init Timer from recv at seq %d\n", recvhdr.cuml_ack);
    fflush(stdout);
    timer_running = 1;
    t_args *connParams = (t_args *)malloc(sizeof(t_args));
    
    struct timeval timeout;
    timeout.tv_sec = rttinfo.rtt_rto;
    timeout.tv_usec = rttinfo.rtt_rto*1000000;
    
    connParams->sockfd = sockfd;
    connParams->timeout = timeout;
    connParams->cliadd = cliaddr;
    connParams->cliaddlen = cliaddrlen;
    printf("SEQUENCE HEADER: %d\n", recvhdr.cuml_ack);
    connParams->seq = recvhdr.cuml_ack;
    connParams->resp = send_buf[recvhdr.cuml_ack-1].data;
    connParams->resplen = strlen(send_buf[recvhdr.cuml_ack-1].data);
    pthread_t tid;
    if (pthread_create(&tid, NULL, (void *)recv_handler, (void*)connParams) != 0){
        err_sys("Could not create server Thread. Abort\n");
    }
    pthread_detach(tid);
    }
    fflush(stdout);
    //    alarm(0);
//    rtt_stop(&rttinfo, rtt_ts(&rttinfo) - recvhdr.ts);
}
/*
int recv_from_cli(int sockfd, char *recvline, SA *cliaddr, socklen_t cliaddrlen){
    struct iovec iovrecv[2];
    msgrecv.msg_name = cliaddr;
    msgrecv.msg_namelen = cliaddrlen;
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
        }
        */
int send_to_cli(int sockfd, char *resp, int resplen, int seq, SA *cliaddr, socklen_t cliaddrlen, int timer){

    struct timeval  timeout;

    struct iovec iovsend[2];

    rtt_newpack(&(send_buf[seq].rttinfo));

    msgsend.msg_name = cliaddr;
    msgsend.msg_namelen = cliaddrlen;
    msgsend.msg_iov = iovsend;
    msgsend.msg_iovlen = 2;
    iovsend[0].iov_base = &sendhdr;
    iovsend[0].iov_len = sizeof(struct pkt_hdr);
    iovsend[1].iov_base = resp;
    iovsend[1].iov_len = resplen;

    sendhdr.seq = seq;
    sendhdr.ts = rtt_ts(&(send_buf[seq].rttinfo));

    sendmsg(sockfd, &msgsend, 0);

    timeout.tv_sec = rttinfo.rtt_rto;
    timeout.tv_usec = rttinfo.rtt_rto*1000000;

    send_buf[seq].tv = timeout;
    //    Signal(SIGALRM, sig_alrm);
    t_args *connParams = (t_args *)malloc(sizeof(t_args));
    
    connParams->sockfd = sockfd;
    connParams->timeout = timeout;
    connParams->cliadd = cliaddr;
    connParams->cliaddlen = cliaddrlen;
    connParams->seq = seq;
    connParams->resp = resp;
    connParams->resplen = resplen;

    fd_set      readfs;
    if (timer_running == 0){
        printf("Init Timer at seq %d data %s\n", seq, connParams->resp);
        fflush(stdout);
        timer_running = 1;
        pthread_t tid;
        if (pthread_create(&tid, NULL, (void *)recv_handler, (void*)connParams)!=0){
            err_sys("Could not create Timer Thread. Abort\n");
        }
        pthread_detach(tid);
    }
}
int main(int argc, char **argv){
    int sockfd;
    const int on = 1;
    struct sockaddr_in *servaddr, cliaddr;
    struct ifi_info *ifi, *ifihead;

//    fileio("server.in");

    for (ifihead = ifi = Get_ifi_info(AF_INET, 1); ifi != NULL;ifi=ifi->ifi_next){
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    
    Setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr = (struct sockaddr_in *) ifi->ifi_addr;
    servaddr->sin_family = AF_INET;
    servaddr->sin_port = htons(SERV_PORT);
    Bind(sockfd, (SA *) servaddr, sizeof(*servaddr));

    printf("Server bound at interface %s\n", Sock_ntop((SA *) servaddr, sizeof(*servaddr)));
    
    }
    
    int len = sizeof(cliaddr);

    char msg[MAXLINE];

    struct iovec iovsend[2];
    while(1){
        int n = Recvfrom(sockfd, msg, MAXLINE, 0, (SA *)&cliaddr, &len);
        int pid = fork();
        if (pid == 0){
            char command[MAXLINE] = {0};
            printf("Received: %s\n. Trying Ephemeral Handshake", msg);
            int eph_sock = eph_serv_handshake(sockfd, &cliaddr);
            if (eph_sock > 0){
                struct sockaddr_in echoaddr;
                int addr_size = sizeof(echoaddr);
                getsockname(eph_sock, (SA *)&echoaddr, &addr_size);
                rtt_init(&rttinfo);
                rttinit = 1;
                rtt_d_flag = 1;
                
                msgsend.msg_name = (SA *)&cliaddr;
                msgsend.msg_namelen = len;
                msgsend.msg_iov = iovsend;
                msgsend.msg_iovlen = 2;

                printf("Handshake established at %s:%d\n", inet_ntoa(echoaddr.sin_addr), ntohs(echoaddr.sin_port));
                fflush(stdout);
                close(sockfd);
            }
            else{
                fprintf(stderr, "err: Something unknown happened while establishing UDP ephemeral handshake. Abort\n");
                exit(0);
            }

            while(1){
                printf("\nWaiting to recv\n");
                fflush(stdout);
                recv_cmdfrom_cli(eph_sock, (SA *)&cliaddr, len, command);
                clear_up();
                if (strstr(command, "list")){
                    printf("list issued");
                    fflush(stdout);
                    DIR *p_dir = opendir(SHARED_PATH);
                    char resp[MAXLINE] = {0};
                    if (p_dir == NULL){
                        strcpy(resp, "Can't open present directory for reading");
                        fprintf(stderr, "%s", resp);
                        Sendto(eph_sock, resp, strlen(resp), 0, (SA *)&cliaddr, len);
                    }
                    else{
                        struct dirent *pDirent;
                        int sent_frames = 0;
                        pDirent = readdir(p_dir);
                        int last = 0;
                        while (pDirent != NULL) {
                            
                            struct dirent *nextDirent = readdir(p_dir);
                            if (nextDirent == NULL){
                                sendhdr.last = 1;
                            }
                            else
                                sendhdr.last = 0;
                            
                            
                            send_buf[sent_frames].sent = 1;
                            memset(resp, 0, MAXLINE); 
                            strcpy(resp, strcat(pDirent->d_name, ""));
                            strcpy(send_buf[sent_frames].data, strcat(pDirent->d_name, ""));
                            rtt_init(&(send_buf[sent_frames].rttinfo));
                            printf("Sending: %s Index %d w_size  %d WindowOffset %d\n", resp, sent_frames, curr_wsize, get_woffset());
                            //                      if (sent_frames >= curr_wsize-1)
                            frame_burst++;
                            send_to_cli(eph_sock, resp, strlen(resp), sent_frames, (SA *)&cliaddr, len, 1);
                            
                            while (get_frameburst() == winc_val && get_cumloffset() != get_woffset()){
                            printf("Waiting on while %d %d\n", sent_frames, curr_wsize-1);
                           //     frame_burst = 0;
                       //         yield();
                            }
                            if (get_frameburst() == winc_val)
                                frame_burst = 0;
                            //                     else
                            //                       send_to_cli(eph_sock, resp, strlen(resp), sent_frames, (SA *)&cliaddr, len, 0);

               //             printf("Out of while recvhdr.cuml_ack %d %d", recvhdr.cuml_ack, get_woffset());
                            //             if (sent_frames >= curr_wsize-1 || sent_frames >= recvhdr.adv-1 || nextDirent == NULL)
               //                 recv_ackfrom_cli(eph_sock, (SA *)&cliaddr, len);
                            sent_frames++;
                            pDirent = nextDirent;
                        }
                        //         recv_ackfrom_cli(eph_sock, (SA *)&cliaddr, len);


                        closedir (p_dir);
                    }
                }
                else if (strstr(command, "download")){
                    printf("download issued");
                    printf("%s", command);
                    fflush(stdout);
                    char *d_args = strtok(command, " ");
                    d_args = strtok(NULL, " \n");
                    char path[MAXLINE];
                    strcpy(path, SHARED_PATH);
                    strcat(path, d_args);
                    int fd = open(path, O_RDONLY);
                    if (fd < 0){
                        char *msg = "Error Downloading file from server"; 
                        Sendto(eph_sock, msg, strlen(msg), 0, (SA *) &cliaddr, len);
                        continue;
                    }
                    char file_buf[MAXLINE];
                    int n;
                    while ((n = read(fd, file_buf, MAXLINE)) > 0) {
                        iovsend[0].iov_base = &sendhdr;
                        iovsend[0].iov_len = sizeof(struct pkt_hdr);
                        iovsend[1].iov_base = file_buf;
                        iovsend[1].iov_len = n;
                        sendmsg(eph_sock, &msgsend, 0);
                    //    send_to_cli(eph_sock, file_buf, n, (SA *)&cliaddr, len);
                 //       Sendto(eph_sock, file_buf, n, 0, (SA *) &cliaddr, len);
                    }
                    sendhdr.seq++;
                }
                else
                        send_to_cli(eph_sock, command, n, 0, (SA *)&cliaddr, len, 0);
                //    Sendto(eph_sock, command, n, 0, (SA *)&cliaddr, len);
                memset(command, 0, sizeof(command));
            }
        }

    }

    return 0;
}
/*int fileio(char *fname){

  FILE *confd = fopen(fname, "r");
    char buf[MAXLINE];
    char port[MAXLINE];

    char **lines = malloc(sizeof(*lines) * alloc_size);
    
    int lineIdx = 0;

    while (fgets(buf, sizeof(buf), confd) != NULL) {
        
        if (lineIdx == 0){
            port = atoi(buf);
        }
        else if (lineIdx == 1){
            MAX_WSIZE = atoi(buf);
        }
        else if (lineIdx == 2){
            strcpy(SHARED_PATH, buf);
        }
        lineIdx++;
    }
}*/
