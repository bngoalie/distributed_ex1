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
#define WINDOW_SIZE     1024
#define MAX_PACKET_SIZE 1400
#define PACKET_ID int
#define PACKET_TYPE int
#define PAYLOAD_SIZE    MAX_PACKET_SIZE-sizeof(PACKET_TYPE)-sizeof(PACKET_ID)
#define NACK_WAIT_COUNT 20

/* Packet: Struct for generic packet */
typedef struct {
    /* Types for ncp packets: request transfer(0), regular data(1), 
     * final data(2).
     * Types for rcv packets: ready to transfer(0), not ready for transfer (1), 
ack & nacks(2). */
    PACKET_TYPE type;
    /* ncp payloads: for transfer request, just the name of the file. 
     *               for all other types: bytes for the file
     * rcv payloads: for ready to transfer packets, there is no need for a payload.
     *               for acks & nacks packets, the first byte will be the cumulative ack, and the remaining bytes will be nacks
     */
    char payload[MAX_PACKET_SIZE- sizeof(PACKET_TYPE)];    
} Packet;

/* DataPacket: Struct for send packet */
typedef struct {
    /* Types for ncp packets: request transfer, regular data, final data
       Types for rcv packets: ready to transfer, ack & nacks */
    PACKET_TYPE type;
    /* ID for sending packet is a number from 0-255 inclusive */
    PACKET_ID id;
    /* ncp payloads: for transfer request, just the name of the file. 
     *               for all other types: bytes for the file
     * rcv payloads: for ready to transfer packets, there is no need for a payload.
     *               for acks & nacks packets, the first byte will be the cumulative ack, and the remaining bytes will be nacks
     */
    char payload[MAX_PACKET_SIZE- sizeof(PACKET_ID) - sizeof(PACKET_TYPE)];    
} DataPacket;

typedef struct {
    PACKET_TYPE type;
    PACKET_ID ack_id;
    PACKET_ID nacks[MAX_PACKET_SIZE- sizeof(PACKET_ID) - sizeof(PACKET_TYPE)];
} AckNackPacket;

typedef struct {
    PACKET_TYPE type; /* 0: filename, 1: data, 2: end */
    char payload[MAX_PACKET_SIZE - sizeof(PACKET_TYPE)];
} TcpPacket;
