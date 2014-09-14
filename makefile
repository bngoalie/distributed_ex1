CC=gcc

CFLAGS = -ansi -c -Wall -pedantic

all: ncp

ncp: ncp.o
	    $(CC) -o ncp ncp.o sendto_dbg.o

clean:
	rm *.o
	rm ncp

%.o:    %.c
	$(CC) $(CFLAGS) $*.c


