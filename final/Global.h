#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include "Header.h"
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#define tcpd_msg_socket sock

struct binded_sock
{
    int SocketFD;
    int Accepting;
    struct sockaddr_in AcceptingAddr;
};

struct acked_sock
{
    int SocketFD;
    uint32_t Session;
    uint32_t Base;
    size_t ReceivingLength;
    struct sockaddr_in ReceivingAddr;
    GList *Packages;
    char RecvBuffer[BUF_LEN];
    size_t RecvBufferSize;
};

struct connected_sock
{
    int SocketFD;
    uint32_t Session;
    uint32_t Base;
    uint32_t MinSeq;
    struct sockaddr_in ServerAddr;
    GList *Packages;
    GList *PendingPackages;
};

GHashTable *ConnectedSockets;

ERRCODE recv_data(struct binded_sock *NowBindedSocket);
ERRCODE recv_ack(struct connected_sock *NowConnectedSocket);
ERRCODE recv_timer();
ERRCODE handle_ack_back();
ERRCODE handle_recv_back();
ERRCODE handle_send_all_back();
ERRCODE print_tcpd_status();
ERRCODE tcpd_SOCKET(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength);
ERRCODE tcpd_BIND(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength);
ERRCODE tcpd_ACCEPT(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength);
ERRCODE tcpd_RECV(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength);
ERRCODE tcpd_CONNECT(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength);
ERRCODE tcpd_SEND(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength);

void GlobalInitialize();
void global_init();
void socket_init();
void re_register_fd();
void process_tcpd_message();
void handle_back_imformation();
void get_data_from_sockets();

#endif