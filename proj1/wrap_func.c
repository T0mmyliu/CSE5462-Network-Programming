#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "address.h"
#include "wrap_func.h"

int CONNECT(int socket,struct sockaddr* addr,socklen_t address_len){
    return 0;
}

int ACCEPT(int socket, struct sockaddr *address,socklen_t* address_len){
    return 0;
}

int BIND(int socket,struct sockaddr *address, socklen_t address_len){
    return bind(socket,address,address_len);
}

int LISTEN(int sockfd, int backlog){
    return 0;
}

int SOCKET(int domain,int type,int protocol){
    return socket(AF_INET,SOCK_DGRAM,0);
}

ssize_t SEND(int socket,const void *buffer,size_t length,int flags){
    usleep(10000); 
    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(LOCAL_PORT);
    sock_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);
    return sendto(socket,buffer,length,flags,(struct sockaddr*)&sock_addr,sizeof(sock_addr));
}

ssize_t RECV(int socket,const void *buffer,size_t length,int flags){
    struct sockaddr_in sock_addr;
    socklen_t addrlen;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(SERVER_PORT);
    sock_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    addrlen=sizeof(sock_addr);
    return recvfrom(socket,(char *)buffer,length,flags,(struct sockaddr*)&sock_addr,&addrlen);
}


