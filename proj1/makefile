C = gcc
CFLAGS=-c -Wall
# setup for system
LIBS = 

all: ftpc ftps tcpd wrap_func.o timer.o
ftpc: ftpc.c wrap_func.o
	$(CC) ftpc.c wrap_func.o -o ftpc

ftps: ftps.c wrap_func.o
	$(CC) ftps.c wrap_func.o -o ftps

tcpd: tcpd.c timer.o
	$(CC) tcpd.c timer.o -o tcpd

timer.o: timer.c 
	$(CC) -c timer.c
 				
wrap_func.o: wrap_func.c
	$(CC) $(CFLAGS) wrap_func.c
	
clean:
	rm *.o ftpc ftps tcpd recvd* 

