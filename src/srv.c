#include "unp.h"
#include <dirent.h>
#define SHARED_PATH "./"
int main(int argc, char **argv){
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;

    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERV_PORT);

    Bind(sockfd, (SA *) &servaddr, sizeof(servaddr));
    
    int len = sizeof(cliaddr);

    char msg[MAXLINE];

    while(1){
//        printf("Waiting to recv");
        int n = Recvfrom(sockfd, msg, MAXLINE, 0, (SA *)&cliaddr, &len);
        int pid = fork();
        if (pid == 0){
            struct sockaddr_in ephaddr;
            int eph_sock = Socket(AF_INET, SOCK_DGRAM, 0);

            bzero(&ephaddr, sizeof(ephaddr));
            ephaddr.sin_family = AF_INET;
            ephaddr.sin_addr.s_addr = htonl(INADDR_ANY);

            Bind(eph_sock, (SA *)&ephaddr, sizeof(ephaddr));

            struct sockaddr_in echoaddr;
            int addr_size = sizeof(echoaddr);
            getsockname(eph_sock, (SA *)&echoaddr, &addr_size);
            printf("New UDP client: connection established at %s:%d\n", inet_ntoa(echoaddr.sin_addr), ntohs(echoaddr.sin_port));
            fflush(stdout);
            char ephport_res[MAXLINE];
            sprintf(ephport_res, "%d", ntohs(echoaddr.sin_port));
            Sendto(sockfd, ephport_res, strlen(ephport_res), 0, (SA *)&cliaddr, len);

            char command[MAXLINE];
            fflush(stdout);
            while(1){
                int n = Recvfrom(eph_sock, command, MAXLINE, 0, (SA *)&cliaddr, &len);

                if (strcmp(command, "list") >= 0){
                    //          printf("list issued");
                    //           fflush(stdout);
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
                            strcpy(resp, strcat(pDirent->d_name, "\n"));
                            Sendto(eph_sock, resp, strlen(resp), 0, (SA *)&cliaddr, len);
                        }
                        closedir (p_dir);
                    }
                }
                else if (strcmp(command, "download") >= 0){
                    //           printf("download issued");
                    //         fflush(stdout);
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
                        sendto(eph_sock, file_buf, n, 0, (SA *) &cliaddr, len);
                    }
                }
                else
                    Sendto(eph_sock, command, n, 0, (SA *)&cliaddr, len);
            }
        }

    }

    return 0;
}
