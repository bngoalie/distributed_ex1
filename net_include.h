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

#define PORT	        10110
#define WINDOW_SIZE     256
#define MAX_PACKET_SIZE 1400
#define PAYLOAD_SIZE    MAX_PACKET_SIZE-2*sizeof(char)

/* Packet: Struct for generic packet */
typedef struct dummy_packet {
    /* Types for ncp packets: request transfer(0), regular data(1), 
     * final data(2).
     * Types for rcv packets: ready to transfer(0), not ready for transfer (1), 
ack & nacks(2). */
    char type;
    /* ncp payloads: for transfer request, just the name of the file. 
     *               for all other types: bytes for the file
     * rcv payloads: for ready to transfer packets, there is no need for a payload.
     *               for acks & nacks packets, the first byte will be the cumulative ack, and the remaining bytes will be nacks
     */
    char payload[MAX_PACKET_SIZE- sizeof(char)];    
} Packet;

/* DataPacket: Struct for send packet */
typedef struct dummy_packet2 {
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

/* TODO: Make a struct for end packet */

