#include "unp.h"
#include "unpifi.h"
#include <dirent.h>
#define SHARED_PATH "./"

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
int main(int argc, char **argv){
    int sockfd;
    const int on = 1;
    struct sockaddr_in *servaddr, cliaddr;
    struct ifi_info *ifi, *ifihead;

 //   struct sockaddr_in *sa;
//    ifihead = Get_ifi_info(AF_INET, 1);
    for (ifihead = ifi = Get_ifi_info(AF_INET, 1); ifi != NULL;ifi=ifi->ifi_next){
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    
    Setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    
    servaddr = (struct sockaddr_in *) ifi->ifi_addr;
    servaddr->sin_family = AF_INET;
    servaddr->sin_port = htons(SERV_PORT);
    Bind(sockfd, (SA *) servaddr, sizeof(*servaddr));

    printf("Server bound at interface %s\n", Sock_ntop((SA *) servaddr, sizeof(*servaddr)));
    
    }
    /*
    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERV_PORT);

    Bind(sockfd, (SA *) &servaddr, sizeof(servaddr));
    */
    int len = sizeof(cliaddr);

    char msg[MAXLINE];

    while(1){
//        printf("Waiting to recv");
        int n = Recvfrom(sockfd, msg, MAXLINE, 0, (SA *)&cliaddr, &len);
        int pid = fork();
        if (pid == 0){
            char command[MAXLINE] = {0};
            printf("Recieved: %s\n", msg);
            int eph_sock = eph_serv_handshake(sockfd, &cliaddr);
            if (eph_sock > 0){
                struct sockaddr_in echoaddr;
                int addr_size = sizeof(echoaddr);
                getsockname(eph_sock, (SA *)&echoaddr, &addr_size);
                printf("Handshake established at %s:%d\n", inet_ntoa(echoaddr.sin_addr), ntohs(echoaddr.sin_port));
                fflush(stdout);
                close(sockfd);
            }
            else{
                fprintf(stderr, "err: Something unknown happened while establishing UDP ephemeral handshake. Abort\n");
                exit(0);
            }
            while(1){
                int n = Recvfrom(eph_sock, command, MAXLINE, 0, (SA *)&cliaddr, &len);
                if (n < 0)
                    printf("esfsdfdsf");
                if (strstr(command, "list")){
      //              printf("list issued");
        //            fflush(stdout);
                    DIR *p_dir = opendir(SHARED_PATH);
                    char resp[MAXLINE];
                    if (p_dir == NULL){
                        strcpy(resp, "Can't open present directory for reading");
                        fprintf(stderr, "%s", resp);
                        Sendto(eph_sock, resp, strlen(resp), 0, (SA *)&cliaddr, len);
                    }
                    else{
                        struct dirent *pDirent;
                        while ((pDirent = readdir(p_dir)) != NULL) {
                            strcat(resp, strcat(pDirent->d_name, "\n"));
                        }
                        Sendto(eph_sock, resp, strlen(resp), 0, (SA *)&cliaddr, len);
                        closedir (p_dir);
                    }
                }
                else if (strstr(command, "download")){
       //             printf("download issued");
         //           printf("%s", command);
        //            fflush(stdout);
                    char *d_args = strtok(command, " ");
                    d_args = strtok(NULL, " \n");
                    char path[MAXLINE];
                    strcpy(path, SHARED_PATH);
                    strcat(path, d_args);
                    //            printf("Path: %s\n", path);
                    //          fflush(stdout);
                    int fd = open(path, O_RDONLY);
                    if (fd < 0){
                        char *msg = "Error Downloading file from server"; 
                        Sendto(eph_sock, msg, strlen(msg), 0, (SA *) &cliaddr, len);
                        continue;
                    }
                    char file_buf[MAXLINE];
                    int n;
                    while ((n = read(fd, file_buf, MAXLINE)) > 0) {
                        Sendto(eph_sock, file_buf, n, 0, (SA *) &cliaddr, len);
                    }
                }
                else
                    Sendto(eph_sock, command, n, 0, (SA *)&cliaddr, len);
                memset(command, 0, sizeof(command));
            }
        }

    }

    return 0;
}
