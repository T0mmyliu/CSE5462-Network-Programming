/* Example: client.c sending and receiving datagrams using UDP */
#include <netdb.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define MAX_MES_LEN 1024

/* client program called with host name and port number of server */
main(int argc, char *argv[])
{
    int sock, buflen;
    char cli_buf[MAX_MES_LEN] = "Hello in UDP from client";
    struct sockaddr_in name;
    struct hostent *hp, *gethostbyname();

    if(argc < 3) {
	printf("usage: cli remote_host_name remote_port_number\n");
	exit(1);
    }

    /* create socket for connecting to server */
    sock = socket(AF_INET, SOCK_DGRAM,0);
    if(sock < 0) {
	perror("opening datagram socket");
	exit(2);
    }

    /* construct name for connecting to server */
    name.sin_family = AF_INET;
    name.sin_port = htons(atoi(argv[2]));

    /* convert hostname to IP address and enter into name */
    hp = gethostbyname(argv[1]);
    if(hp == 0) {
	fprintf(stderr, "%s:unknown host\n", argv[1]);
	exit(3);
    }
    bcopy((char *)hp->h_addr, (char *)&name.sin_addr, hp->h_length);

    /* send message to server */
    buflen = 1024; 
    printf("Client sends:    %s\n",cli_buf);
    if(sendto(sock, cli_buf, buflen, 0, (struct sockaddr *)&name, sizeof(name)) <0) {
	perror("sending datagram message");
	exit(4);
    }

    /* waiting for server to respond using the same socket */
    bzero(cli_buf, MAX_MES_LEN);
    if(recv(sock, cli_buf, buflen, 0) < 0) {
	perror("sending datagram message");
	exit(5);
    }
    printf("Client receives: %s\n",cli_buf);

    /* close connection */
    close(sock);
    exit(0);
}
