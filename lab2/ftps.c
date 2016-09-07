#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/ip.h>

int process_request(int conn_sock);

int main()
{
		int listen_sock,conn_sock;
		pid_t child_pid;
		struct sockaddr_in serv_addr,cli_addr;
		int cli_len;
		int port;

		listen_sock = socket(AF_INET, SOCK_STREAM, 0);
		
		bzero(&serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(port);

		if(bind(listen_sock, (struct sockaddr*)& serv_addr, sizeof(struct sockaddr_in))<0){
				printf("Error binding stream socket.\n");
				return;
		}
		
		listen(listen_sock, 5);

		for( ; ;){
				cli_len = sizeof(cli_addr);
				conn_sock = accept(listen_sock, (struct sockaddr*) &cli_addr, &cli_len);

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
		size_t n;
		char buf[1000],buf2[1000];
		
		bzero(buf,1000);
		bzero(buf2,1000);

		for(;;){
				if ((read(conn_sock,buf,1000))<0){
						printf("Error reading on stream socket\n");
						exit(0);
				}
				
				printf("Server receives: %s\n",buf);

				if((write(conn_sock,buf2,1000))<0){
						printf("Error writing on stream socket\n");
				}

				printf("Server receives: %s\n",buf2);

				close(conn_sock);
		}
}
