C = gcc
OBJCLI = ftpc.c 
OBJSRV = ftps.c
CFLAGS=-c -Wall
# setup for system
LIBS = 

all: ftpc ftps tcpd wrap_func.o
ftpc: ftpc.c wrap_func.o
	$(CC) ftpc.c wrap_func.o -o ftpc

ftps: ftps.c wrap_func.o
	$(CC) ftps.c wrap_func.o -o ftps

tcpd: tcpd.c
	$(CC) tcpd.c -o tcpd
 				
wrap_func.o: wrap_func.c
	$(CC) $(CFLAGS) wrap_func.c
	
clean:
	rm *o ftpc ftps tcpd
