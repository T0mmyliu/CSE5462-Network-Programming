#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <netinet/tcp.h>

#include "address.h"
#include "wrap_func.h"

static int blocking;
int CONNECT(int socket, struct sockaddr *addr, socklen_t address_len)
{
    return 0;
}

int ACCEPT(int socket, struct sockaddr *address, socklen_t *address_len)
{
    return 0;
}

int BIND(int socket, struct sockaddr *address, socklen_t address_len)
{
    return bind(socket, address, address_len);
}

int LISTEN(int sockfd, int backlog)
{
    return 0;
}

int SOCKET(int domain, int type, int protocol)
{
    return socket(AF_INET, SOCK_DGRAM, 0);
}

ssize_t SEND(int sock, const void *buffer, size_t length, int flags)
{
    fd_set readfds;
    int left = length;
    struct sockaddr_in sock_addr, addr_block_sig;
    int socket_block_sig;

    socket_block_sig = socket(AF_INET, SOCK_DGRAM, 0);

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(LOCAL_PORT_CLIENT);
    sock_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);

    addr_block_sig.sin_family = AF_INET;
    addr_block_sig.sin_port = htons(BLOCK_SIG);
    addr_block_sig.sin_addr.s_addr = inet_addr(CLIENT_IP);

    if (bind(socket_block_sig, (struct sockaddr *)&addr_block_sig, sizeof(addr_block_sig)) < 0)
    {
        perror("bind addr_block_sig");
        exit(EXIT_FAILURE);
    }
    int byte;
    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(socket_block_sig, &readfds);

        if (blocking == 0)
        {
            printf("not blocking at last time...send a packet to tcpd\n");
            byte = sendto(sock, buffer, length, flags, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
        }

        printf("waiting for a signal from tcpd\n");
        select(FD_SETSIZE, &readfds, NULL, NULL, NULL);

        printf("receive a signal from tcpd\n");
        char buffer[10] = {0};
        socklen_t addrlen = sizeof(addr_block_sig);
        recvfrom(socket_block_sig, buffer, sizeof(buffer), 0,
                 (struct sockaddr *)&addr_block_sig, &addrlen);

        if (strcmp(buffer, "block") == 0)
        {
            printf("The tcpd window is full, blocking....\n");
            blocking = 1;
        }

        else if (strcmp(buffer, "unblock") == 0)
        {
            printf("The tcpd window has empty space, unblock!\n");
            left = left - byte;
            blocking = 0;
        }

        else if (strcmp(buffer, "receved") == 0)
        {
            printf("The tcpd receved the packet!\n");
            left = left - byte;
        }

        else
        {
            printf("Receve unknown command from tcpd, the command is : %s....\n", buffer);
        }

        if (left == 0)
        {
            printf("FINISH ONE SEND\n");
            close(socket_block_sig);
            return length;
        }
    }
}

ssize_t RECV(int socket, const void *buffer, size_t length, int flags)
{
    struct sockaddr_in sock_addr;
    socklen_t addrlen;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(SERVER_PORT);
    sock_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    addrlen = sizeof(sock_addr);
    return recvfrom(socket, (char *)buffer, length, flags, (struct sockaddr *)&sock_addr, &addrlen);
}