CC=gcc

CFLAGS = -ansi -c -Wall -pedantic

all: ncp rcv t_ncp t_rcv

ncp: ncp.o sendto_dbg.o
	    $(CC) -o ncp ncp.o sendto_dbg.o

rcv: rcv.o
	    $(CC) -o rcv rcv.o

t_ncp: t_ncp.o
	    $(CC) -o t_ncp t_ncp.o

t_rcv: t_rcv.o
	    $(CC) -o t_rcv t_rcv.o

clean:
	rm *.o
	rm ncp
	rm rcv
	rm t_ncp
	rm t_rcv

%.o:    %.c
	$(CC) $(CFLAGS) $*.c


