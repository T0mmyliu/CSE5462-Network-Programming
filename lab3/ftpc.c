#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

#include "wrap_func.h"
#include "address.h"

struct file_info{
    FILE* fp;
    int len;
    char file_name[100];
};

//init the file_info: file_name file_len ...
int ini_file_info(struct file_info* file)
{
    if((file->fp=fopen(file->file_name,"r"))==NULL){
        perror("Fail to open the file");
        return -1;
    }

    if(strlen(file->file_name)>25){
        printf("Error: the file name is too long.\n ");
        return -1;
    }

    fseek(file->fp,0L,SEEK_END);
    if((file->len=ftell(file->fp))<0){
        printf("Error: file len is smaller than zero.\n");
        return -1;
    }

    fseek(file->fp,0L,SEEK_SET);
    return file->len;
}


/* client program called with host name where server is run */
int main(int argc, char *argv[])
{
    char server_ip[20];
    char server_port[10];
    int sock;                     /* initial socket descriptor */
    struct sockaddr_in sin_addr; /* structure for socket name * setup */
    struct hostent *hp;
    struct file_info finfo;
    uint32_t net_order_len;

    if(argc != 4) {
        printf("Error: usage is ftpc <remote-IP> <remote-port> <local-file-to-transfer>\n");
        exit(1);
    }

    bzero((void*)(&server_ip),sizeof(server_ip));
    bzero((void*)(&server_port),sizeof(server_port));
    bzero((void*)(&finfo),sizeof(finfo));
    strcpy(server_ip,argv[1]);
    strcpy(server_port,argv[2]);
    strcpy(finfo.file_name,argv[3]);

    if(ini_file_info(&finfo)==-1){
        return -1;
    }

    /* initialize socket connection in unix domain */
    if((sock = SOCKET(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("error openting datagram socket");
        exit(1);
    }

    hp = gethostbyname(server_ip);
    if(hp == 0) {
        fprintf(stderr, "%s: unknown host\n", argv[1]);
        exit(2);
    }

    /* construct name of socket to send to */
    bcopy((void *)hp->h_addr, (void *)&sin_addr.sin_addr, hp->h_length);
    sin_addr.sin_family = AF_INET;
    sin_addr.sin_port = (short)(htons(atoi(server_port))); /* fixed by adding htons() */

    /* establish connection with server */
    if(CONNECT(sock, (struct sockaddr *)&sin_addr, sizeof(struct sockaddr_in)) < 0) {
        close(sock);
        perror("error connecting stream socket");
        exit(1);
    }

    /* write buf to sock */
    net_order_len=htonl(finfo.len);
    if(SEND(sock,(char*)(&net_order_len), 4, 0) < 0) {
        perror("error writing on stream socket");
        exit(1);
    }
    printf("Client sends the file len: %d\n", finfo.len);

    if(SEND(sock, finfo.file_name, 25, 0) < 0) {
        perror("error writing on stream socket");
        exit(1);
    }
    printf("Client sends the file name: %s\n", finfo.file_name);

    int left_len=finfo.len;
    while(left_len!=0){
        int read_len=0;
        char read_buf[1000]={0};
        if((read_len=fread(read_buf,sizeof(char),sizeof(read_buf),finfo.fp))<0){
            perror("error reading the file");
        }
        left_len-=read_len;
        
        if(SEND(sock, read_buf, read_len,0) < 0) {
            perror("error writing on stream socket");
            exit(1);
        }
    }
    printf("Finish sending the file.\n");
    
    return 0;
} 
