#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/ip.h>

#include "address.h"
#include "wrap_func.h"

int process_request(int conn_sock);

int main(int argc,char* argv[])
{
    int listen_sock,conn_sock;
    pid_t child_pid;
    struct sockaddr_in serv_addr,cli_addr;
    socklen_t cli_len;
    int port;

    if(argc!=2){
        printf("Usage: \"./ftps <PORT>\".\n");
        exit(1);
    }
    
    port=atoi(argv[1]);

    listen_sock = SOCKET(AF_INET, SOCK_STREAM, 0);

    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if(BIND(listen_sock, (struct sockaddr*)& serv_addr, sizeof(struct sockaddr_in))<0){
        perror("error binding stream socket");
        return 0;
    }

    LISTEN(listen_sock, 5);

    //run server
    for( ; ;){
        printf("serveing...\n");
        cli_len = sizeof(cli_addr);
        process_request(listen_sock);
    }
}

int process_request(int conn_sock)
{
    int file_len;
    int left_len;
    int rec_len;
    char file_name[25];
    char rec_name[40];
    char buf[1000];
    FILE* fp;

    bzero(&file_len,sizeof(file_len));
    bzero(file_name,sizeof(file_name));
    bzero(rec_name,sizeof(rec_name));

    //read the first four byte: file_len in network order.
    

    rec_len = RECV(conn_sock,buf,4,MSG_WAITALL);
    printf("bytes in: %d %s\n",rec_len,buf);	
    if(rec_len < 0){
        perror("Error reading from socket!1st");
	exit(1);
    }
    file_len=0;
    memcpy(&file_len,buf,4);	
    file_len = ntohl(file_len);
    printf("The size of the file is: %d bytes.\n",file_len);


    //read the following 25 byte: file_name.
    rec_len=0;
    while(rec_len!=25){
        int tmp=0;
        if((tmp=RECV(conn_sock,file_name,sizeof(file_name),MSG_WAITALL))<0){
            printf("Error writing on stream socket, 2nd\n");
        }
        printf("read %d byte\n",tmp);
        rec_len+=tmp;
        printf("rec_len %d byte\n",rec_len);
    }
    printf("Server recevies file name: %s\n",file_name);
    
    //create new recve_file
    strcat(rec_name,"recvd_");
    strcat(rec_name,file_name);
    if((fp=fopen(rec_name,"w"))==NULL){
        perror("error create receive file.");
    }
    
    left_len=file_len;
    
    //write data to recvd_file
    fp=fopen(rec_name,"a");
    while(left_len!=0){
        bzero(buf,sizeof(buf));
        if((rec_len=RECV(conn_sock,buf,sizeof(buf),MSG_WAITALL))<0){
            perror("error happen in receive.");
        }
        left_len-=rec_len;
        fwrite(buf,sizeof(char),rec_len,fp);
        fflush(fp);
    }
    
    return 0;
}
