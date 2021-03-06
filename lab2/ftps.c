#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/ip.h>

int process_request(int conn_sock);

//A multiprocess server
int main(int argc,char* argv[])
{
    int listen_sock,conn_sock;
    pid_t child_pid;
    struct sockaddr_in serv_addr,cli_addr;
    socklen_t cli_len;
    int port;

    port=atoi(argv[1]);

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if(bind(listen_sock, (struct sockaddr*)& serv_addr, sizeof(struct sockaddr_in))<0){
        perror("error binding stream socket");
        return 0;
    }

    listen(listen_sock, 5);

    //run server
    for( ; ;){
        printf("serveing...\n");
        cli_len = sizeof(cli_addr);
        conn_sock = accept(listen_sock, (struct sockaddr*) &cli_addr,&cli_len);

        //process in child process
        if((child_pid=fork())==0){
            close(listen_sock);
            process_request(conn_sock);
            exit(0);
        }
        close(conn_sock);
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
    rec_len=0;
    while(rec_len!=4){
        int tmp=0;
        if((tmp=read(conn_sock,(void*)(&file_len),4))<0){
            printf("Error reading on stream socket\n");
            exit(0);
        }
        printf("read %d byte\n",tmp);
        rec_len+=tmp;
        printf("rec_len %d byte\n",rec_len);
    }
    file_len=ntohl(file_len);

    printf("Server receives file length: %d\n",file_len);

    //read the following 25 byte: file_name.
    rec_len=0;
    while(rec_len!=25){
        int tmp=0;
        if((tmp=read(conn_sock,file_name,sizeof(file_name)))<0){
            printf("Error writing on stream socket\n");
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
        if((rec_len=read(conn_sock,buf,sizeof(buf)))<0){
            perror("error happen in receive.");
        }
        left_len-=rec_len;
        
        if((fwrite(buf,sizeof(char),rec_len,fp))!=rec_len){
            perror("error happen when writing the data.");
        }
    }

    close(conn_sock);
    
    return 0;
}
