CC=gcc
CFLAGS = -c -O3 -g -Wall
LFLAGS = -O3 -g -Wall
all: ftpc.o ftps.o
	${CC} ${LFLAGS} ftpc.o -o ftpc
	${CC} ${LFLAGS} ftps.o -o ftps
ftpc.o:ftpc.c
	${CC} ${CFLAGS} ftpc.c
ftps.o:ftps.c
	${CC} ${CFLAGS} ftps.c
clean:
	rm -f ftpc ftps *.o *~ recvd*
