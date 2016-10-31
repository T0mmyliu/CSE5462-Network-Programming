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
void address_init();
void socket_init();
void socket_bind();
void pipe_init();