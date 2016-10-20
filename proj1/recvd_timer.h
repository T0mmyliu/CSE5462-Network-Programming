#ifndef _TIMER_H_
#define _TIMER_H_
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <string.h>
#include <sys/wait.h>

//function expose to the ftd.c
void send_to_timer(int RTO, int seq);
void delete_from_timer(int seq);
int timer_process_start();

//timeout function to ask the tcpd to resend
//TODO : next time
/*--------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------*/

#define timercmp(a, b, CMP) \
    (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec CMP(b)->tv_usec) : ((a)->tv_sec CMP(b)->tv_sec))

#define timeradd(a, b, result)                           \
    do                                                   \
    {                                                    \
        (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;    \
        (result)->tv_usec = (a)->tv_usec + (b)->tv_usec; \
        if ((result)->tv_usec >= 1000000)                \
        {                                                \
            ++(result)->tv_sec;                          \
            (result)->tv_usec -= 1000000;                \
        }                                                \
    } while (0)

#define COMMAND_SIZE 40

typedef struct time_node
{
    int id;
    struct timeval expire_time;
    struct time_node *next_node;
} time_node;

typedef struct cancel_args
{
    char command;
    int id;
} cancel_args;

typedef struct start_args
{
    char command;
    int id;
    double expire_time;
} start_args;

typedef struct stop_args
{
    char command;
} stop_args;

//Follows are help function
struct timeval get_current_time();

//Follows are timer function
time_node *insert(time_node *head, double expire_time, int id);
time_node *delele(time_node *head, int id);
time_node *update(time_node *head);
void delete_all(time_node *head);
void print_time_node(time_node *node);

//Follows are driver function
void start_timer(int fd, double expire_time, int id);
void cancel_timer(int fd, int id);
void stop_timer(int fd);

#endif
