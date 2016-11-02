#include "tcpd.h"

static int debug = 0;

static struct sockaddr_in client_local_addr, server_remote_addr,
    client_remote_addr;

static struct sockaddr_in to_client_troll, to_server_troll, to_server_ftp, to_server_tcpd, to_client_ftp, to_client_tcpd;

static int sock_client_local, sock_client_remote,
    sock_server_remote;

static time_record record_buf[30];
static int record_index;
static int seq;
static char tmp_save_buf[1000];

int pipefd[2];
int pipefd_from_timer[2];

int main(int argc, char *argv[])
{
    double RTO = 100.0;
    fd_set readfds;
    char buffer_local[1000], buffer_remote[1000];
    char tmp_save[1000];
    int save_size;
    int cpid = 0;
    struct timeval waitting;
    int bytes;
    my_window window_client, window_server;

    if (argc != 1)
    {
        perror("Error, the usage is: \"./tcpd\"\n");
        exit(EXIT_FAILURE);
    }

    address_init();
    socket_init();
    socket_bind();
    pipe_init();
    window_init(&window_client);
    window_init(&window_server);
    seq = random_ini_seq();

    //create a new process, start the timer

    if ((cpid = fork()) == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (cpid == 0) //timer process
    {
        timer_process_start();
        exit(EXIT_SUCCESS);
    }

    else //tcpd process
    {
        while (1)
        {
            FD_ZERO(&readfds);
            FD_SET(sock_client_local, &readfds);
            FD_SET(sock_server_remote, &readfds);
            FD_SET(sock_client_remote, &readfds);
            FD_SET(pipefd_from_timer[0], &readfds);

            waitting.tv_sec = 0;
            waitting.tv_usec = 100000;

            select(1, NULL, NULL, NULL, &waitting);

            select(FD_SETSIZE, &readfds, NULL, NULL, NULL);

            if (FD_ISSET(pipefd_from_timer[0], &readfds))
            {
                //TODO fromtimer
            }

            if (FD_ISSET(sock_client_local, &readfds))
            {
                if (window_full(&window_client))
                {
                    block_sending_and_save();
                    continue;
                }

                send_recv_to_ftpc();

                socklen_t len = sizeof(client_local_addr);
                bzero(buffer_local, sizeof(buffer_local));
                bytes = recvfrom(sock_client_local, buffer_local, sizeof(buffer_local), 0,
                                 (struct sockaddr *)&client_local_addr, &len);
                printf("Bytes from client :%d\n", bytes);
                TrollHeader head;
                generate_TrollHeader(&head, buffer_local, bytes);

                memcpy(&head.tcp_header.check, "\0", 2);
                head.tcp_header.check = crc16((void *)&head.tcp_header, sizeof(head.tcp_header) + bytes);

                {
                    printf("*****************\n");
                    int i = 0;
                    char *p = (char *)&head.tcp_header;
                    int size = sizeof(head.tcp_header) + bytes;
                    printf("size: %d\n", size);
                    for (; i < sizeof(head.tcp_header) + bytes; i++)
                    {
                        printf("%c", *p);
                        p++;
                    }
                    printf("\n*****************\n");
                }

                sendto(sock_server_remote, (char *)&head, sizeof(head.send_to) + sizeof(head.tcp_header) + bytes, 0,
                       (struct sockaddr *)&to_client_troll, sizeof(to_client_troll));
                send_to_timer(RTO, head.tcp_header.seq);
                save_packet(&window_client, &head);
                print_window(&window_client);
                record(head.tcp_header.seq);

                seq = (seq + bytes) % 65535;

                printf("Send to Troll: %d.\n", bytes);
            }

            if (FD_ISSET(sock_client_remote, &readfds))
            {
                printf("ACK ACK \n ACK ACK\n");
                if (debug == 1)
                {
                    printf("receive ack packet from server.\n");
                }
                int bytes;
                struct TrollHeader head_received;
                socklen_t len;
                unsigned short crc_send;
                unsigned short crc_recv;

                bzero(&head_received, sizeof(head_received));
                len = sizeof(client_remote_addr);

                bytes = recvfrom(sock_client_remote, (char *)&head_received, sizeof(head_received), 0,
                                 (struct sockaddr *)&client_remote_addr, &len);
                int i;
                for (i = (window_client.last_ack + 1) % WINDOW_SIZE; i != (window_client.last_send + 1) % WINDOW_SIZE; i = (i + 1) % WINDOW_SIZE)
                {
                    if (head_received.tcp_header.seq == window_client.troll_headers[i].tcp_header.seq)
                    {
                        printf("packet index :%d seq: %d\n", i, head_received.tcp_header.seq);
                        window_client.ack_bitmap[i] = 1;
                        break;
                    }

                    else
                    {
                        printf("packet index :%d seq: %d\n", i, head_received.tcp_header.seq);
                        printf("packet index :%d  window_client seq: %d\n", i, window_client.troll_headers[i].tcp_header.seq);
                    }
                }

                int cnt = WINDOW_SIZE - window_client.num_empty;
                for (i = (window_client.last_ack + 1) % WINDOW_SIZE; cnt > 0; i = (i + 1) % WINDOW_SIZE, cnt--)
                {
                    if (window_client.ack_bitmap[i] == 1)
                    {
                        window_client.last_ack = (window_client.last_ack + 1) % WINDOW_SIZE;
                        window_client.num_empty++;
                    }

                    else
                    {
                        break;
                    }
                }

                print_window(&window_client);

                double rtt = 0;
                //first calculate the time intercal
                rtt = get_rtt(head_received.tcp_header.seq);
                int ack_byte = head_received.tcp_header.ack_seq - head_received.tcp_header.seq;
                //second update the RTO time
                double next_rto = update_RTO(rtt);
                printf("previous rto : %f, next rto : %f\n", RTO, next_rto);

                RTO = next_rto;

                //third delete the node from timer
                delete_from_timer(head_received.tcp_header.seq);

                printf("\n\n\n\n\n\n\n\n");
            }

            if (FD_ISSET(sock_server_remote, &readfds))
            {
                if (debug == 1)
                {
                    printf("receive ack packet from client.\n");
                }

                int bytes;
                struct TrollHeader head_received;
                socklen_t len;
                unsigned short crc_send;
                unsigned short crc_recv;

                bzero(&head_received, sizeof(head_received));
                len = sizeof(server_remote_addr);

                bytes = recvfrom(sock_server_remote, (char *)&head_received, sizeof(head_received), 0,
                                 (struct sockaddr *)&server_remote_addr, &len);

                printf("seq : %d , bytes : %d\n", head_received.tcp_header.seq, bytes);

                //printf("head received: %s\n", head_received.body);

                crc_send = head_received.tcp_header.check;

                memcpy(&head_received.tcp_header.check, "\0", 2);
                crc_recv = crc16((void *)&head_received.tcp_header,bytes-sizeof(head_received.send_to));
                {
                    printf("######################\n");
                    int i = 0;
                    int size = bytes-sizeof(head_received.send_to);
                    char *p = (char *)&head_received.tcp_header;
                    printf("size: %d\n", size);
                    for (; i < size; i++)
                    {
                        printf("%c", *p);
                        p++;
                    }
                    printf("\n######################\n");
                }

                //TODO IF CRC RETRUN NEG, RESEND
                check_crc(crc_send, crc_recv);

                bytes = sendto(sock_client_local, (char *)(&head_received.body), bytes - sizeof(head_received.tcp_header) - sizeof(head_received.send_to), 0,
                               (struct sockaddr *)&to_server_ftp, sizeof(to_server_ftp));

                //send ack back to client
                TrollHeader ack_packet;
                ack_packet.tcp_header.ack = 1;
                ack_packet.tcp_header.seq = head_received.tcp_header.seq;
                ack_packet.tcp_header.ack_seq = ack_packet.tcp_header.seq + bytes;
                //TODO CRC CALCULATE HERE
                sendto(sock_server_remote, (char *)(&ack_packet), sizeof(ack_packet), 0,
                       (struct sockaddr *)&to_client_tcpd, sizeof(to_client_tcpd));
            }
        }
    }
    return 0;
}

int window_full(my_window *window)
{
    if (window->num_empty == 0)
        return 1;
    return 0;
}

void window_init(my_window *window)
{
    bzero(window, sizeof(window));
    window->last_ack = 0;
    window->last_send = 0;
    window->num_empty = WINDOW_SIZE;
}

void address_init()
{
    bzero(&client_local_addr, sizeof(client_local_addr));
    bzero(&server_remote_addr, sizeof(server_remote_addr));
    bzero(&to_client_troll, sizeof(to_client_troll));
    bzero(&to_server_ftp, sizeof(to_server_ftp));
    bzero(&to_server_tcpd, sizeof(to_server_tcpd));
    bzero(&to_server_troll, sizeof(to_server_troll));
    bzero(&to_client_ftp, sizeof(to_client_ftp));

    client_local_addr.sin_family = AF_INET;
    client_local_addr.sin_port = htons(LOCAL_PORT_CLIENT);
    client_local_addr.sin_addr.s_addr = INADDR_ANY;

    client_remote_addr.sin_family = AF_INET;
    client_remote_addr.sin_port = htons(REMOTE_PORT_CLIENT);
    client_remote_addr.sin_addr.s_addr = INADDR_ANY;

    server_remote_addr.sin_family = AF_INET;
    server_remote_addr.sin_port = htons(REMOTE_PORT_SERVER);
    server_remote_addr.sin_addr.s_addr = INADDR_ANY;

    to_client_ftp.sin_family = AF_INET;
    to_client_ftp.sin_port = htons(BLOCK_SIG);
    to_client_ftp.sin_addr.s_addr = inet_addr(CLIENT_IP);

    to_server_ftp.sin_family = AF_INET;
    to_server_ftp.sin_port = htons(SERVER_PORT);
    to_server_ftp.sin_addr.s_addr = inet_addr(SERVER_IP);

    to_server_tcpd.sin_family = AF_INET;
    to_server_tcpd.sin_port = htons(REMOTE_PORT_SERVER);
    to_server_tcpd.sin_addr.s_addr = inet_addr(SERVER_IP);

    to_client_tcpd.sin_family = AF_INET;
    to_client_tcpd.sin_port = htons(REMOTE_PORT_CLIENT);
    to_client_tcpd.sin_addr.s_addr = inet_addr(CLIENT_IP);

    to_client_troll.sin_family = AF_INET;
    to_client_troll.sin_port = htons(TROLL_PORT_CLIENT);
    to_client_troll.sin_addr.s_addr = inet_addr(CLIENT_IP);

    to_server_troll.sin_family = AF_INET;
    to_server_troll.sin_port = htons(TROLL_PORT_SERVER);
    to_server_troll.sin_addr.s_addr = inet_addr(SERVER_IP);
}

void socket_init()
{
    sock_client_local = socket(AF_INET, SOCK_DGRAM, 0);
    sock_client_remote = socket(AF_INET, SOCK_DGRAM, 0);
    sock_server_remote = socket(AF_INET, SOCK_DGRAM, 0);
}

void socket_bind()
{
    if (bind(sock_client_local, (struct sockaddr *)&client_local_addr, sizeof(client_local_addr)) < 0)
    {
        perror("sock_client_local bind");
        exit(EXIT_FAILURE);
    }

    if (bind(sock_server_remote, (struct sockaddr *)&server_remote_addr, sizeof(server_remote_addr)) < 0)
    {
        perror("sock_server_remote bind");
        exit(EXIT_FAILURE);
    }

    if (bind(sock_client_remote, (struct sockaddr *)&client_remote_addr, sizeof(client_remote_addr)) < 0)
    {
        perror("sock_client_remote bind");
        exit(EXIT_FAILURE);
    }
}

void pipe_init()
{
    if (pipe(pipefd) == -1)
    {
        perror("pipefd");
        exit(EXIT_FAILURE);
    }

    if (pipe(pipefd_from_timer) == -1)
    {
        perror("pipefd_from_timer");
        exit(EXIT_FAILURE);
    }
}

void generate_TrollHeader(TrollHeader *head, char *buffer, int size)
{
    head->send_to.sin_family = htons(AF_INET);
    head->send_to.sin_addr.s_addr = inet_addr(SERVER_IP);
    head->send_to.sin_port = htons(REMOTE_PORT_SERVER);
    head->tcp_header.seq = seq;
    head->tcp_header.ack = 0;

    memcpy(head->body, buffer, size);
}
void block_sending_and_save()
{
    //if (debug == 1)
    //{
    printf("The window is full, block the sending.\n");
    //}
    char blocking_word[10] = "block";
    sendto(sock_client_local, blocking_word, sizeof(blocking_word), 0,
           (struct sockaddr *)&to_client_ftp, sizeof(to_client_ftp));

    bzero(tmp_save_buf, sizeof(tmp_save_buf));
    if (debug == 1)
    {
        printf("The window is full, temporary save the data in temporary buffer.\n");
    }
    socklen_t len = sizeof(client_local_addr);
    recvfrom(sock_client_local, tmp_save_buf, sizeof(tmp_save_buf), 0,
             (struct sockaddr *)&client_local_addr, &len);
}

void send_recv_to_ftpc()
{
    printf("send_recv_to_ftpc\n");
    char blocking_word[10] = "received";
    sendto(sock_client_local, blocking_word, sizeof(blocking_word), 0,
           (struct sockaddr *)&to_client_ftp, sizeof(to_client_ftp));
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

int check_crc(int crc_send, int crc_recv)
{
    if (crc_recv == crc_send)
    {
        printf("Pass the crc16 checksum test.\n");

        return 0;
    }
    else
    {
        printf("Fail to pass the crc16 checksum test. Send crc16 is %#4x, Recv crc16 is %#4x\n", crc_send, crc_recv);

        return -1;
    }
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
    gettimeofday(&record_buf[record_index].start_time, NULL);
    record_buf[record_index].seq = seq;
    record_index = (record_index + 1) % 30;
    return;
}

void save_packet(my_window *window, TrollHeader *head)
{
    int next_index = (window->last_send + 1) % WINDOW_SIZE;
    window->ack_bitmap[next_index] = 0;
    memcpy(&window->troll_headers[next_index], head, sizeof(*head));
    window->last_send = next_index;
    window->num_empty--;
    return;
}

void print_window(my_window *window)
{
    int i = 0;
    printf("LASK ACK: %d\tLAST SEND: %d\tNUMBER EMPTY: %d\n",
           window->last_ack, window->last_send, window->num_empty);

    /*
    for (; i < WINDOW_SIZE; i++)
    {
        printf("************************INDEX %d************************\n", i);
        int j = 0;
        for (; j < 10; j++)
        {
            printf("%c", window->troll_headers[i].body[j]);
        }
        printf("\n");
    }
    */
}

double get_rtt(int ack)
{
    struct timeval end_time;
    double rtt = -1.0;

    gettimeofday(&end_time, NULL);

    int i;
    for (i = 0; i < 30; i++)
    {
        if (ack == record_buf[i].seq)
        {
            rtt = (double)(end_time.tv_sec - record_buf[i].start_time.tv_sec) + (end_time.tv_usec - record_buf[i].start_time.tv_usec) / 1000000.0;
            printf("end time: %ld, start time %ld\n", end_time.tv_sec, record_buf[i].start_time.tv_sec);
            printf("end time: %ld, start time %ld\n", end_time.tv_usec, record_buf[i].start_time.tv_usec);
            printf("ack: %d, seq: %d\n", ack, record_buf[i].seq);
            printf("RTT: % f\n", rtt);
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

/*ssize_t buffer_recvfrom(int sockfd, send_buffer *buf, int flags,
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

    byte = recvfrom(sockfd, tmp_buf, cap, flags, src_addr, addrlen);

    printf("bytebytebytebyte:%d\n", byte);

    if (debug == 1)
    {
        if (byte == 4)
        {
            int *len = (int *)&tmp_buf[0];
            printf("byte recvd: %d , content: int: %d, char %s \n", byte, *len, tmp_buf);
        }send_recv_to_client_ftp
        else
        {
            printf("byte recvd: %d , content: %s\n", byte, tmp_buf);
        }
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
            printf("mode 1 : %s\n", tmp_buf);
            printf("ack index: %d\n", buf->ack_index);
            printf("send index: %d\n", buf->send_index);
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
                printf("%s\n", tmp_buf);
                printf("ack index: %d\n", buf->ack_index);
                printf("send index: %d\n", buf->send_index);
            }

            return byte;
        }
    }
}

void update_ack(send_buffer *buf, int ack_byte)
{
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

    return;
}

void read_from_buffer(char *buf, send_buffer *send_buf, int start, int len)
{
    if ((start + len) < sizeof(send_buf->buff))
    {
        printf("read_from_buffer:%d\n", len);

        memcpy(buf, &send_buf->buff[start], len);
        return;
    }

    int right_size = sizeof(send_buf->buff) - start;
    int left_size = len - right_size;
    memcpy(buf, &send_buf->buff[start], right_size);
    memcpy(buf + right_size, &send_buf->buff[0], left_size);
    return;
}*/