#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <string.h>

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

int main()
{
    int pipefd[2];
    pid_t cpid;
    int result;
    fd_set rfds;
    struct timeval tv;
    int retval;
    char buf[COMMAND_SIZE];
    char command;
    struct timeval current_time, past_time;
    time_node *head;

    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    cpid = fork();
    if (cpid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (cpid == 0)
    {
        //TODO: There is a bug, if add the following line will be loop forever.
        //close(pipefd[1]); /* Close unused write end */

        head = NULL;

        while (1)
        {
            FD_ZERO(&rfds);
            FD_SET(pipefd[0], &rfds);
            /* Wait up to five seconds. */
            tv.tv_sec = 5;
            tv.tv_usec = 0;

            retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
            head = update(head);
            current_time = get_current_time();

            if (retval == -1)
            {
                perror("select()");
            }
            else if (retval)
            {
                read(pipefd[0], buf, COMMAND_SIZE); //read one command from the driver.
                command = *buf;
                switch (command)
                {
                case 's': //start a new timer
                    printf("\nCommand = \'s\'\n");
                    start_args *sargs = (start_args *)buf;
                    head = insert(head, sargs->expire_time, sargs->id);
                    printf("******after insert*****\n");
                    print_time_node(head);
                    break;
                case 'c': //cancel a timer
                    printf("\nCommand = \'c\'\n");
                    cancel_args *cargs = (cancel_args *)buf;
                    head = delele(head, cargs->id);
                    printf("******after delect*****\n");
                    print_time_node(head);
                    break;
                case 'q': //stop the timer process
                    printf("\nCommand=\'q\', exit the process.\n");
                    delete_all(head);
                    exit(0);
                }
                //printf("Recv : command=%c  id=%d\n", args.command, args.id);
            }
            else
            {
                printf("Select time expire, exit the process.\n");
                break;
            }
        }
        delete_all(head);
    }
    else
    {
        //TODO: There is a bug, if add the following line will be loop forever.
        //close(pipefd[0]); /* Close unused read end */
        start_timer(pipefd[1], 5.0, 1);
        start_timer(pipefd[1], 6.1, 2);
        start_timer(pipefd[1], 7.2, 3);
        sleep(2);
        start_timer(pipefd[1], 10.3, 4);
        sleep(2);
        cancel_timer(pipefd[1], 1);
        sleep(2);
        stop_timer(pipefd[1]);
        close(pipefd[1]); /* Reader will see EOF */
        wait(NULL);       /* Wait for child */
        exit(EXIT_SUCCESS);
    }
}

void print_time_node(time_node *node)
{
    if (node == NULL)
    {
        return;
    }
    printf("The id is: %d\n", node->id);
    struct tm *time;
    time = localtime(&node->expire_time.tv_sec);
    printf("The current local time is %d-%d-%d %d:%d:%d %f\n", 1900 + time->tm_year, time->tm_mon + 1, time->tm_mday,
           time->tm_hour, time->tm_min, time->tm_sec, (double)node->expire_time.tv_usec/1000.0);
    print_time_node(node->next_node);
}

struct timeval get_current_time()
{
    struct timeval curtime;
    struct tm *time;
    gettimeofday(&curtime.tv_sec, &curtime.tv_usec);
    time = localtime(&curtime.tv_sec);
    //printf("The current local time is %d-%d-%d %d:%d:%d.%ld\n", 1900 + time->tm_year, time->tm_mon + 1, time->tm_mday,
    //      time->tm_hour, time->tm_min, time->tm_sec, curtime.tv_usec);
    return curtime;
}

void start_timer(int fd, double expire_time, int id)
{
    start_args args;
    args.id = id;
    args.expire_time = expire_time;
    args.command = 's';
    //printf("Send : command=%c  id=%d  time is%lf\n", args.command, args.id, args.expire_time);
    write(fd, (void *)&args, COMMAND_SIZE);
}

void cancel_timer(int fd, int id)
{
    cancel_args args;
    args.id = id;
    args.command = 'c';
    //printf("Send : command=%c  id=%d\n", args.command, args.id);
    write(fd, (void *)&args, COMMAND_SIZE);
}

void stop_timer(int fd)
{
    stop_args args;
    args.command = 'q';
    //printf("Send : command=%c\n", args.command);
    write(fd, (void *)&args, COMMAND_SIZE);
}

time_node *insert(time_node *head, double expire_time, int id)
{
    struct timeval delta_time, curtime, *res;

    time_node *node_to_add = (time_node *)malloc(sizeof(time_node));
    node_to_add->id = id;
    node_to_add->next_node = NULL;

    delta_time.tv_sec = (time_t)expire_time;
    delta_time.tv_usec = (expire_time - (time_t)expire_time)*1000;
    curtime = get_current_time();

    timeradd(&delta_time, &curtime, &(node_to_add->expire_time));

    //create a tmp prehead node for simplicity
    time_node prenode;
    prenode.next_node = head;
    time_node *p = &prenode;

    while (p->next_node && timercmp(&node_to_add->expire_time, &p->next_node->expire_time, >))
    {
        p = p->next_node;
    }

    time_node *tmp = p->next_node;
    p->next_node = node_to_add;
    node_to_add->next_node = tmp;

    return prenode.next_node;
}

time_node *delele(time_node *head, int id)
{

    //create a tmp prehead node for simplicity
    time_node prenode;
    prenode.next_node = head;
    time_node *p = &prenode;

    while (p->next_node)
    {
        if (p->next_node->id == id)
        {
            time_node *tmp = p->next_node;
            p->next_node = p->next_node->next_node;
            free(tmp);
            return prenode.next_node;
        }

        p = p->next_node;
    }

    if (p->next_node == NULL)
    {
        printf("Didnot find the id , delect Fail.\n");
        return prenode.next_node;
    }
}

void delete_all(time_node *head)
{
    if (head == NULL)
    {
        return;
    }

    time_node *tmp = head;
    head = head->next_node;
    free(tmp);
    delete_all(head);
}

time_node *update(time_node *head)
{
    struct timeval curtime;
    curtime = get_current_time();
    while (head)
    {
        if (timercmp(&head->expire_time, &curtime, >))
        {
            break;
        }
        head = head->next_node;
    }
    return head;
}

int test_print_time_node()
{
    time_node node1, node2;
    node1.id = 1;
    gettimeofday(&node1.expire_time.tv_sec, &node1.expire_time.tv_usec);
    node1.next_node = &node2;
    sleep(1);
    node2.id = 2;
    gettimeofday(&node2.expire_time.tv_sec, &node2.expire_time.tv_usec);
    node2.next_node = NULL;
    print_time_node(&node1);
}

int test_update()
{
    time_node node1, node2, node3, node4;
    time_node *head;

    node1.id = 1;
    node2.id = 2;
    node3.id = 3;
    node4.id = 4;
    node1.next_node = &node2;
    node2.next_node = &node3;
    node3.next_node = &node4;
    node4.next_node = NULL;

    gettimeofday(&node1.expire_time.tv_sec, &node1.expire_time.tv_usec);
    sleep(1);
    gettimeofday(&node2.expire_time.tv_sec, &node2.expire_time.tv_usec);
    node2.expire_time.tv_sec = node2.expire_time.tv_sec + 1;
    node2.expire_time.tv_usec = node2.expire_time.tv_usec + 100;
    sleep(1);
    gettimeofday(&node3.expire_time.tv_sec, &node3.expire_time.tv_usec);
    node3.expire_time.tv_sec = node3.expire_time.tv_sec + 1;
    node3.expire_time.tv_usec = node3.expire_time.tv_usec + 200;
    gettimeofday(&node4.expire_time.tv_sec, &node4.expire_time.tv_usec);
    node4.expire_time.tv_usec = node4.expire_time.tv_usec + 500;
    node4.expire_time.tv_sec = node4.expire_time.tv_sec + 2;

    head = update(&node1);
    printf("\n");
    print_time_node(head);
    printf("\n");
    print_time_node(&node1);
}

int test_insert()
{
    time_node *t = NULL;
    int i;
    for (i = 0; i < 5; i++)
    {
        t = insert(t, (double)i, i);
    }
    print_time_node(t);
}

int test_delect()
{
    time_node *t = NULL;
    int i;
    for (i = 0; i < 7; i++)
    {
        t = insert(t, (double)i, i);
    }
    print_time_node(t);

    printf("*****************\n");
    t = delele(t, 2); //test node not in the first and last
    print_time_node(t);

    printf("*****************\n");
    t = delele(t, 0); //test node in the first
    print_time_node(t);

    printf("*****************\n");
    t = delele(t, 6); //test node in the last
    print_time_node(t);

    printf("*****************\n");
    t = delele(t, 0); //test node don't exist
    print_time_node(t);
}
