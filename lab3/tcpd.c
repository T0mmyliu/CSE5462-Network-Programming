#include <netinet/in.h>
#include <sys/types.h>
#include <string.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "address.h"

int main(int argc, char* argv[])
{
    struct sockaddr_in local_addr,remote_addr,troll_addr,server_addr,client_addr;
    int sock_local,sock_remote;
    char buffer_local[1000],buffer_remote[1000];
    fd_set readfds;

    if(argc!=1){
        printf("Error, the usage is: \"./tcpd\"\n");
        exit(1);
    }

    sock_local=socket(AF_INET,SOCK_DGRAM,0);
    sock_remote=socket(AF_INET,SOCK_DGRAM,0);

    bzero((void*)&local_addr,sizeof(local_addr));
    bzero((void*)&remote_addr,sizeof(remote_addr));
    bzero((void*)&troll_addr,sizeof(troll_addr));
    bzero((void*)&server_addr,sizeof(server_addr));
    bzero((void*)&client_addr,sizeof(client_addr));
    
    local_addr.sin_family=AF_INET;
    local_addr.sin_port=htons(LOCAL_PORT);
    local_addr.sin_addr.s_addr=INADDR_ANY;
                
    remote_addr.sin_family=AF_INET;
    remote_addr.sin_port=htons(REMOTE_PORT);
    remote_addr.sin_addr.s_addr=INADDR_ANY;

    troll_addr.sin_family=AF_INET;
    troll_addr.sin_port=htons(TROLL_PORT);
    troll_addr.sin_addr.s_addr=inet_addr(CLIENT_IP);

    if(bind(sock_local,(struct sockaddr*)&local_addr,sizeof(local_addr))<0){
        perror("local bind");
    }
    
    if(bind(sock_remote,(struct sockaddr*)&remote_addr,sizeof(remote_addr))<0){
        perror("remote bind");
    }

    FD_ZERO(&readfds);
    FD_SET(sock_local,&readfds);
    FD_SET(sock_remote,&readfds);

    while(1){
        if(select(2,&readfds,NULL,NULL,NULL)==-1){
            perror("select()");
        }

        if(FD_ISSET(sock_local,&readfds)){
            bzero(&buffer_local,sizeof(buffer_local));

            int bytes;
            socklen_t len=sizeof(local_addr);
            bytes=recvfrom(sock_local,buffer_local,sizeof(buffer_local),0,
                    (struct sockaddr*)&local_addr,&len);
            if(bytes<0){
                perror("Receing datagram from ftpc.");
                exit(1);
            }

            printf("Bytes from client :%d\n",bytes);
            
            bytes=sendto(sock_remote,buffer_local,sizeof(buffer_local),0,
                    (struct sockaddr*)&troll_addr,sizeof(troll_addr));
            
            printf("Send to Troll: %d.\n",bytes);
        }

        if(FD_ISSET(sock_remote,&readfds)){
             bzero(&buffer_local,sizeof(buffer_remote));
              
             int bytes;
             socklen_t len=sizeof(remote_addr);
             bytes=recvfrom(sock_remote,buffer_remote,sizeof(buffer_remote),0,
                     (struct sockaddr*)&remote_addr,&len);
             if(bytes<0){
                 perror("Receing datagram from troll.");
                 exit(1);
             }
                                                             
             printf("Bytes from troll :%d\n",bytes);
                                                                               
             bytes=sendto(sock_remote,buffer_remote,sizeof(buffer_remote),0,
             (struct sockaddr*)&server_addr,sizeof(server_addr));
                                                                                            
             printf("Send to server: %d.\n",bytes);
        }

        FD_ZERO(&readfds);
        FD_SET(sock_remote,&readfds);
        FD_SET(sock_remote,&readfds);
    }
    
    return 0;
}
    
