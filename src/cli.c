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

    while(Fgets(sendline, MAXLINE, stdin) != NULL){

        Sendto(sockfd, sendline, strlen(sendline), 0, (SA *)&servaddr, sizeof(servaddr));
        int n = Recvfrom(sockfd, recvline, MAXLINE, 0, NULL, NULL);
        recvline[n] = 0;
        Fputs(recvline, stdout);
    }
    return 0;
}
