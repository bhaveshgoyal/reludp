#include "unp.h"

int main(int argc, char **argv){
    int sockfd;
    struct sockaddr_in servaddr;

    bzero(&servaddr, sizeof(servaddr));
    
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);

    
    char sendline[MAXLINE], recvline[MAXLINE];
    
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
                Sendto(sockfd, "REQ", strlen("REQ"), 0, (SA *)&servaddr, sizeof(servaddr));
                char eph_port[MAXLINE];
                // Also acts as ACK to command
                int n = Recvfrom(sockfd, eph_port, MAXLINE, 0, NULL, NULL);
                eph_port[n] = 0;

                struct sockaddr_in addrinfo;
                int addr_size = sizeof(addrinfo);

                getpeername(sockfd, (SA *)&addrinfo, &addr_size);
                fprintf(stdout, "\nClient bound at ephemeral port at server %s:%s\n", inet_ntoa(addrinfo.sin_addr), eph_port);
                fflush(stdout);
                // Connect to ephemeral
                struct sockaddr_in ephaddr;
                int ephsock = Socket(AF_INET, SOCK_DGRAM, 0);
                bzero(&ephaddr, sizeof(ephaddr));
                ephaddr.sin_family = AF_INET;
                ephaddr.sin_port = htons((int) strtol(eph_port, (char **)NULL, 10));
                Inet_pton(AF_INET, argv[1], &ephaddr.sin_addr); 
                sockfd = ephsock;
                Sendto(ephsock, sendline, strlen(sendline), 0, (SA *)&ephaddr, sizeof(ephaddr));
                 
            }
        }
    }
        return 0;
}
