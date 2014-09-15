CC=gcc

CFLAGS = -ansi -c -Wall -pedantic

all: ncp rcv

ncp: ncp.o sendto_dbg.o
	    $(CC) -o ncp ncp.o sendto_dbg.o

rcv: rcv.o
	    $(CC) -o rcv rcv.o

clean:
	rm *.o
	rm ncp

%.o:    %.c
	$(CC) $(CFLAGS) $*.c


