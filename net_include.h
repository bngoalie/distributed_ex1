#include <stdio.h>

#include <stdlib.h>

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h> 
#include <netdb.h>

#include <errno.h>

#define PORT	     10110

#define MAX_MESS_LEN 8192

#define WINDOW_SIZE 256

#define MAX_PACKET_SIZE 1400

/* Struct for generic packet */
typedef struct dummy_packet {
    /* Types for ncp packets: request transfer, regular data, final data
       Types for rcv packets: ready to transfer, ack & nacks */
    char type;
    /* ncp payloads: for transfer request, just the name of the file. 
     *               for all other types: bytes for the file
     * rcv payloads: for ready to transfer packets, there is no need for a payload.
     *               for acks & nacks packets, the first byte will be the cumulative ack, and the remaining bytes will be nacks
     */
    char payload[MAX_PACKET_SIZE- sizeof(char)];    
} Packet;

/* Struct for send packet */
typedef struct dummy_packet_2 {
    /* Types for ncp packets: request transfer, regular data, final data
       Types for rcv packets: ready to transfer, ack & nacks */
    char type;
    /* ID for sending packet is a number from 0-255 inclusive */
    char id;
    /* ncp payloads: for transfer request, just the name of the file. 
     *               for all other types: bytes for the file
     * rcv payloads: for ready to transfer packets, there is no need for a payload.
     *               for acks & nacks packets, the first byte will be the cumulative ack, and the remaining bytes will be nacks
     */
    char payload[MAX_PACKET_SIZE- 2*sizeof(char)];    
} DataPacket;



