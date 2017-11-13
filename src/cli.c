#include "unp.h"
#include "unprtt.h"
#include <math.h>
#include <setjmp.h>
#define RTT_DEBUG
#define SHARED_PATH "./"

static char SERVER_IP[MAXLINE];
static int SERVER_PORT = 9877;
static int seed = 41;
static float PROB_LOSS = 0.2;
static int MAX_WSIZE = 1024;
static int delay = 20;

int curr_wsize = 4;

int last_pktidx = -1;

typedef struct buf_entry{
    int recvd;
    int dirty;
    char data[MAXLINE];

}buf_entry;

static buf_entry *recv_buf;
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

int calc_drop_prob(){
    double prob = rand()/(double)RAND_MAX;
//    printf("PACKET PROB: %lf %lf", prob, PROB_LOSS);
    if (prob < PROB_LOSS){
//    printf("Dropping Packet\n");
        return 1;
    }
    return 0;
//    return ((srand(seed)/(double)RAND_MAX) < PROB_LOSS) ? 0 : 1;

}
static void sig_alrm(int signo);
static sigjmp_buf jmpbuf;

static int eph_port_recv = 0;
static void sig_alrm(int signo)
{
    siglongjmp(jmpbuf, 1);
}

void clear_up(){
    int i;
    srand(seed);
    for(i = 0; i < MAX_WSIZE; i++){
        recv_buf[i].recvd = 0;
        recv_buf[i].dirty = 0;
        memset(recv_buf[i].data, 0, MAXLINE);
    }
    curr_wsize = 4;
    last_pktidx = -1;
    memset(&sendhdr, 0, sizeof(sendhdr));
    memset(&recvhdr, 0, sizeof(recvhdr));

}
int check_clear_up(){
    int i = 0;
    for(i = 0;i < MAX_WSIZE; i++){
        if (recv_buf[i].recvd == 0)
            break;
    }
    if (i-1 == last_pktidx){
        printf("Check successful %d %d\n", i-1, last_pktidx);
        fflush(stdout);
    }
    return (i-1 == last_pktidx) ? 1 : 0;
}
void *read_buf(void* out_fds){

    int fds = *((int *)out_fds);
    while(1){
    int i = 0;
    for(i = 0; i < MAX_WSIZE; i++){
        if (recv_buf[i].recvd == 1 && recv_buf[i].dirty == 0){
            if (write(fds, recv_buf[i].data, strlen(recv_buf[i].data)) < 0){
                fprintf(stderr, "Could not write to Output File Descriptor. Abort\n");
            }
   //         fflush(fds);
            recv_buf[i].dirty = 1;
        }
    }
    if (last_pktidx >= 0 && (check_clear_up() == 1)){
        clear_up();
        printf("All Server data received. Press ENTER to execute Next Command.\n");
        fflush(stdout);
        if (fds != 1)
            close(fds);
        break;
    }
    float random = ((float)rand()/RAND_MAX);
    float sleep_time = pow(M_E, (-1*delay*log(random)));
    sleep(5);
    }
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
int send_ackto_srv(int sockfd, SA *servaddr, socklen_t servaddrlen, int recv_seq){

    struct iovec iovsend[1];
    sendhdr.cuml_ack = get_unacked();
    sendhdr.adv = curr_wsize;
    sendhdr.seq = recv_seq;
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
        if (recv_buf[i].recvd == 0)
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
//        printf("Received %s ", recvline);
        strcpy(recv_buf[recvhdr.seq].data, recvline);
        //      printf("\nReceiving %s %d %d %d %d %d\n", recvline, (int)sizeof(struct hdr), n, recvhdr.seq, curr_wsize, recvhdr.last);
        fflush(stdout);
        memset(recvline, 0, sizeof(recvline));
    } while (n < sizeof(struct hdr));
    if (calc_drop_prob() == 1)
        return;

    if (cmdacked == 0){ //First Reply from server. Acts as ACK for command
        cmdacked = 1;
        alarm(0);
        rtt_stop(&rttinfo, rtt_ts(&rttinfo) - recvhdr.ts);
    }

    // Print msg if the buffer is full
    if (recvhdr.seq == MAX_WSIZE){
        printf("Client Buffer Full. Program behaviour might get disrupted\n");
        fflush(stdout);
    }
    recv_buf[recvhdr.seq].recvd = 1;
    int unacked = get_unacked();

    //    if (unacked < recvhdr.seq || recvhdr.seq+1 == curr_wsize){
    if (recvhdr.last != -1){
        printf("Last packet index received at %d\n", recvhdr.last);
        sendhdr.last = 1;
        last_pktidx = recvhdr.last;
        fflush(stdout);
    }
    send_ackto_srv(sockfd, servaddr, servaddrlen, recvhdr.seq);
    //    }
    /*    else if (recvhdr.seq+1 == curr_wsize){
          if (curr_wsize + get_unacked() > MAX_WSIZE){
          printf("Max Window Size Reached");
          fflush(stdout);
          }
          else{
          printf("Shifting client window to %d", curr_wsize + get_unacked());
          curr_wsize += get_unacked();
          }
          send_ackto_srv(sockfd, servaddr, servaddrlen);

          }
          */  
//    if (recvhdr.last == 1)
//        clear_up();
}
int main(int argc, char **argv){
    int sockfd;
    struct sockaddr_in servaddr;

    if (fileiocli("client.in") == 0){
        fprintf(stderr, "error: Not able to read config file client.in");
        exit(0);
    }
    recv_buf = (buf_entry *)malloc(sizeof(buf_entry)*MAX_WSIZE);
    bzero(&servaddr, sizeof(servaddr));
    
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    Inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr);
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);

    //Create REad Thread;
    //
    char sendline[MAXLINE], recvline[MAXLINE];
    int servlen = sizeof(servaddr);

    fd_set readfs;

    int maxfd = max(STDIN_FILENO, sockfd);
    
    printf("$> ");
    fflush(stdout);
    static int cmdacked = 0;
    clear_up();
    
    int out_des = 1;

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
            //

            recv_from_srv(sockfd, recvline, (SA *)&servaddr, sizeof(servaddr), cmdacked);
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
                if (strstr(sendline, "quit\n")){
                    printf("Closing all open sockets and terminating threads. Bye!\n");
                    fflush(stdout);
                    close(sockfd);
                    clear_up();
                    exit(0);
                
                }
                if (eph_port_recv == 0){
                    if ((sockfd = eph_cli_handshake(sockfd, &servaddr , SERVER_IP)) > 0){
                        eph_port_recv = 1;
                    }
                    else{
                        fprintf(stderr, "Something went wrong while doing handshake. Abort\n");
                        exit(0);
                    }
                }

                cmdacked = 0;
                if (strstr(sendline, ">")){
                    char copy_cmd[MAXLINE];
                    strcpy(copy_cmd, sendline);
                    char *d_args = strtok(copy_cmd, " ");
                    d_args = strtok(NULL, " \n");
                    d_args = strtok(NULL, " \n");
                    d_args = strtok(NULL, " \n");
                    char path[MAXLINE];
                    strcpy(path, SHARED_PATH);
                    strcpy(path, d_args);
                    printf("Output file Path: %s\n", path);

                    int fd = open(path, O_CREAT | O_WRONLY, 0664);
                    if (fd < 0){
                        fprintf(stderr, "Could not Open file at %s for writing\n",path);
                        return;
                    }
                    else
                        out_des = fd;
                    fflush(stdout);
                }
                
                rtt_init(&rttinfo);
                rttinit = 1;
                rtt_d_flag = 1;
                
                // Start Recv Buffer Thread Readr
                pthread_t tid_readr; 
                if (pthread_create(&tid_readr, NULL, read_buf, (void *)&out_des) < 0){
                    printf("Could not create Recvbuffer thread listener\n. Abort");
                    fflush(stdout);
                    return;
                }
                pthread_detach(tid_readr);
                send_to_srv(sockfd, sendline, strlen(sendline), (SA *)&servaddr, sizeof(servaddr));
                memset(sendline, 0, sizeof(sendline));
                //            Sendto(sockfd, sendline, strlen(sendline), 0, (SA *)&servaddr, sizeof(servaddr));

            }
        }
    }
    return 0;
}
int fileiocli(char *fname){

  FILE *confd = fopen(fname, "r");

  if (confd == NULL){
    return 0;
  }

    char buf[MAXLINE];
    int lineIdx = 0;
    printf("\n=============================");
    printf("\nLoading server configuration:\n");
    while (fgets(buf, MAXLINE, confd) != NULL) {

        if (lineIdx == 0){
            buf[strlen(buf)-1] = 0;
            strcpy(SERVER_IP, buf);
            printf("Server IP: %s\n", SERVER_IP);
        }
        else if (lineIdx == 1){
            buf[strlen(buf)-1] = 0;
            SERVER_PORT = atoi(buf);
            printf("Server Port: %d\n", SERVER_PORT);
        }
        else if (lineIdx == 2){
            buf[strlen(buf)-1] = 0;
            seed = atoi(buf);
            printf("srand seed: %d\n", seed);
        }
        else if (lineIdx == 3){
            buf[strlen(buf)-1] = 0;
            PROB_LOSS = atof(buf);
            printf("Loss Probability: %lf\n", PROB_LOSS);
        }
        else if (lineIdx == 4){
            buf[strlen(buf)-1] = 0;
            MAX_WSIZE = atoi(buf);
            printf("Max Window Size: %d\n", MAX_WSIZE);
        }
        else if (lineIdx == 5){
            buf[strlen(buf)-1] = 0;
            delay = atoi(buf);
            printf("Delay in ms: %d\n", delay);
        }
        lineIdx++;
    }
    printf("=============================\n\n");
return 1;
}
