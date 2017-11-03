#include "unp.h"

int eph_port_recv = 0;

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
            int n = Recvfrom(sockfd, recvline, MAXLINE, 0, NULL, NULL);
            recvline[n] = 0;
            Fputs(recvline, stdout);
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
                    }
                    else{
                        fprintf(stderr, "Something went wrong while doing handshake. Abort\n");
                        exit(0);
                    }
                }
                Sendto(sockfd, sendline, strlen(sendline), 0, (SA *)&servaddr, sizeof(servaddr));

            }
        }
    }
        return 0;
}
