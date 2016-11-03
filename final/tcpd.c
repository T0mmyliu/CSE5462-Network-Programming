#include "stdafx.h"

int sock; /* internal communication socket */
char buf[BUF_LEN];
struct sockaddr_in tcpd_name;
struct sockaddr_in troll_addr;
socklen_t tcpd_name_len;
char end_name[MAX_PATH];
struct hostent *troll_server_host;
int sock_timer_driver;
struct sockaddr_in timer_addr;
int max_socket;
fd_set sock_fd;
int in_buffer_packages = 0;

GArray *binded_sockets;
GHashTable *acked_sockets;
GList *waiting_ack_sockets;

//jockobson's algorithm info
struct timeval SendTime = {.tv_sec = 0, .tv_usec = 0}, AckTime = {.tv_sec = 0, .tv_usec = 0}; /*for RTT/RTO calculation*/
double ElapsedSeconds = 0.0;
double EstimatedRTT = 0.0;
double EstimatedRTTprev = 0.01;
double EstimatedDEV = 0.0;
double EstimatedDEVprev = 0.01;
double EstimatedRTO = 0.0;

int main(int argc, char *argv[])
{
    global_init();

    if (argc == 2)
    {
        strcpy(end_name, argv[1]);
    }

    else
    {
        perror("USAGE ERROR");
        exit(1);
    }

    socket_init();

    while (1)
    {
        //g_debug("*******************************LOOP START!*************************************");
        //g_debug("waiting socket ...");
        re_register_fd();

        struct timeval waiting_time;
        waiting_time.tv_sec = 1000;
        waiting_time.tv_usec = 0;

        int ready = select(max_socket + 1, &sock_fd, NULL, NULL, &waiting_time);

        if (ready > 0)
        {
            //g_debug("Testing active socket ...");
            if (FD_ISSET(tcpd_msg_socket, &sock_fd))
            {
                //g_debug("Receiving Tcpd Message ...");
                process_tcpd_message();
            }

            get_data_from_sockets();

            GHashTableIter iter_connected_sock;
            int sockfd_connected;
            struct connected_sock *tmp_sock_connected;
            g_hash_table_iter_init(&iter_connected_sock, ConnectedSockets);

            //look for ack
            while (g_hash_table_iter_next(&iter_connected_sock, (void *)&sockfd_connected, (void *)&tmp_sock_connected))
            {
                if (FD_ISSET(tmp_sock_connected->SocketFD, &sock_fd))
                {
                    ////g_info("Received Ack");
                    recv_ack(tmp_sock_connected);
                }
            }

            //look for expired timers
            if (FD_ISSET(sock_timer_driver, &sock_fd))
            {
                g_debug("[TIMEOUT]: Receiving timer expired request");
                recv_timer();
            }
        }
        handle_back_imformation();
        print_tcpd_status();
    }

    /* server terminates connection, closes socket, and exits */
    close(sock);
    exit(0);
}

//receive data from troll or from client
ERRCODE recv_data(struct binded_sock *binded_sock_fd)
{
    //g_assert(binded_sock_fd);
    //g_debug("Receiving data sent to socket %d ...", binded_sock_fd->SocketFD);
    struct TrollMessageStruct *NowTrollMessage = (struct TrollMessageStruct *)malloc(sizeof(struct TrollMessageStruct));
    struct sockaddr_in RemoteTrollAddr;
//receive from the troll or client
#ifdef __WITH_TROLL
    socklen_t RemoteTrollAddrLength = sizeof(RemoteTrollAddr);
    //g_debug("Receiving data from troll ...");
    ssize_t RecvRet = recvfrom(binded_sock_fd->SocketFD, (void *)NowTrollMessage, sizeof(*NowTrollMessage), MSG_WAITALL, (struct sockaddr *)&RemoteTrollAddr, &RemoteTrollAddrLength);
#else
    struct sockaddr_in ClientAddr;
    socklen_t ClientAddrLength = sizeof(ClientAddr);
    //g_debug("Receiving data directly from client ...");
    ssize_t RecvRet = recvfrom(binded_sock_fd->SocketFD, (void *)NowTrollMessage, sizeof(*NowTrollMessage), MSG_WAITALL, (struct sockaddr *)&ClientAddr, &ClientAddrLength);
    NowTrollMessage->header = ClientAddr;
#endif
    if (RecvRet < 0)
    {
        perror("error receiving Data");
        exit(5);
    }

    //check the checksum
    //g_debug("Checking package body is garbled or not ...");
    uint32_t CheckSum = CHECKSUM_ALGORITHM(0, ((char *)(NowTrollMessage)) + CheckSumOffset, sizeof(*NowTrollMessage) - CheckSumOffset);
    if (CheckSum != NowTrollMessage->CheckSum)
    {
        g_info("[GARBLED] Now checksum: %u, Origin checksum: %u", CheckSum, NowTrollMessage->CheckSum);
        return -2;
    }

    //verify socket info
    //g_debug("Finding Accepted socket with session %u", NowTrollMessage->Session);
    struct acked_sock *NowAcceptedSocket = g_hash_table_lookup(acked_sockets, GUINT_TO_POINTER(NowTrollMessage->Session));
    if (NowAcceptedSocket == NULL)
    {
        GList *l;
        for (l = waiting_ack_sockets; l != NULL; l = l->next)
        {
            if (((struct acked_sock *)(l->data))->Session == NowTrollMessage->Session)
            {
                NowAcceptedSocket = (struct acked_sock *)(l->data);
                break;
            }
        }
    }

    //g_debug("Constructing Accepted Socket if not found in acked_sockets or waiting_ack_sockets ...");
    if (NowAcceptedSocket == NULL)
    {
        NowAcceptedSocket = (struct acked_sock *)malloc(sizeof(struct acked_sock));
        NowAcceptedSocket->SocketFD = binded_sock_fd->SocketFD;
        NowAcceptedSocket->Session = NowTrollMessage->Session;
        NowAcceptedSocket->Base = 0;
        NowAcceptedSocket->Packages = NULL;
        NowAcceptedSocket->ReceivingLength = 0;
        NowAcceptedSocket->RecvBufferSize = 0;
        waiting_ack_sockets = g_list_append(waiting_ack_sockets, NowAcceptedSocket);
    }

    //place package in buffer
    //g_debug("Inserting received package into packages buffer ...");
    int RequireValid = TRUE;
    if (NowAcceptedSocket->Base > NowTrollMessage->SeqNum)
    {
        ////g_info("[OUTDATED] Received Outdated package");
        RequireValid = FALSE;
    }
    GList *l;
    //find if package is already in list
    for (l = NowAcceptedSocket->Packages; l != NULL; l = l->next)
    {
        struct TrollMessageStruct *TmpTrollMessage = (struct TrollMessageStruct *)(l->data);
        if (TmpTrollMessage->SeqNum == NowTrollMessage->SeqNum)
        {
            g_info("[DUPLICATED] Received duplicated package");
            RequireValid = FALSE;
            break;
        }
        if (TmpTrollMessage->SeqNum > NowTrollMessage->SeqNum)
        {
            break;
        }
    }
    //insert in list
    if (RequireValid)
    {
        NowAcceptedSocket->Packages = g_list_insert_before(NowAcceptedSocket->Packages, l, NowTrollMessage);
    }
    //make ACK package to return
    //g_debug("Inserted received package");
    //g_debug("Constructing Ack package ...");
    struct TrollAckStruct *NowTrollAck = (struct TrollAckStruct *)malloc(sizeof(struct TrollAckStruct));
    NowTrollAck->header = NowTrollMessage->header;
    NowTrollAck->header.sin_family = htons(AF_INET);
    NowTrollAck->header.sin_addr = RemoteTrollAddr.sin_addr;
    NowTrollAck->Session = NowTrollMessage->Session;
    NowTrollAck->SeqNum = NowTrollMessage->SeqNum;
    NowTrollAck->CheckSum = CHECKSUM_ALGORITHM(0, ((char *)(NowTrollAck)) + CheckSumOffset, sizeof(*NowTrollAck) - CheckSumOffset);
    ////g_info("Sending Ack Addr:%s, Port:%u, SeqNum:%u (Session:%u)", inet_ntoa(NowTrollAck->header.sin_addr), ntohs(NowTrollAck->header.sin_port), NowTrollAck->SeqNum, NowTrollAck->Session);
    //g_debug("Sending ACK back ...");
    //send ACK
    ssize_t AckRet = sendto(binded_sock_fd->SocketFD, (void *)NowTrollAck, sizeof(*NowTrollAck), MSG_WAITALL, (struct sockaddr *)&troll_addr, sizeof(troll_addr));
    if (AckRet < 0)
    {
        perror("error sending Ack to troll");
        exit(5);
    }
    //g_debug("Recv Data done.");
    return 0;
}

//receive ACK response from TROLL
ERRCODE recv_ack(struct connected_sock *NowConnectedSocket)
{
    //g_assert(NowConnectedSocket);
    //g_debug("Receiving Ack sent to socket %d ...", NowConnectedSocket->SocketFD);
    struct TrollAckStruct *NowTrollAck = (struct TrollAckStruct *)malloc(sizeof(struct TrollAckStruct));
//receieve ACK from TROLL
#ifdef __WITH_TROLL
    struct sockaddr_in RemoteTrollAddr;
    socklen_t RemoteTrollAddrLength = sizeof(RemoteTrollAddr);
    //g_debug("Receiving Ack from troll ...");
    ssize_t RecvRet = recvfrom(NowConnectedSocket->SocketFD, (void *)NowTrollAck, sizeof(*NowTrollAck), MSG_WAITALL, (struct sockaddr *)&RemoteTrollAddr, &RemoteTrollAddrLength);
#else
    struct sockaddr_in ClientAddr;
    socklen_t ClientAddrLength = sizeof(ClientAddr);
    //g_debug("Receiving Ack directly from client ...");
    ssize_t RecvRet = recvfrom(NowConnectedSocket->SocketFD, (void *)NowTrollAck, sizeof(*NowTrollAck), MSG_WAITALL, (struct sockaddr *)&ClientAddr, &ClientAddrLength);
    NowTrollAck->header = ClientAddr;
#endif

    //get RTT
    gettimeofday(&AckTime, NULL);
    //g_debug("Calculating elapsed time ...\n\n\n");
    //elapsed time between send and ack response
    ElapsedSeconds = (double)(SendTime.tv_sec - AckTime.tv_sec) + USEC2SEC * SendTime.tv_usec - USEC2SEC * AckTime.tv_usec;
    if (ElapsedSeconds < 0.0)
        ElapsedSeconds *= -1.0;
    ////g_info("Elapsed %6f seconds", ElapsedSeconds);

    if (RecvRet < 0)
    {
        perror("error receiving Ack");
        exit(5);
    }
    //check the checksum
    //g_debug("Checking package body is garbled or not ...");
    uint32_t CheckSum = CHECKSUM_ALGORITHM(0, ((char *)(NowTrollAck)) + CheckSumOffset, sizeof(*NowTrollAck) - CheckSumOffset);
    ////g_info("Received Ack SeqNum:%u (Session:%u)", NowTrollAck->SeqNum, NowTrollAck->Session);
    if (CheckSum != NowTrollAck->CheckSum)
    {
        g_info("[GARBLED] CHECKSUM: %u, Origin CHECKSUM: %u", CheckSum, NowTrollAck->CheckSum);
        return -2;
    }
    //g_debug("Removing Ack package with SeqNum %u (Session %u) ...", NowTrollAck->SeqNum, NowTrollAck->Session);
    GList *l;
    //remove all ACKed packages from window
    for (l = NowConnectedSocket->Packages; l != NULL; l = l->next)
    {
        struct TrollMessageStruct *TmpTrollMessage = (struct TrollMessageStruct *)(l->data);
        if (TmpTrollMessage->Session == NowTrollAck->Session && TmpTrollMessage->SeqNum == NowTrollAck->SeqNum)
        {
            NowConnectedSocket->Packages = g_list_remove_link(NowConnectedSocket->Packages, l);
            in_buffer_packages--;
            free(TmpTrollMessage);
            ////g_info("Package SeqNum %u removed. (Session %u)", TmpTrollMessage->SeqNum, TmpTrollMessage->Session);
            break;
        }
    }
    //cancel all removed packages timer requests
    //g_debug("Sending cancel timer request with SeqNum %u (Session %u) ...", NowTrollAck->SeqNum, NowTrollAck->Session);
    CancelTimerRequest(sock_timer_driver, &timer_addr, NowTrollAck->Session, NowTrollAck->SeqNum);
    free(NowTrollAck);
    return 0;
}

//timer expiration notice
ERRCODE recv_timer()
{
    //g_debug("Receiving Timer Expired request ...");
    struct TimerExpiredStruct *NowExpired = (struct TimerExpiredStruct *)malloc(sizeof(struct TimerExpiredStruct));
    struct sockaddr_in RemoteTimerAddr;
    socklen_t RemoteTimerAddrLength = sizeof(RemoteTimerAddr);
    ssize_t RecvRet = recvfrom(sock_timer_driver, (void *)NowExpired, sizeof(*NowExpired), MSG_WAITALL, (struct sockaddr *)&RemoteTimerAddr, &RemoteTimerAddrLength);
    if (RecvRet < 0)
    {
        perror("error receiving Timer Expired");
        exit(5);
    }
    //get socket info for timer
    //g_debug("Finding connected socket for timer expired ...");
    struct connected_sock *NowConnectedSocket = NULL;
    GHashTableIter iter_connected_sock;
    int sockfd_connected;
    struct connected_sock *tmp_sock_connected;
    g_hash_table_iter_init(&iter_connected_sock, ConnectedSockets);
    while (g_hash_table_iter_next(&iter_connected_sock, (void *)&sockfd_connected, (void *)&tmp_sock_connected))
    {
        if (tmp_sock_connected->Session == NowExpired->Session)
        {
            NowConnectedSocket = tmp_sock_connected;
            break;
        }
    }
    if (NowConnectedSocket == NULL)
    {
        perror("socket not found");
        exit(5);
    }
    //add package to list to be resent to socket
    //g_debug("Moving expired package to pending sequence ...");
    GList *l;
    for (l = NowConnectedSocket->Packages; l != NULL; l = l->next)
    {
        struct TrollMessageStruct *TmpTrollMessage = (struct TrollMessageStruct *)(l->data);
        if (TmpTrollMessage->Session == NowExpired->Session && TmpTrollMessage->SeqNum == NowExpired->SeqNum)
        {
            NowConnectedSocket->Packages = g_list_remove_link(NowConnectedSocket->Packages, l);
            NowConnectedSocket->PendingPackages = g_list_append(NowConnectedSocket->PendingPackages, TmpTrollMessage);
            g_info("Packet NUM %d goint to resend. ", TmpTrollMessage->SeqNum);
            break;
        }
    }
    //g_debug("Recv Timer done.");
    return 0;
}

//search and accept binded sockets and move sockets from waiting struct to accepted struct
ERRCODE handle_ack_back()
{
    //g_debug("Sending available accept callback to front process");
    if (waiting_ack_sockets == NULL)
    {
        return 0;
    }
    //g_debug("Searching accept request in binded sockets ...");
    uint i;
    //search through all binded sockets to accept
    for (i = 0; i < binded_sockets->len; i++)
    {
        struct binded_sock *sock_tmp = g_array_index(binded_sockets, struct binded_sock *, i);
        if (sock_tmp->Accepting)
        {
            //g_debug("Searching waiting list with given binded socket ...");
            GList *l;
            for (l = waiting_ack_sockets; l != NULL; l = l->next)
            {
                struct acked_sock *TmpAcceptedSocket = (struct acked_sock *)(l->data);
                if (TmpAcceptedSocket->SocketFD == sock_tmp->SocketFD)
                {
                    //send accept callback
                    //g_debug("Sending accept callback ...");
                    if (sendto(tcpd_msg_socket, (char *)&(TmpAcceptedSocket->Session), sizeof(int), 0, (struct sockaddr *)&(sock_tmp->AcceptingAddr), sizeof(sock_tmp->AcceptingAddr)) < 0)
                    {
                        perror("error sending ACCEPT return");
                        exit(4);
                    }
                    //move sockets that are accepted to accepted list
                    //g_debug("Moving accepted socket struct from waiting list to accepted list ...");
                    g_hash_table_insert(acked_sockets, GUINT_TO_POINTER(TmpAcceptedSocket->Session), TmpAcceptedSocket);
                    waiting_ack_sockets = g_list_remove_link(waiting_ack_sockets, l);
                    sock_tmp->Accepting = FALSE;
                    break;
                }
            }
        }
    }
    //g_debug("Solve Accept Callback done.");
    return 0;
}

//deal with pending receive request from ftps
ERRCODE handle_recv_back()
{
    //g_debug("Sending Recv Callbacks ...");
    GHashTableIter AcceptedSocketIter;
    int AcceptedSocketSession;
    struct acked_sock *TmpAcceptedSocket;
    g_hash_table_iter_init(&AcceptedSocketIter, acked_sockets);
    //iterate through sockets
    while (g_hash_table_iter_next(&AcceptedSocketIter, (void *)&AcceptedSocketSession, (void *)&TmpAcceptedSocket))
    {
        //if theres a rcv request then send data to socket
        if (TmpAcceptedSocket->ReceivingLength > 0)
        {
            //g_debug("Testing RECV request on session %u with length %zu", TmpAcceptedSocket->Session, TmpAcceptedSocket->ReceivingLength);
            if (TmpAcceptedSocket->ReceivingLength > BUF_LEN)
            {
                perror("Request length exceed buffer length");
                exit(5);
            }
            while (TmpAcceptedSocket->RecvBufferSize < TmpAcceptedSocket->ReceivingLength)
            {
                //g_debug("RecvBufferSize:%zu RecvBuffer:%s", TmpAcceptedSocket->RecvBufferSize, TmpAcceptedSocket->RecvBuffer);
                if (TmpAcceptedSocket->Packages == NULL)
                {
                    break;
                }
                struct TrollMessageStruct *NowTrollMessage = (struct TrollMessageStruct *)(g_list_first(TmpAcceptedSocket->Packages)->data);
                if (NowTrollMessage->SeqNum != TmpAcceptedSocket->Base)
                {
                    break;
                }
                //get message for socket
                if (TmpAcceptedSocket->RecvBufferSize + NowTrollMessage->len <= TmpAcceptedSocket->ReceivingLength)
                {
                    bcopy(NowTrollMessage->body, TmpAcceptedSocket->RecvBuffer + TmpAcceptedSocket->RecvBufferSize, NowTrollMessage->len);
                    TmpAcceptedSocket->RecvBufferSize += NowTrollMessage->len;
                    TmpAcceptedSocket->Packages = g_list_remove_link(TmpAcceptedSocket->Packages, g_list_first(TmpAcceptedSocket->Packages));
                    TmpAcceptedSocket->Base++;
                    free(NowTrollMessage);
                }
                else
                {
                    size_t RemainSize = TmpAcceptedSocket->ReceivingLength - TmpAcceptedSocket->RecvBufferSize;
                    bcopy(NowTrollMessage->body, TmpAcceptedSocket->RecvBuffer + TmpAcceptedSocket->RecvBufferSize, RemainSize);
                    bcopy(NowTrollMessage->body + RemainSize, NowTrollMessage->body, NowTrollMessage->len - RemainSize);
                    NowTrollMessage->len -= RemainSize;
                }
            }
            //if requested size = buffer size then send the buffer to the request socket
            //g_debug("RecvBufferSize:%zu RecvBuffer:%s", TmpAcceptedSocket->RecvBufferSize, TmpAcceptedSocket->RecvBuffer);
            if (TmpAcceptedSocket->RecvBufferSize == TmpAcceptedSocket->ReceivingLength)
            {
                //g_debug("Sending data with size %zu to upper process", TmpAcceptedSocket->RecvBufferSize);
                //g_debug("data: %s", TmpAcceptedSocket->RecvBuffer);
                if (sendto(tcpd_msg_socket, TmpAcceptedSocket->RecvBuffer, TmpAcceptedSocket->RecvBufferSize, 0, (struct sockaddr *)&(TmpAcceptedSocket->ReceivingAddr), sizeof(TmpAcceptedSocket->ReceivingAddr)) < 0)
                {
                    perror("error sending RECV return");
                    exit(4);
                }
                TmpAcceptedSocket->RecvBufferSize = 0;
                TmpAcceptedSocket->ReceivingLength = 0;
            }
        }
    }
    return 0;
}

//send packages to troll and add timers
ERRCODE handle_send_all_back()
{
    //g_debug("Sending all pending data packages to server");
    GHashTableIter iter_connected_sock;
    int sockfd_connected;
    struct connected_sock *tmp_sock_connected;
    g_hash_table_iter_init(&iter_connected_sock, ConnectedSockets);
    //iterate through connected sockets
    while (g_hash_table_iter_next(&iter_connected_sock, (void *)&sockfd_connected, (void *)&tmp_sock_connected) != FALSE)
    {
        //g_debug("Updating MinSeq of ConnectedSocket");
        tmp_sock_connected->MinSeq = 100000000;
        GList *l;
        //get seq numbers
        for (l = tmp_sock_connected->Packages; l != NULL; l = l->next)
        {
            struct TrollMessageStruct *TmpTrollMessage = (struct TrollMessageStruct *)(l->data);
            tmp_sock_connected->MinSeq = TmpTrollMessage->SeqNum < tmp_sock_connected->MinSeq ? TmpTrollMessage->SeqNum : tmp_sock_connected->MinSeq;
        }
        l = tmp_sock_connected->PendingPackages;
        //iterate through all packages
        while (l != NULL)
        {
            //g_debug("Sending one pending package ...");
            GList *lnext = l->next;
            struct TrollMessageStruct *NowTrollMessage = (struct TrollMessageStruct *)(l->data);
            if (NowTrollMessage->SeqNum >= tmp_sock_connected->MinSeq + MAX_BUFFER_PACKAGES)
            {
                l = lnext;
                continue;
            }
            ////g_info("Sending package Addr:%s, Port:%u, SeqNum:%u (Session:%u)", inet_ntoa(NowTrollMessage->header.sin_addr), ntohs(NowTrollMessage->header.sin_port), NowTrollMessage->SeqNum, NowTrollMessage->Session);
            //send buffer to troll
            ssize_t SendRet = sendto(tmp_sock_connected->SocketFD, (void *)NowTrollMessage, sizeof(*NowTrollMessage), MSG_WAITALL, (struct sockaddr *)&troll_addr, sizeof(troll_addr));
            if (SendRet < 0)
            {
                perror("error sending buffer to troll");
                exit(5);
            }
            tmp_sock_connected->Packages = g_list_append(tmp_sock_connected->Packages, NowTrollMessage);
            tmp_sock_connected->PendingPackages = g_list_remove_link(tmp_sock_connected->PendingPackages, l);
            //g_debug("Send timer request");

            //calculate RTT, DEV and RTO to set timer with jacobson's algorithm
            gettimeofday(&SendTime, NULL);
            if (ElapsedSeconds <= 0)
                ElapsedSeconds = .01;
            EstimatedRTT = .875 * EstimatedRTTprev + (1 - .875) * ElapsedSeconds;
            EstimatedDEV = (1 - .25) * EstimatedDEVprev + .25 * abs(ElapsedSeconds - EstimatedRTTprev);
            EstimatedRTO = EstimatedRTTprev + 4 * EstimatedDEVprev;
            g_info("\n\n\nRTT %6f seconds", EstimatedRTT);
            g_info("RTO %6f seconds", EstimatedRTO);
            EstimatedDEVprev = EstimatedDEV;
            EstimatedRTTprev = EstimatedRTT;

            //send timer request with calculated RTO
            SendTimerRequest(sock_timer_driver, &timer_addr, NowTrollMessage->Session, NowTrollMessage->SeqNum, EstimatedRTO);
            //g_debug("Delay 1000usec to avoid blocking local udp transmission...");
            usleep(1000);
            // sleep(1);
            l = lnext;
        }
    }
    //g_debug("Solve Send Callback done.");
    return 0;
}

//print entire tcpd status
ERRCODE print_tcpd_status()
{
    //g_debug("Printing Tcpd Status...");
    ////g_info("##########");
    ////g_info("#Tcpd Status");
    ////g_info("##binded_sockets len:%u", binded_sockets->len);
    uint i;
    for (i = 0; i < binded_sockets->len; i++)
    {
        struct binded_sock *sock_tmp = g_array_index(binded_sockets, struct binded_sock *, i);
        ////g_info("###BindedSocket:%u SocketFD:%d Accepting:%d Addr:%s Port:%hu", i, sock_tmp->SocketFD, sock_tmp->Accepting, inet_ntoa(sock_tmp->AcceptingAddr.sin_addr), ntohs(sock_tmp->AcceptingAddr.sin_port));
    }
    ////g_info("##acked_sockets size:%u", g_hash_table_size(acked_sockets));
    GHashTableIter AcceptedSocketIter;
    int AcceptedSocketSession;
    struct acked_sock *TmpAcceptedSocket;
    g_hash_table_iter_init(&AcceptedSocketIter, acked_sockets);
    while (g_hash_table_iter_next(&AcceptedSocketIter, (void *)&AcceptedSocketSession, (void *)&TmpAcceptedSocket))
    {
        ////g_info("###AcceptedSocket:%d Session:%u Base:%u Packages:%u", TmpAcceptedSocket->SocketFD, TmpAcceptedSocket->Session, TmpAcceptedSocket->Base, g_list_length(TmpAcceptedSocket->Packages));
    }
    ////g_info("##waiting_ack_sockets size:%u", g_list_length(waiting_ack_sockets));
    GList *l;
    for (l = waiting_ack_sockets; l != NULL; l = l->next)
    {
        TmpAcceptedSocket = (struct acked_sock *)(l->data);
        ////g_info("###WaitingSocket:%d Session:%u Base:%u Packages:%u", TmpAcceptedSocket->SocketFD, TmpAcceptedSocket->Session, TmpAcceptedSocket->Base, g_list_length(TmpAcceptedSocket->Packages));
    }
    ////g_info("##ConnectedSockets size:%u", g_hash_table_size(ConnectedSockets));
    GHashTableIter iter_connected_sock;
    int sockfd_connected;
    struct connected_sock *tmp_sock_connected;
    g_hash_table_iter_init(&iter_connected_sock, ConnectedSockets);
    while (g_hash_table_iter_next(&iter_connected_sock, (void *)&sockfd_connected, (void *)&tmp_sock_connected))
    {
        ////g_info("###ConnectedSocket:%d Session:%u Base:%u Packages:%u Pending:%u", tmp_sock_connected->SocketFD, tmp_sock_connected->Session, tmp_sock_connected->Base, g_list_length(tmp_sock_connected->Packages), g_list_length(tmp_sock_connected->PendingPackages));
    }
    ////g_info("#Tcpd Status done.");
    ////g_info("##########");
    //g_debug("Printing Tcp Status done.");
    return 0;
}

//Socket function
ERRCODE tcpd_SOCKET(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength)
{
    ////g_info("SOCKET");

    /* initialize the socket that upper process calls */
    int ret_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ret_sock < 0)
    {
        perror("opening datagram socket");
        exit(2);
    }

    ////g_info("SOCKET return %d", ret_sock);

    /* return the socket initial result */
    if (sendto(sock, (char *)&ret_sock, sizeof(int), 0, (struct sockaddr *)&FrontAddr, sizeof(FrontAddr)) < 0)
    {
        perror("error sending SOCKET return");
        exit(4);
    }
    return 0;
}

//Bind function
ERRCODE tcpd_BIND(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength)
{
    /* fetch the oringin arguments of BIND calls */
    int sockfd;
    struct sockaddr_in addr;

    bzero(buf, sizeof(int));
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, buf, sizeof(int), MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving BIND sockfd");
        exit(4);
    }
    bcopy(buf, (char *)&sockfd, sizeof(int));

    bzero(buf, sizeof(struct sockaddr_in));
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, buf, sizeof(struct sockaddr_in), MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving BIND addr");
        exit(4);
    }
    bcopy(buf, (char *)&addr, sizeof(struct sockaddr_in));

    ////g_info("BIND sockfd:%d, addr.port:%hu, addr.in_addr:%s", sockfd, ntohs(addr.sin_port), inet_ntoa(addr.sin_addr));

    /* bind the socket with name provided */
    int ret_bind = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret_bind < 0)
    {
        perror("error binding socket");
        exit(2);
    }

    struct binded_sock *binded_sock_fd = (struct binded_sock *)malloc(sizeof(struct binded_sock));
    binded_sock_fd->SocketFD = sockfd;
    binded_sock_fd->Accepting = FALSE;
    g_array_append_val(binded_sockets, binded_sock_fd);

    ////g_info("BIND return %d", ret_bind);

    /* send bind result back */
    if (sendto(sock, (char *)&ret_bind, sizeof(int), 0, (struct sockaddr *)&FrontAddr, sizeof(FrontAddr)) < 0)
    {
        perror("error sending BIND return");
        exit(4);
    }
    return 0;
}

//Accept function
ERRCODE tcpd_ACCEPT(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength)
{
    //g_debug("Received ACCEPT message ...");
    int AcceptSocketFD;
    //get accept socket
    bzero(buf, sizeof(int));
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, buf, sizeof(int), MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving ACCEPT sockfd");
        exit(4);
    }
    bcopy(buf, (char *)&AcceptSocketFD, sizeof(int));

    ////g_info("ACCEPT SocketFD:%d", AcceptSocketFD);

    //g_debug("Adding ACCEPT request to binded socket list ...");
    ERRCODE AcceptResult = -1;
    uint i;
    //accept binded sockets
    for (i = 0; i < binded_sockets->len; i++)
    {
        struct binded_sock *sock_tmp = g_array_index(binded_sockets, struct binded_sock *, i);
        if (sock_tmp->SocketFD != AcceptSocketFD)
        {
            continue;
        }
        if (sock_tmp->Accepting)
        {
            g_message("Already Accepting");
            AcceptResult = -2;
            break;
        }
        sock_tmp->Accepting = TRUE;
        sock_tmp->AcceptingAddr = FrontAddr;
        AcceptResult = 0;
    }
    if (AcceptResult == -1)
    {
        g_message("No binded socket found");
    }
    //send accept result back
    if (AcceptResult != 0)
    {
        //g_debug("Sending back error ACCEPT");
        int AcceptRet = -1;
        if (sendto(sock, (char *)&AcceptRet, sizeof(int), 0, (struct sockaddr *)&FrontAddr, sizeof(FrontAddr)) < 0)
        {
            perror("error sending ACCEPT return");
            exit(4);
        }
        return -1;
    }
    //g_message("ACCEPT request added");
    ////g_debug("Tcpd ACCEPT done.");
    return 0;
}

//Receive function
ERRCODE tcpd_RECV(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength)
{
    ////g_debug("Received RECV message ...");
    /* fetch the oringin arguments of RECV calls */
    int sockfd;
    size_t len;

    //get recv sockfd
    bzero(buf, sizeof(int));
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, buf, sizeof(int), MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving RECV sockfd");
        exit(4);
    }
    bcopy(buf, (char *)&sockfd, sizeof(int));

    bzero(buf, sizeof(size_t));
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, buf, sizeof(size_t), MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving RECV len");
        exit(4);
    }
    bcopy(buf, (char *)&len, sizeof(size_t));

    ////g_info("RECV sockfd:%d, len:%zu", sockfd, len);

    /* request too large */

    //g_warning("TODO: RECV");
    ssize_t RecvRet = 0;
    if (len == 0)
    {
        g_message("No data required for RECV request");
        RecvRet = 0;
        if (sendto(sock, (char *)&RecvRet, sizeof(int), 0, (struct sockaddr *)&FrontAddr, sizeof(FrontAddr)) < 0)
        {
            perror("error sending RECV return");
            exit(4);
        }
        return -1;
    }
    uint32_t Session = sockfd;
    struct acked_sock *NowAcceptedSocket = g_hash_table_lookup(acked_sockets, GUINT_TO_POINTER(Session));
    if (NowAcceptedSocket == NULL)
    {
        g_message("No accepted session with %u", Session);
        RecvRet = -2;
        if (sendto(sock, (char *)&RecvRet, sizeof(int), 0, (struct sockaddr *)&FrontAddr, sizeof(FrontAddr)) < 0)
        {
            perror("error sending RECV return");
            exit(4);
        }
        return -1;
    }
    if (NowAcceptedSocket->ReceivingLength != 0)
    {
        g_message("Already waiting on session %u", Session);
        RecvRet = -3;
        if (sendto(sock, (char *)&RecvRet, sizeof(int), 0, (struct sockaddr *)&FrontAddr, sizeof(FrontAddr)) < 0)
        {
            perror("error sending RECV return");
            exit(4);
        }
        return -1;
    }

    NowAcceptedSocket->ReceivingLength = len;
    NowAcceptedSocket->ReceivingAddr = FrontAddr;

    //g_debug("Tcpd RECV done.");
    return 0;
}

//connect function
ERRCODE tcpd_CONNECT(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength)
{
    //g_debug("Received CONNECT message ...");
    //g_debug("Fetching origin arguments of CONNECT ...");
    int sockfd;
    struct sockaddr_in addr;

    //receive connect sockft
    bzero(buf, sizeof(int));
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, buf, sizeof(int), MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving CONNECT sockfd");
        exit(4);
    }
    bcopy(buf, (char *)&sockfd, sizeof(int));

    bzero(buf, sizeof(struct sockaddr_in));
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, buf, sizeof(struct sockaddr_in), MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving CONNECT addr");
        exit(4);
    }
    bcopy(buf, (char *)&addr, sizeof(struct sockaddr_in));

    ////g_info("CONNECT sockfd:%d, addr.port:%hu, addr.in_addr:%s", sockfd, ntohs(addr.sin_port), inet_ntoa(addr.sin_addr));
    /* record the connected name within socket */
    int ConnectRet = 0;

    //g_debug("Constructing connected_sock ...");
    struct connected_sock *NowConnectedSocket = (struct connected_sock *)malloc(sizeof(struct connected_sock));
    NowConnectedSocket->SocketFD = sockfd;
    NowConnectedSocket->Session = rand();
    NowConnectedSocket->Base = 0;
    NowConnectedSocket->MinSeq = 0;
    NowConnectedSocket->Packages = NULL;
    NowConnectedSocket->PendingPackages = NULL;
    NowConnectedSocket->ServerAddr = addr;
    g_hash_table_insert(ConnectedSockets, GINT_TO_POINTER(NowConnectedSocket->SocketFD), NowConnectedSocket);

    //g_debug("Sending back conncet result ...");
    /* send back connect result (alwalys success) */
    if (sendto(sock, (char *)&ConnectRet, sizeof(int), 0, (struct sockaddr *)&FrontAddr, sizeof(FrontAddr)) < 0)
    {
        perror("error sending CONNECT return");
        exit(4);
    }
    //g_debug("Tcpd CONNECT done.");
    return 0;
}

//send function
ERRCODE tcpd_SEND(struct sockaddr_in FrontAddr, socklen_t FrontAddrLength)
{
    //g_debug("Received SEND message");
    //g_debug("Fetching origin arguments of SEND ...");
    int sockfd;
    size_t len;
    //receive send sockfd
    bzero(buf, sizeof(int));
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, buf, sizeof(int), MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving SEND sockfd");
        exit(4);
    }
    bcopy(buf, (char *)&sockfd, sizeof(int));
    //get send len
    bzero(buf, sizeof(size_t));
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, buf, sizeof(size_t), MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving SEND len");
        exit(4);
    }
    bcopy(buf, (char *)&len, sizeof(size_t));
    //fetch data
    //g_debug("Fetching send data ...");
    if (BUF_LEN < len)
    {
        perror("error request length larger than buffer");
        exit(5);
    }
    /* fetch data to send from upper process */
    if (recvfrom(sock, buf, len, MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving SEND buf");
        exit(4);
    }

    ////g_info("SEND sockfd:%d, len:%zu", sockfd, len);

    //g_debug("Find connected socket struct ...");
    struct connected_sock *NowConnectedSocket = g_hash_table_lookup(ConnectedSockets, GINT_TO_POINTER(sockfd));
    if (NowConnectedSocket == NULL)
    {
        perror("Socket not connected");
        exit(1);
    }

#ifdef __WITH_TROLL
    /* wrap data with troll message structure */
    //g_debug("Constructing troll message package ...");
    struct TrollMessageStruct *NowTrollMessage = (struct TrollMessageStruct *)malloc(sizeof(struct TrollMessageStruct));
    NowTrollMessage->header = NowConnectedSocket->ServerAddr;
    NowTrollMessage->header.sin_family = htons(AF_INET);
    NowTrollMessage->Session = NowConnectedSocket->Session;
    NowTrollMessage->SeqNum = NowConnectedSocket->Base;
    NowConnectedSocket->Base++;
    bcopy(buf, NowTrollMessage->body, len);
    NowTrollMessage->len = len;
    NowTrollMessage->CheckSum = CHECKSUM_ALGORITHM(0, ((char *)(NowTrollMessage)) + CheckSumOffset, sizeof(*NowTrollMessage) - CheckSumOffset);
    ////g_info("Checksum is %u", NowTrollMessage->CheckSum);
    //g_debug("Appending troll package after now connected socket packages");
    NowConnectedSocket->PendingPackages = g_list_append(NowConnectedSocket->PendingPackages, NowTrollMessage);
    in_buffer_packages++;
#endif

    // ////g_info("SEND return %zd",ret_send);

    //g_debug("Sending send result alwalys success...");
    ssize_t SendRet = len;
    /* send result back */
    if (sendto(sock, (char *)&SendRet, sizeof(int), 0, (struct sockaddr *)&FrontAddr, sizeof(FrontAddr)) < 0)
    {
        perror("error sending SEND return");
        exit(4);
    }
    //g_debug("Tcpd SEND done.");
    return 0;
}

void global_init()
{
    srand((unsigned)time(NULL));
    binded_sockets = g_array_new(FALSE, FALSE, sizeof(struct binded_sock *));
    acked_sockets = g_hash_table_new(NULL, NULL);
    ConnectedSockets = g_hash_table_new(NULL, NULL);
}

void socket_init()
{
    /* create internal communication socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        perror("opening datagram socket");
        exit(2);
    }

    /* create name with parameters and bind name to socket */
    tcpd_name.sin_family = AF_INET;
    if (strcmp(end_name, "server") == 0)
    {
        tcpd_name.sin_port = htons(atoi(SERVER_TCPD_PORT));
    }
    else if (strcmp(end_name, "client") == 0)
    {
        tcpd_name.sin_port = htons(atoi(CLIENT_TCPD_PORT));
    }
    else
    {
        tcpd_name.sin_port = htons(atoi(end_name));
    }
    tcpd_name.sin_addr.s_addr = INADDR_ANY;

    /* bind the socket with name */
    if (bind(sock, (struct sockaddr *)&tcpd_name, sizeof(tcpd_name)) < 0)
    {
        perror("error binding socket");
        exit(2);
    }
    tcpd_name_len = sizeof(tcpd_name);
    /* Find assigned port value and print it for client to use */
    if (getsockname(sock, (struct sockaddr *)&tcpd_name, &tcpd_name_len) < 0)
    {
        perror("error getting sock name");
        exit(3);
    }

    //g_debug("Fetching localhost entity");
    troll_server_host = gethostbyname("localhost");
    if (troll_server_host == NULL)
    {
        perror("localhost: unknown host");
        exit(3);
    }

#ifdef __WITH_TROLL
    /* construct name of socket to send to */
    troll_addr.sin_family = AF_INET;
    bcopy((void *)troll_server_host->h_addr, (void *)&troll_addr.sin_addr, troll_server_host->h_length);
    troll_addr.sin_port = htons(atoi(TROLL_PORT));
#endif

    ////g_info("Server waiting on port # %d", ntohs(tcpd_name.sin_port));

    //g_debug("Setting up timer ...");
    //g_debug("Constructing driver socket ...");
    sock_timer_driver = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_timer_driver < 0)
    {
        perror("error opening driver socket");
        exit(1);
    }
    ////g_info("Constructed driver socket with fd %d", sock_timer_driver);
    //g_debug("Initializing Timer Addr ...");
    char TimerPort[8] = TIMER_PORT;
    timer_addr.sin_family = AF_INET;
    bcopy((char *)troll_server_host->h_addr, (char *)&timer_addr.sin_addr, troll_server_host->h_length);
    timer_addr.sin_port = htons(atoi(TimerPort));
}

void re_register_fd()
{
    FD_ZERO(&sock_fd);
    FD_SET(sock_timer_driver, &sock_fd);
    max_socket = sock_timer_driver;
    //login the socket fd
    if (in_buffer_packages < MAX_BUFFER_PACKAGES)
    {
        FD_SET(tcpd_msg_socket, &sock_fd);
        max_socket = tcpd_msg_socket > max_socket ? tcpd_msg_socket : max_socket;
    }

    uint i;
    for (i = 0; i < binded_sockets->len; i++)
    {
        struct binded_sock *sock_tmp = g_array_index(binded_sockets, struct binded_sock *, i);
        FD_SET(sock_tmp->SocketFD, &sock_fd);
        max_socket = sock_tmp->SocketFD > max_socket ? sock_tmp->SocketFD : max_socket;
    }
    GHashTableIter iter_connected_sock;
    int sockfd_connected;
    struct connected_sock *tmp_sock_connected;
    g_hash_table_iter_init(&iter_connected_sock, ConnectedSockets);
    while (g_hash_table_iter_next(&iter_connected_sock, (void *)&sockfd_connected, (void *)&tmp_sock_connected))
    {
        FD_SET(tmp_sock_connected->SocketFD, &sock_fd);
        max_socket = tmp_sock_connected->SocketFD > max_socket ? tmp_sock_connected->SocketFD : max_socket;
        ////g_info("ConnectedSocket %d added to waiting sockets", tmp_sock_connected->SocketFD);
    }
}

void process_tcpd_message()
{
    struct sockaddr_in FrontAddr;
    socklen_t FrontAddrLength;
    /* fetch the function that upper local process calls */
    char tcpd_msg[TCPD_MSG_LEN];
    FrontAddrLength = sizeof(FrontAddr);
    if (recvfrom(sock, tcpd_msg, TCPD_MSG_LEN, MSG_WAITALL, (struct sockaddr *)&FrontAddr, &FrontAddrLength) < 0)
    {
        perror("error receiving");
        exit(4);
    }
    //g_debug("Server receives: %s", tcpd_msg);
    /* switch by the function */
    if (strcmp(tcpd_msg, "SOCKET") == 0)
    {
        tcpd_SOCKET(FrontAddr, FrontAddrLength);
    }
    else if (strcmp(tcpd_msg, "BIND") == 0)
    {
        tcpd_BIND(FrontAddr, FrontAddrLength);
    }
    else if (strcmp(tcpd_msg, "ACCEPT") == 0)
    {
        tcpd_ACCEPT(FrontAddr, FrontAddrLength);
    }
    else if (strcmp(tcpd_msg, "RECV") == 0)
    {
        tcpd_RECV(FrontAddr, FrontAddrLength);
    }
    else if (strcmp(tcpd_msg, "CONNECT") == 0)
    {
        tcpd_CONNECT(FrontAddr, FrontAddrLength);
    }
    else if (strcmp(tcpd_msg, "SEND") == 0)
    {
        tcpd_SEND(FrontAddr, FrontAddrLength);
    }
}

void handle_back_imformation()
{
    handle_ack_back();
    handle_recv_back();
    handle_send_all_back();
}

void get_data_from_sockets()
{
    uint i;
    //get data from sockets
    for (i = 0; i < binded_sockets->len; i++)
    {
        struct binded_sock *sock_tmp = g_array_index(binded_sockets, struct binded_sock *, i);
        if (FD_ISSET(sock_tmp->SocketFD, &sock_fd))
        {
            recv_data(sock_tmp);
        }
    }
}
