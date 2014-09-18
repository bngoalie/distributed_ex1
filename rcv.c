/* Includes */
#include "net_include.h"
#include "sendto_dbg.h"

/* Constants */
#define NAME_LENGTH 80

/* Function prototypes */
int gethostname(char*,size_t);
void handleTransferPacket(Packet *packet, int ip, int ss, struct sockaddr_in *send_addr);
char isInQueue(int ip);
void addToQueue(Packet *packet, int ip);
void initiateTransfer(char *file_name, int ip, int ss, struct sockaddr_in *send_addr);
int transferNacksToPayload(char *nack_payload_ptr, char rcvd_id, char sequence_id);
int handleDataPacket(DataPacket *packet, int packet_size, int ip,
                      int ss, struct sockaddr_in *send_addr, 
                      int sequence_number);
int transferNacksToPayload(char *nack_payload_ptr, char rcvd_id, char sequence_id); 
    
/* Structs */
typedef struct dummy_node 
{
    int         sender_ip;
    char        *name;
    struct dummy_node *next;
} Node;

Node *transfer_queue_head = NULL;
Node *transfer_queue_tail = NULL;

typedef struct dummy_nack_node
{
    char id;
    struct dummy_nack_node *next;
} NackNode;
    
NackNode *nack_queue_head = NULL;
NackNode *nack_queue_tail = NULL;
DataPacket            *window[WINDOW_SIZE]; 
char                  is_transferring = 0;     
FILE *fw = NULL;
int                   size_of_last_payload;

int main(int argc, char **argv)
{
    struct sockaddr_in    name;
    struct sockaddr_in    send_addr;
    struct sockaddr_in    from_addr;
    socklen_t             from_len;
    int                   from_ip;
    int                   ss,sr;
    fd_set                mask;
    fd_set                dummy_mask,temp_mask;
    int                   bytes;
    int                   num;
    char                  mess_buf[MAX_PACKET_SIZE];
    char                  input_buf[80];
    struct timeval        timeout;
    Packet                *rcvd_packet;
    int                   sequence_number = -1;
    int                   loss_rate;
    int                   timeout_counter = 0;

    /* Need one arguements: loss_rate_percent */
    if(argc != 2) {
        printf("Usage: rcv <loss_rate_percent>\n");
        exit(0);
    }

    /* Set loss rate */
    loss_rate = atoi(argv[1]);
    sendto_dbg_init(loss_rate);
 
    sr = socket(AF_INET, SOCK_DGRAM, 0);  /* socket for receiving (udp) */
    if (sr<0) {
        perror("Ucast: socket");
        exit(1);
    }

    name.sin_family = AF_INET;
    /* Receive from anybody */ 
    name.sin_addr.s_addr = INADDR_ANY;
    /* port to use */ 
    name.sin_port = htons(PORT);

    /* socket on which to receive*/
    if ( bind( sr, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Ucast: bind");
        exit(1);
    }
    /*Socket on which to send*/
    ss = socket(AF_INET, SOCK_DGRAM, 0); /* socket for sending (udp) */
    if (ss<0) {
        perror("Ucast: socket");
        exit(1);
    }

    /*Set address family and port*/
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = htons(PORT);
    
    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );
    for(;;)
    {
        temp_mask = mask;
        if (is_transferring == 0) {
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
        } else {
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
        }
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, &timeout);
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                from_len = sizeof(from_addr);
                /* get data from ethernet interface*/
                bytes = recvfrom( sr, mess_buf, sizeof(mess_buf), 0,  
                          (struct sockaddr *)&from_addr, 
                          &from_len );
                from_ip = from_addr.sin_addr.s_addr;
                /* TODO: extract logic for handling received packet */
                rcvd_packet = malloc(sizeof(Packet));
                if (!rcvd_packet) {
                    printf("Malloc failed for new tranfer queue node.\n");
                    exit(0);
                }
                memcpy((char *)rcvd_packet, mess_buf, MAX_PACKET_SIZE);
                if (rcvd_packet->type == (char) 0) {
                    /* TODO: Handle tranfer packet */
                    handleTransferPacket(rcvd_packet, from_ip, ss, &send_addr);             
                } else if (is_transferring != 0 && transfer_queue_head->sender_ip == from_ip){
                    /* handling data packet. */
                    sequence_number = handleDataPacket(
                                        (DataPacket *) rcvd_packet, bytes,
                                        from_ip, ss, &send_addr, 
                                        sequence_number);
                }

                printf( "Received from (%d.%d.%d.%d)\n", 
						(htonl(from_ip) & 0xff000000)>>24,
						(htonl(from_ip) & 0x00ff0000)>>16,
						(htonl(from_ip) & 0x0000ff00)>>8,
						(htonl(from_ip) & 0x000000ff));

            }else if( FD_ISSET(0, &temp_mask) ) {
                bytes = read( 0, input_buf, sizeof(input_buf) );
                input_buf[bytes] = 0;
                printf( "There is an input: %s\n", input_buf );
                sendto( ss, input_buf, strlen(input_buf), 0, 
                    (struct sockaddr *)&send_addr, sizeof(send_addr) );
            }
	    } else {
            if (is_transferring == 0 && transfer_queue_head != NULL) {
                initiateTransfer(transfer_queue_head->name, transfer_queue_head->sender_ip, ss, &send_addr);
                is_transferring = 1;
                timeout_counter = 0;
            }
            if (is_transferring && ++timeout_counter >= 10) {
            
            }
		    printf(".");
		    fflush(0);
        }
    }

    return 0;

}

/* Returns possibly updated sequence number  */
int handleDataPacket(DataPacket *packet, int packet_size, int ip,
                      int ss, struct sockaddr_in *send_addr, 
                      int sequence_number) {
    printf("Handling data packet with id: %d\n", (unsigned char) (packet->id));
    int number_of_nacks = 0;
    int write_size = 0;
    /* If the packet has not been set yet, but it in the window */
    if (window[(unsigned char) ((packet->id) % WINDOW_SIZE)] == NULL) {
        window[(unsigned char)((packet->id) % WINDOW_SIZE)] = packet;
        if (packet->type == (char)2) {
            size_of_last_payload = packet_size - sizeof(PACKET_ID) - sizeof(PACKET_TYPE);
        }
    }
    /* If the received packet id is the expected id */
    if (packet->id == (char) ((sequence_number + 1) % WINDOW_SIZE)) {
        unsigned char itr = (packet->id) % WINDOW_SIZE;
        /* Write payload to file */
        while(window[(unsigned char)itr] != NULL) {
            sequence_number = itr;
            /* TODO: Do we need to check how many bytes fwrite wrote? (it's return val) */
            /* payload size = packet_size - size of ID field - size of type field*/
            if (window[(unsigned int) itr]->type == (char) 2) {
                /* Write the last payload */ 
                write_size = size_of_last_payload;
                /* TODO: SET VALUE FOR CLOSING FILE WRITER*/ 
            } else {
                write_size = MAX_PACKET_SIZE - sizeof(PACKET_ID) - sizeof(PACKET_TYPE);
            }
            fwrite(window[itr]->payload, 1, write_size, fw);
            if (window[(unsigned int) itr]->type == (char) 2) {
               fclose(fw); 
            }
            window[(unsigned int) itr] = NULL;
            itr++;
            itr %= WINDOW_SIZE;
        }
        /* Update Nack Queue */
        NackNode nack_itr = nack_queue_head;
        while (nack_queue_head != NULL && nack_queue_tail != NULL) {
            if ((nack_queue_tail->id > nack_queue_head->id && nack_queue_head->id < sequence_number)
                || (nack_queue_tail->id < nack_queue_head->id) 
                    && (nack_queue_head->id < sequence_number || nack_queue_tail->id >= sequence_number)) {
                nack_itr = nack_queue_head;
                nack_queue_head = nack_queue_head->next;
                free(nack_itr);
            } else {
                break;
            }
        }
        if (nack_queue_head == NULL) {
            nack_queue_tail = NULL;
        }            
    } else {
        /* This is not the next expected packet. It is already, maybe, added to window above. 
         * Now possibly update nack queue. */
         
    }
    /* Send ack-nack packet*/
    Packet *responsePacket = malloc(sizeof(Packet));
    if (!responsePacket) {
        printf("Malloc failed for ack-nack Packet.\n");
        exit(0);
    }
    /*IP address of host to send to.*/
    send_addr->sin_addr.s_addr = ip; 
     
    /* Ack-nack packet type */
    printf("prepare ack nack packet\n");
    responsePacket->type = (char) 2;
    responsePacket->payload[0] = (char)sequence_number;
    number_of_nacks = transferNacksToPayload(&((responsePacket->payload)[1]), packet->id, (char)sequence_number); 
    sendto_dbg(ss, (char *)responsePacket, sizeof(PACKET_TYPE) + sizeof(PACKET_ID) + (number_of_nacks + 1) * sizeof(PACKET_ID), 0,
            (struct sockaddr *)send_addr, sizeof(*send_addr));

    /* Return new sequence number */
    return sequence_number;
}

int transferNacksToPayload(char *nack_payload_ptr, char rcvd_id, char sequence_id) {
    int number_of_nacks_added = 0;
    NackNode *itr = nack_queue_head;
    while (itr != NULL 
           && ((sequence_id < rcvd_id && itr->id < rcvd_id)
                || (rcvd_id < sequence_id
                    && (rcvd_id > itr->id || itr->id > sequence_id)))) {
        memcpy(nack_payload_ptr, &itr->id, sizeof(itr->id)); 
        itr++;
        nack_payload_ptr++;
        number_of_nacks_added++;
    }
    return number_of_nacks_added;
}

void handleTransferPacket(Packet *packet, int ip, int ss, struct sockaddr_in *send_addr) {
    printf("file name: %s\n", packet->payload);
    /* If the ip is not in the queue, we want to add it to the tranfer queue */
    if (!isInQueue(ip)) {
        addToQueue(packet, ip);
    }
    /* If the current sender is first in the queue, want to initiate 
tranfer. */
    if (transfer_queue_head->sender_ip == ip) {
        /* handle transfer initiation */
        initiateTransfer(packet->payload, ip, ss, send_addr);
    } else {
        /* Send not-ready-for-transfer packet */
        Packet *responsePacket = malloc(sizeof(Packet));
        if (!responsePacket) {
            printf("Malloc failed for not-ready-for-tranfer Packet.\n");
            exit(0);
        }
        /*IP address of host to send to.*/
        send_addr->sin_addr.s_addr = ip; 
        
        /* Not-ready-for-transfer packet type */
        responsePacket->type = (char) 1;
        
        /* Send not-ready-for-transfer packet */
        sendto_dbg(ss, (char *)responsePacket, sizeof(char), 0,
                (struct sockaddr *)send_addr, sizeof(*send_addr));
        }
}

char isInQueue(int ip) {
    Node *itr = transfer_queue_head;
    while (itr != NULL) {
        if (itr->sender_ip == ip) {
            return 1;
        }
        itr =itr->next;
    }
    return 0;
}
 /* TODO: change Packet * to char * for name */ 
void addToQueue(Packet *packet, int ip) {
    Node *newNode = malloc(sizeof(Node)); 
    if (!newNode) {
        printf("Malloc failed for new tranfer queue node.\n");
        exit(0);
    }
    newNode->name = packet->payload;
    newNode->next = NULL;
    newNode->sender_ip = ip;
    if (transfer_queue_head == NULL) {
        transfer_queue_head = newNode;
        transfer_queue_tail = newNode;
    } else {
        transfer_queue_tail->next = newNode;
        transfer_queue_tail = newNode;
    }
}
/* fixed usage. 
*/
void initiateTransfer(char *file_name, int ip, int ss, 
                      struct sockaddr_in *send_addr) {
    /* Create ready for transfer response packet */
    Packet *responsePacket = malloc(sizeof(Packet));
    if (!responsePacket) {
        printf("Malloc failed for ready-for-tranfer response packet.\n");
        exit(0);
    }
    /*IP address of host to send to.*/
    send_addr->sin_addr.s_addr = ip; 
    
    /* Ready-for-transfer packet type */
    responsePacket->type = (char) 0;
    
    /* Send ready-for-transfer packet */
    sendto_dbg(ss, (char *)responsePacket, sizeof(char), 0,
               (struct sockaddr *)send_addr, sizeof(*send_addr));

    if (is_transferring == 0) {
         /* Clear window for new transfer. */
        int i;
        for (i = 0; i < WINDOW_SIZE; i++) {
            if (window[i] != NULL) {
                free(window[i]);
                window[i] = NULL;
            }
        }
        /* Clear nack queue for new transfer */
        NackNode *tmp;
        while (nack_queue_head != NULL) {
            tmp = nack_queue_head;
            nack_queue_head = nack_queue_head->next;
            free(tmp);
        }
        nack_queue_tail = NULL;

        /* Only open file for writing if not already opened. */
        if((fw = fopen(file_name, "w")) == NULL) {
            perror("fopen");
            exit(0);
        }
        is_transferring = 1;
    }
}
