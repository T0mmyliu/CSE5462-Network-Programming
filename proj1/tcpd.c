#include <netinet/in.h>
#include <sys/types.h>
#include <string.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#define POLY 0x8408

#include "address.h"

typedef struct TrollHeader
{
    struct sockaddr_in send_to;
    struct tcphdr tcp_header;
    char body[800];
} TrollHeader;

void print_test(char *message, int size);

unsigned short crc16(char *data_p, int length);

int main(int argc, char *argv[])
{
    struct sockaddr_in local_addr, remote_addr, troll_addr, server_addr, client_addr;
    int sock_local, sock_remote;
    char buffer_local[1000], buffer_remote[1000];
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

    if (bind(sock_local, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("local bind");
    }

    if (bind(sock_remote, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0)
    {
        perror("remote bind");
    }

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(sock_local, &readfds);
        FD_SET(sock_remote, &readfds);
        printf("waiting for a packet.\n");

        if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL))
        {
        }

        printf("One packet comes.\n");

        if (FD_ISSET(sock_local, &readfds))
        {
            TrollHeader head;
            bzero(&buffer_local, sizeof(buffer_local));
            head.send_to.sin_family = htons(AF_INET);
            head.send_to.sin_addr.s_addr = inet_addr(SERVER_IP);
            head.send_to.sin_port = htons(REMOTE_PORT);

            int bytes;
            socklen_t len = sizeof(local_addr);
            bytes = recvfrom(sock_local, buffer_local, sizeof(buffer_local), 0,
                             (struct sockaddr *)&local_addr, &len);
            if (bytes < 0)
            {
                perror("Receing datagram from ftpc.");
                exit(1);
            }

            printf("Bytes from client :%d\n", bytes);

            memcpy(&head.body, &buffer_local, bytes);

            //calculate the checksum
            //print_test((void *)&head, sizeof(head));
            unsigned short crc_16 = crc16((void *)&head.tcp_header, sizeof(head.tcp_header) + bytes);
            //unsigned short crc_16 = crc16((void *)&head, sizeof(head.send_to) + sizeof(head.tcp_header) + bytes);
            memcpy(&head.tcp_header.check, &crc_16, sizeof(crc_16));

            bytes = sendto(sock_local, (char *)&head, sizeof(head.send_to) + sizeof(head.tcp_header) + bytes, 0,
                           (struct sockaddr *)&troll_addr, sizeof(troll_addr));

            printf("Send to Troll: %d.\n", bytes);
        }

        if (FD_ISSET(sock_remote, &readfds))
        {
            struct TrollHeader rec_head;
            bzero(&rec_head, sizeof(rec_head));
            bzero(&buffer_remote, sizeof(buffer_remote));

            int bytes;
            socklen_t len = sizeof(remote_addr);
            bytes = recvfrom(sock_remote, buffer_remote, sizeof(buffer_remote), 0,
                             (struct sockaddr *)&remote_addr, &len);

            memcpy(&rec_head, buffer_remote, bytes);

            unsigned short crc_send = rec_head.tcp_header.check;
            memcpy(&rec_head.tcp_header.check, "\0", 2);

            unsigned short crc_recv = crc16((void *)&rec_head.tcp_header, bytes - sizeof(rec_head.send_to));
            //unsigned short crc_recv = crc16((void *)&rec_head, bytes);
            //print_test((void *)&rec_head, sizeof(rec_head));

            if (crc_recv == crc_send)
            {
                printf("********************Pass the crc16 checksum test.\n");
            }
            else
            {
                printf("********************Fail to pass the crc16 checksum test. Send crc16 is %#4x, Recv crc16 is %#4x\n", crc_send, crc_recv);
            }

            if (bytes < 0)
            {
                perror("Receing datagram from troll.");
                exit(1);
            }

            printf("Bytes from troll :%d\n", bytes);

            bytes = sendto(sock_remote, buffer_remote + 16, bytes - 16, 0,
                           (struct sockaddr *)&server_addr, sizeof(server_addr));

            printf("Send to server: %d.\n", bytes);
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