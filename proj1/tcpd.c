#include <netinet/in.h>
#include <sys/types.h>
#include <string.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <math.h>

#include "timer.h"
#include "address.h"

#define POLY 0x8408

typedef struct TrollHeader
{
    struct sockaddr_in send_to;
    struct tcphdr tcp_header;
    char body[800];
} TrollHeader;

typedef struct send_buffer
{
    int ack_index;
    int send_index;
    int upper_bound;
    int full;
    char buff[5000];
} send_buffer;

typedef struct recv_buffer
{
    int ack_index;
    int send_index;
    int upper_bound;
    int full;
    char buff[5000];
} recv_buffer;

typedef struct time_record
{
    struct timeval start_time;
    int seq;
} time_record;

int pipefd[2];
int pipefd_from_timer[2];
time_record record_buf[30];
int record_index;
int debug = 0;
char tmp_save[1000];
int save_size;

void print_test(char *message, int size);
unsigned short crc16(char *data_p, int length);
int random_ini_seq();
double update_RTO(double RTT);
void record(int seq);
double get_rtt(int ack);
int buffer_empty_size(send_buffer *buf);
ssize_t buffer_recvfrom(int sockfd, send_buffer *buf, int flags,
                        struct sockaddr *src_addr, socklen_t *addrlen);
void read_from_buffer(char *buf, send_buffer *send_buf, int start, int len);
void update_ack(send_buffer *buf, int ack_byte);

int main(int argc, char *argv[])
{
    struct sockaddr_in local_addr, remote_addr, troll_addr, server_addr, client_addr, ack_addr;
    int sock_local, sock_remote, sock_ack;
    char buffer_local[1000], buffer_remote[1000];
    int seq = 0;
    double RTO = 100.0;
    int cpid = 0;
    fd_set readfds;

    if (argc != 1)
    {
        printf("Error, the usage is: \"./tcpd\"\n");
        exit(1);
    }

    sock_local = socket(AF_INET, SOCK_DGRAM, 0);
    sock_remote = socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&local_addr, sizeof(local_addr));
    bzero(&remote_addr, sizeof(remote_addr));
    bzero(&troll_addr, sizeof(troll_addr));
    bzero(&server_addr, sizeof(server_addr));
    bzero(&client_addr, sizeof(client_addr));

    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(LOCAL_PORT);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(REMOTE_PORT);
    remote_addr.sin_addr.s_addr = INADDR_ANY;

    troll_addr.sin_family = AF_INET;
    troll_addr.sin_port = htons(TROLL_PORT);
    troll_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(REMOTE_PORT);
    client_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);

    if (bind(sock_local, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("local bind");
    }

    if (bind(sock_remote, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0)
    {
        perror("remote bind");
    }

    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    if (pipe(pipefd_from_timer) == -1)
    {
        perror("pipefd_from_timer");
        exit(EXIT_FAILURE);
    }

    //create a new process, start the timer
    cpid = fork();
    if (cpid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (cpid == 0)
    {
        //timer_process_start();
        exit(EXIT_SUCCESS);
    }
    else
    {
        send_buffer send_buf;
        send_buf.upper_bound = 4999;
        send_buf.ack_index = -1;
        send_buf.send_index = -1;

        while (1)
        {
            struct timeval waitting;
            waitting.tv_sec = 0;
            waitting.tv_usec = 1000000;
            select(0, NULL, NULL, NULL, &waitting);

            FD_ZERO(&readfds);
            FD_SET(sock_local, &readfds);
            FD_SET(sock_remote, &readfds);
            FD_SET(pipefd_from_timer[0], &readfds);

            //printf("waiting for a packet.\n");

            if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL))
            {
            }

            //printf("One packet comes.\n");
            if (FD_ISSET(pipefd_from_timer[0], &readfds))
            {
                //TODO fromtimer
            }

            if (FD_ISSET(sock_local, &readfds) && buffer_empty_size(&send_buf) > 0)
            {
                printf("from local port.\n");
                TrollHeader head;
                bzero(buffer_local, sizeof(buffer_local));

                head.send_to.sin_family = htons(AF_INET);
                head.send_to.sin_addr.s_addr = inet_addr(SERVER_IP);
                head.send_to.sin_port = htons(REMOTE_PORT);

                int bytes;
                socklen_t len = sizeof(local_addr);

                int pre_send_index = send_buf.send_index;
                bytes = buffer_recvfrom(sock_local, &send_buf, 0,
                                        (struct sockaddr *)&local_addr, &len);
                read_from_buffer(buffer_local, &send_buf, pre_send_index + 1, bytes);

                if (bytes == 0)
                {
                    continue;
                }

                if (debug == 1)
                {
                    printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
                    printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
                    printf("in buffer %s\n", buffer_local);
                    printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
                    printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
                }

                if (bytes < 0)
                {
                    perror("Receing datagram from ftpc.");
                    exit(1);
                }

                printf("Bytes from client :%d\n", bytes);

                memcpy(&head.body, &buffer_local, bytes);

                unsigned short crc_16 = crc16((void *)&head.tcp_header, sizeof(head.tcp_header) + bytes);
                memcpy(&head.tcp_header.check, &crc_16, sizeof(crc_16));

                if (seq == 0)
                {
                    seq = random_ini_seq();
                }

                head.tcp_header.seq = seq;
                head.tcp_header.ack = 0;
                if (debug == 1)
                {
                    printf("\n");
                    printf("ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n");
                    printf("ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n");
                    printf("%s\n", head.body);
                    printf("ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n");
                    printf("ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n");
                    printf("\n");
                }
                sendto(sock_remote, (char *)&head, sizeof(head.send_to) + sizeof(head.tcp_header) + bytes, 0,
                       (struct sockaddr *)&troll_addr, sizeof(troll_addr));

                send_to_timer(RTO, head.tcp_header.seq);
                record(head.tcp_header.seq);
                seq += bytes;
                //printf("Send to Troll: %d.\n", bytes);
            }

            if (FD_ISSET(sock_remote, &readfds))
            {
                printf("from remote port.\n");

                struct TrollHeader rec_head;
                bzero(&rec_head, sizeof(rec_head));
                bzero(buffer_remote, sizeof(buffer_remote));

                int bytes;
                socklen_t len = sizeof(remote_addr);
                bytes = recvfrom(sock_remote, buffer_remote, sizeof(buffer_remote), 0,
                                 (struct sockaddr *)&remote_addr, &len);

                memcpy(&rec_head, buffer_remote, bytes);

                if (debug == 1)
                {
                    printf("\n");
                    printf("ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss\n");
                    printf("ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss\n");
                    printf("%s\n", rec_head.body);
                    printf("ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss\n");
                    printf("ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss\n");
                    printf("\n");
                }

                if (rec_head.tcp_header.ack == 0)
                {
                    printf("server: recv from client\n");

                    unsigned short crc_send = rec_head.tcp_header.check;
                    memcpy(&rec_head.tcp_header.check, "\0", 2);

                    unsigned short crc_recv = crc16((void *)&rec_head.tcp_header, bytes - sizeof(rec_head.send_to));

                    if (crc_recv == crc_send)
                    {
                        //printf("Pass the crc16 checksum test.\n");
                    }
                    else
                    {
                        //printf("Fail to pass the crc16 checksum test. Send crc16 is %#4x, Recv crc16 is %#4x\n", crc_send, crc_recv);
                    }

                    if (bytes < 0)
                    {
                        perror("Receing datagram from troll.");
                        exit(1);
                    }
                    printf("Bytes from troll :%d\n", bytes);

                    bytes = sendto(sock_local, (char *)(&rec_head.body), bytes - sizeof(rec_head.tcp_header) - sizeof(rec_head.send_to), 0,
                                   (struct sockaddr *)&server_addr, sizeof(server_addr));

                    printf("REV, SEQ IS %d\n", rec_head.tcp_header.seq);
                    //send ack back to client
                    TrollHeader ack_packet;
                    ack_packet.tcp_header.ack = 1;
                    ack_packet.tcp_header.seq = rec_head.tcp_header.seq;
                    ack_packet.tcp_header.ack_seq = ack_packet.tcp_header.seq + bytes;
                    bytes = sendto(sock_remote, (char *)(&ack_packet), sizeof(ack_packet), 0,
                                   (struct sockaddr *)&client_addr, sizeof(client_addr));

                    printf("Send ack packet back....send %d bytes\n", bytes);
                }

                else if (rec_head.tcp_header.ack == 1)
                {
                    printf("client: recv from server, ack packet.\n");
                    double rtt = 0;
                    //first calculate the time intercal
                    rtt = get_rtt(rec_head.tcp_header.seq);
                    int ack_byte = rec_head.tcp_header.ack_seq - rec_head.tcp_header.seq;
                    update_ack(&send_buf, ack_byte);
                    //second update the RTO time
                    double next_rto = update_RTO(rtt);

                    printf("***********************************************\n");
                    printf("***********************************************\n");
                    printf("previous rto : %f, next rto : %f\n", RTO, next_rto);
                    printf("***********************************************\n");
                    printf("***********************************************\n");

                    RTO = next_rto;

                    //third delete the node from timer
                    delete_from_timer(rec_head.tcp_header.seq);
                }
            }
        }
    }

    return 0;
}

/*
 * follows a working code to calculate crc16 CCITT(0xFFFF)
 */
unsigned short crc16(char *data_p, int length)
{
    unsigned char i;
    unsigned int data;
    unsigned int crc = 0xffff; //16 bits, all 1s

    if (length == 0)
        return (~crc);

    do
    {
        for (i = 0, data = (unsigned int)0xff & *data_p++;
             i < 8;
             i++, data >>= 1) //Iterting over the bits of the data to be checksummed.
        {
            if ((crc & 0x0001) ^ (data & 0x0001)) //checks if the bits differ
                crc = (crc >> 1) ^ POLY;          //Shifting bits left by 1, and XORing with 0x8408.
            else
                crc >>= 1;
        }
    } while (--length);

    crc = ~crc; //flips bits
    data = crc;
    crc = (crc << 8) | (data >> 8 & 0xff); //crc shifted 8 bits to the left OR'd with data shifted 8 to right AND'd with 255 or 11111111

    return (crc);
}

void print_test(char *message, int size)
{
    char *p = message;
    int i = 0;
    for (i; i < size; i++)
    {
        if (*p == '\0')
            printf("# ");
        if (*p >= '0' && *p <= '9')
            continue;
        else
            printf("%#x ", *p);

        p++;
    }
    printf("\n");
}

int random_ini_seq()
{
    int random = 1;
    return random;
}

double update_RTO(double RTT)
{
    const double alpha = 0.125;
    const double beta = 0.25;

    static double SRTT = 0;
    static double DevRTT = 0;

    double next_rto = 0;

    SRTT = SRTT + alpha * (RTT - SRTT);
    DevRTT = (1 - beta) * DevRTT + beta * fabs(RTT - SRTT);
    next_rto = 1 * SRTT + 4 * DevRTT;

    return next_rto;
}

void record(int seq)
{
    gettimeofday(&record_buf[record_index].start_time.tv_sec, &record_buf[record_index].start_time.tv_usec);
    record_buf[record_index].seq = seq;
    record_index = (record_index + 1) % 30;
    return;
}

double get_rtt(int ack)
{
    struct timeval end_time;
    double rtt = -1.0;

    gettimeofday(&end_time.tv_sec, &end_time.tv_usec);

    int i;
    for (i = 0; i < 30; i++)
    {
        if (ack == record_buf[i].seq)
        {
            rtt = (double)(end_time.tv_sec - record_buf[i].start_time.tv_sec) + (end_time.tv_usec - record_buf[i].start_time.tv_usec) / 1000000.0;
            printf("***********************************************\n");
            printf("***********************************************\n");
            printf("end time: %ld, start time %ld\n", end_time.tv_sec, record_buf[i].start_time.tv_sec);
            printf("ack: %d, seq: %d\n", ack, record_buf[i].seq);
            printf("RTT: % f\n", rtt);
            printf("***********************************************\n");
            printf("***********************************************\n");
        }
    }

    return rtt;
}

int buffer_empty_size(send_buffer *buf)
{
    if (buf->full == 1)
    {
        return 0;
    }

    if (buf->send_index == -1)
    {
        return 5000;
    }

    //|***********send_index||...........ack_index||**********|
    if (buf->send_index < buf->ack_index)
    {
        return buf->ack_index - buf->send_index;
    }

    // |....free......ack_index||******************send_index||.....free......|
    return (sizeof(buf->buff) - (buf->send_index - buf->ack_index));
}

ssize_t buffer_recvfrom(int sockfd, send_buffer *buf, int flags,
                        struct sockaddr *src_addr, socklen_t *addrlen)
{
    char tmp_buf[1000] = {0};
    int cap;
    int byte;

    cap = buffer_empty_size(buf);
    if (cap == 0)
    {
        printf("CAP IS 0\n");
        return 0;
    }

    //TODO BUGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG TO FIXXXXXXXXXXX
    byte = recvfrom(sockfd, tmp_buf, cap, flags, src_addr, addrlen);
    if (byte < 800)
    {
        bzero(tmp_save, 1000);
        save_size = recvfrom(sockfd, tmp_save, 800-cap, flags, src_addr, addrlen);
    }

    printf("\n\n\n\n\n\n\n\n");
    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!bytebytebytebyte:%d\n", byte);
    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!bytebytebytebyte:%d\n", byte);
    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!bytebytebytebyte:%d\n", byte);
    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!bytebytebytebyte:%d\n", byte);
    printf("\n\n\n\n\n\n\n\n");

    if (debug == 1)
    {
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        if (byte == 4)
        {
            int *len = (int *)&tmp_buf[0];
            printf("byte recvd: %d , content: int: %d, char %s \n", byte, *len, tmp_buf);
        }
        else
        {
            printf("byte recvd: %d , content: %s\n", byte, tmp_buf);
        }
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    }

    //|***********send_index||...........ack_index||**********|

    if (buf->send_index < buf->ack_index)
    {
        char *dst = (char *)(&buf->buff[buf->send_index + 1]);

        memcpy(dst, tmp_buf, byte);
        buf->send_index += byte;
        if (buf->send_index == buf->ack_index)
        {
            buf->full = 1;
        }

        if (debug == 1)
        {
            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            printf("mode 1 : %s\n", tmp_buf);
            printf("ack index: %d\n", buf->ack_index);
            printf("send index: %d\n", buf->send_index);
            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        }

        return byte;
    }

    // |....free......ack_index||******************send_index||.....free......|
    int left = byte;
    if (buf->send_index != buf->upper_bound)
    {
        int right_space = buf->upper_bound - buf->send_index;
        char *dst = (char *)buf->buff + (buf->send_index + 1);

        if (right_space >= byte)
        {
            memcpy(dst, tmp_buf, byte);

            if (debug == 1)
            {
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                if (buf->send_index == -1)
                {
                    printf("mode 2 ,right_space %d\ncontent : %d\n", right_space, *(int *)(&buf->buff[buf->send_index + 1]));
                }
                else
                {
                    printf("mode 2 ,right_space %d\ncontent : %s\n", right_space, &buf->buff[buf->send_index + 1]);
                }
                printf("ack index: %d\n", buf->ack_index);
                printf("send index: %d\n", buf->send_index);
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            }
            buf->send_index += byte;

            return byte;
        }

        else
        {
            memcpy(dst, tmp_buf, right_space);
            buf->send_index = -1;

            char *src = (char *)(&tmp_buf[right_space]);
            left = byte - right_space;
            dst = (char *)(&buf->buff[0]);
            memcpy(dst, src, left);
            buf->send_index += left;
            if (buf->send_index == buf->ack_index)
            {
                buf->full = 1;
            }

            if (debug == 1)
            {
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                printf("%s\n", tmp_buf);
                printf("ack index: %d\n", buf->ack_index);
                printf("send index: %d\n", buf->send_index);
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            }

            return byte;
        }
    }
}

void update_ack(send_buffer *buf, int ack_byte)
{
    printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
    printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
    printf("ack_byte is : %d\n", ack_byte);

    printf("update_ack, previous ack : %d\n", buf->ack_index);

    if (ack_byte == 0)
    {
        return;
    }

    if (buf->full == 1)
    {
        buf->full = 0;
    }

    buf->ack_index = (buf->ack_index + ack_byte) % (sizeof(buf->buff));

    printf("update_ack, new ack : %d\n", buf->ack_index);
    printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
    printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");

    return;
}

void read_from_buffer(char *buf, send_buffer *send_buf, int start, int len)
{
    if ((start + len) < sizeof(send_buf->buff))
    {
        printf("\n\n\n\n\n\n\n\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!read_from_buffer:%d\n", len);
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!read_from_buffer:%d\n", len);
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!read_from_buffer:%d\n", len);
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!read_from_buffer:%d\n", len);
        printf("\n\n\n\n\n\n\n\n");

        memcpy(buf, &send_buf->buff[start], len);
        return;
    }

    int right_size = sizeof(send_buf->buff) - start;
    int left_size = len - right_size;
    memcpy(buf, &send_buf->buff[start], right_size);
    memcpy(buf + right_size, &send_buf->buff[0], left_size);
    return;
}