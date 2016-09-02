EXE = count
CC=gcc
OBJS= count.o
CFLAGS = -c -O3 -g -Wall
LFLAGS = -O3 -g -Wall
all: ${OBJS}
	${CC} ${LFLAGS} ${OBJS} -o ${EXE}
count.o:count.c
	${CC} ${CFLAGS} count.c
clean:
	rm -f $(EXE) *.o *out* *~
