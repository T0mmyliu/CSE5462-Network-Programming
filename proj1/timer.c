#include "timer.h"

extern int pipefd[2];
extern int pipefd_from_timer[2];
static int debug = 0;
int timer_process_start()
{
    printf("start timer process.\n");
    pid_t cpid;
    fd_set rfds;
    struct timeval tv;
    int retval;
    char buf[COMMAND_SIZE];
    char command;
    time_node *head;
    start_args *sargs;
    cancel_args *cargs;

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
            tv.tv_sec = 0;
            tv.tv_usec = 1000;

            retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
            head = update(head);

            if (retval == -1)
            {
                perror("select()");
            }
            else if (retval == 0)
            {
                continue;
            }
            else if (retval)
            {
                read(pipefd[0], buf, COMMAND_SIZE); //read one command from the driver.
                command = *buf;
                switch (command)
                {
                case 's': //start a new timer
                    //printf("\nCommand = \'s\'\n");
                    sargs = (start_args *)buf;
                    head = insert(head, sargs->expire_time, sargs->id);
                    printf("******after insert*****\n");
                    print_time_node(head);
                    break;
                case 'c': //cancel a timer
                    //printf("\nCommand = \'c\'\n");
                    cargs = (cancel_args *)buf;
                    head = delele(head, cargs->id);
                    printf("******after delect*****\n");
                    print_time_node(head);
                    break;
                case 'q': //stop the timer process
                    printf("\nCommand=\'q\', exit the process.\n");
                    delete_all(head);
                    exit(0);
                }
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
        //To disable the driver.
        exit(EXIT_SUCCESS);

        //driver: to test the timer
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
    return 0;
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
    //printf("The current local time is %d-%d-%d %d:%d:%d %f\n", 1900 + time->tm_year, time->tm_mon + 1, time->tm_mday,
    //       time->tm_hour, time->tm_min, time->tm_sec, (double)node->expire_time.tv_usec / 1000000.0);
    print_time_node(node->next_node);
    return;
}

struct timeval get_current_time()
{
    struct timeval curtime;
    gettimeofday(&curtime.tv_sec, &curtime.tv_usec);
    return curtime;
}

void send_to_timer(int RTO, int seq)
{
    start_timer(pipefd[1], RTO, seq);

    return;
}

void start_timer(int fd, double expire_time, int id)
{
    start_args args;
    args.id = id;
    args.expire_time = expire_time;
    args.command = 's';
    //printf("Send : command=%c  id=%d  time is%lf\n", args.command, args.id, args.expire_time);
    write(fd, (void *)&args, COMMAND_SIZE);
    return;
}

void delete_from_timer(int seq)
{
    if (debug == 1)
    {
        printf("ack: %d \n", seq);
    }
    cancel_timer(pipefd[1], seq);
    return;
}

void cancel_timer(int fd, int id)
{
    cancel_args args;
    args.id = id;
    args.command = 'c';
    //printf("Send : command=%c  id=%d\n", args.command, args.id);
    write(fd, (void *)&args, COMMAND_SIZE);
    return;
}

void stop_timer(int fd)
{
    stop_args args;
    args.command = 'q';
    //printf("Send : command=%c\n", args.command);
    write(fd, (void *)&args, COMMAND_SIZE);
    return;
}

time_node *insert(time_node *head, double expire_time, int id)
{
    struct timeval delta_time, curtime;

    time_node *node_to_add = (time_node *)malloc(sizeof(time_node));
    node_to_add->id = id;
    node_to_add->next_node = NULL;

    delta_time.tv_sec = (time_t)expire_time;
    delta_time.tv_usec = (expire_time - (time_t)expire_time) * 1000;
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
    if (debug == 1)
    {
        printf("Didnot find the id:  %d, delect Fail.\n", id);
    }
    return prenode.next_node;
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
    return;
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
        //TODO: SHOULD DELETE THE NODE AND SEND BACK TO TCPD TO RECEND.
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
    return 0;
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
    if (debug == 1)
    {

        printf("\n");
    }
    print_time_node(head);
    if (debug == 1)
    {

        printf("\n");
    }
    print_time_node(&node1);
    return 0;
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
    return 0;
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

    if (debug == 1)
    {
        printf("*****************\n");
    }
    t = delele(t, 2); //test node not in the first and last
    print_time_node(t);
    if (debug == 1)
    {
        printf("*****************\n");
    }
    t = delele(t, 0); //test node in the first
    print_time_node(t);

    if (debug == 1)
    {
        printf("*****************\n");
    }
    t = delele(t, 6); //test node in the last
    print_time_node(t);
    if (debug == 1)
    {
        printf("*****************\n");
    }
    t = delele(t, 0); //test node don't exist
    print_time_node(t);
    return 0;
}
