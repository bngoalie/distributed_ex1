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
int transferNacksToPayload(PACKET_ID *nack_payload_ptr, PACKET_ID rcvd_id, 
                           PACKET_ID sequence_id);
int handleDataPacket(DataPacket *packet, int packet_size, int ip,
                      int ss, struct sockaddr_in *send_addr, 
                      int sequence_number);
    
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
    PACKET_ID id;
    int       count;
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
    PACKET_ID             sequence_number = -1;
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
            timeout.tv_sec = 0;
            timeout.tv_usec = 500;
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
                memcpy((char *)rcvd_packet, mess_buf, bytes);
                if (rcvd_packet->type == (PACKET_TYPE) 0) {
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

            }
	} else {
            if (is_transferring == 0 && transfer_queue_head != NULL) {
                initiateTransfer(transfer_queue_head->name, transfer_queue_head->sender_ip, ss, &send_addr);
                timeout_counter = 0;
            }
            if (is_transferring && ++timeout_counter >= 100) {
            
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
                      PACKET_ID sequence_number) {
    printf("Handling data packet with id: %d\n", packet->id);
    int number_of_nacks = 0;
    int write_size = 0;
    /* If the packet has not been set yet, but it in the window */
    if (window[(packet->id) % WINDOW_SIZE] == NULL) {
        window[(packet->id) % WINDOW_SIZE] = packet;
        if (packet->type == (PACKET_TYPE)2) {
            size_of_last_payload = packet_size - sizeof(PACKET_ID) - sizeof(PACKET_TYPE);
        }
    }
    /* If the received packet id is the expected id */
    if (packet->id == (sequence_number + 1)) {
        PACKET_ID itr = packet->id;
        /* Write payload to file */
        while(window[itr % WINDOW_SIZE] != NULL) {
            sequence_number = itr;
            /* TODO: Do we need to check how many bytes fwrite wrote? (it's return val) */
            /* payload size = packet_size - size of ID field - size of type field*/
            if (window[itr % WINDOW_SIZE]->type == (PACKET_TYPE) 2) {
                /* Write the last payload */ 
                write_size = size_of_last_payload;
                /* TODO: SET VALUE FOR CLOSING FILE WRITER*/ 
            } else {
                write_size = PAYLOAD_SIZE;
            }
            fwrite(window[itr % WINDOW_SIZE]->payload, 1, write_size, fw);
            if (window[itr % WINDOW_SIZE]->type == (PACKET_ID) 2) {
               fclose(fw); 
            }
            window[itr % WINDOW_SIZE] = NULL;
            itr++;
        }
        /* Update Nack Queue */
        NackNode *nack_free;
        while (nack_queue_head != NULL 
               && nack_queue_head->id <= sequence_number) {
            printf("removing from nack queue id: %d\n", nack_queue_head->id);
            nack_free = nack_queue_head;
            nack_queue_head = nack_queue_head->next;
            free(nack_free);
        }
        if (nack_queue_head == NULL) {
            nack_queue_tail = NULL;
        } 
    } else {
        /* This is not the next expected packet. It is already, maybe, added to window above. 
         * Now possibly update nack queue. */
        if (nack_queue_tail != NULL && nack_queue_tail->id < packet->id) {
            /*The received packet's id is after the id of the tail of nack 
             * queue. need to add nacks for all id's from tail to packet id */
            int idx;
            for (idx = nack_queue_tail->id + 1; idx < packet->id; idx++) {
                nack_queue_tail->next = malloc(sizeof(NackNode));
                if(nack_queue_tail->next == NULL) {                
                    printf("Malloc failed for NackNode.\n");
                    exit(0);
                }
                nack_queue_tail->next->id = idx;
                nack_queue_tail->next->count = 0;
                nack_queue_tail = nack_queue_tail->next;
                nack_queue_tail->next = NULL;
            }
        } else if (nack_queue_head == NULL) {
            /* The nack queue is empty. Add nacks for elements from 
             * sequence_number+1 to id-1 */
            /* malloc for the nack_queue_head */
            nack_queue_head = malloc(sizeof(NackNode));
            if(nack_queue_head == NULL) {                
                printf("Malloc failed for NackNode.\n");
                exit(0);
            }
            nack_queue_head->next = NULL;
            nack_queue_head->count = 0;
            nack_queue_head->id = sequence_number + 1;
            nack_queue_tail = nack_queue_head;
            /* Add new nacks */
            int idx;
            for (idx = sequence_number + 2; idx < packet->id; idx++) {
                nack_queue_tail->next = malloc(sizeof(NackNode));
                if(nack_queue_tail->next == NULL) {                
                    printf("Malloc failed for NackNode.\n");
                    exit(0);
                }
                nack_queue_tail->next->id = idx;
                nack_queue_tail = nack_queue_tail->next;
                nack_queue_tail->count = 0;
                nack_queue_tail->next = NULL;
            }
        } else if (packet->id >= nack_queue_head->id) {
            NackNode *tmp;
            if ( nack_queue_head->id == packet->id ) {
                if (nack_queue_head == nack_queue_tail) {
                    /* If one element queue, have to assign tail.*/
                    nack_queue_tail = NULL;
                }
                tmp = nack_queue_head;
                nack_queue_head = nack_queue_head->next;
                free(tmp);
            } else {
                NackNode *itr;
                itr = nack_queue_head;
                while (itr->next != NULL) {
                    if (itr->next->id == packet->id) {
                        if (itr->next == nack_queue_tail) {
                            nack_queue_tail = itr;
                        }
                        tmp = itr->next;
                        itr->next = tmp->next;
                        free(tmp);
                        break;
                    }
                }
            }
        }
    }
    /* Send ack-nack packet*/
    AckNackPacket *responsePacket = malloc(sizeof(AckNackPacket));
    if (!responsePacket) {
        printf("Malloc failed for ack-nack Packet.\n");
        exit(0);
    }
    /*IP address of host to send to.*/
    send_addr->sin_addr.s_addr = ip; 
     
    /* Ack-nack packet type */
    /* TODO: what should ack be if haven't received a packet yet?*/
    printf("prepare ack nack packet\n");
    responsePacket->type = (PACKET_TYPE) 2;
    responsePacket->ack_id = (PACKET_ID)sequence_number;
    number_of_nacks = transferNacksToPayload(&(responsePacket->nacks[0]), 
                          packet->id, sequence_number); 
    sendto_dbg(ss, (char *)responsePacket, sizeof(PACKET_TYPE)
               + sizeof(PACKET_ID) + number_of_nacks*sizeof(PACKET_ID), 0,
               (struct sockaddr *)send_addr, sizeof(*send_addr));

    /* Return new sequence number */
    return sequence_number;
}

int transferNacksToPayload(PACKET_ID *nack_payload_ptr, PACKET_ID rcvd_id, 
                           PACKET_ID sequence_id) {
    int number_of_nacks_added = 0;
    NackNode *itr = nack_queue_head;
    while (itr != NULL && itr->id < rcvd_id) {
        if (itr->count % NACK_WAIT_COUNT == 0) {
            printf("adding %u to nack payload\n", itr->id);
            (itr->count)++;
            memcpy(nack_payload_ptr, &itr->id, sizeof(PACKET_ID)); 
            nack_payload_ptr++;
            number_of_nacks_added++;
        }
        itr = itr->next;
    }
    return number_of_nacks_added;
}

void handleTransferPacket(Packet *packet, int ip, int ss, 
                          struct sockaddr_in *send_addr) {
    printf("file name: %s\n", packet->payload);
    /* If the ip is not in the queue, we want to add it to the tranfer queue */
    if (!isInQueue(ip)) {
        addToQueue(packet, ip);
    }
    /* If the current sender is first in the queue, want to initiate 
tranfer. */
    if (transfer_queue_head != NULL && transfer_queue_head->sender_ip == ip) {
        /* handle transfer initiation */
        initiateTransfer(packet->payload, ip, ss, send_addr);
    } else {
        /* Send not-ready-for-transfer packet */
        Packet responsePacket;
        /*IP address of host to send to.*/
        send_addr->sin_addr.s_addr = ip; 
        
        /* Not-ready-for-transfer packet type */
        responsePacket.type = (PACKET_TYPE) 1;
        
        /* Send not-ready-for-transfer packet */
        sendto_dbg(ss, (char *)(&responsePacket), sizeof(PACKET_TYPE), 0,
                (struct sockaddr *)send_addr, sizeof(*send_addr));
        }
}

char isInQueue(int ip) {
    Node *itr = transfer_queue_head;
    while (itr != NULL) {
        if (itr->sender_ip == ip) {
            return 1;
        }
        itr = itr->next;
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
    Packet responsePacket;
    /*IP address of host to send to.*/
    send_addr->sin_addr.s_addr = ip; 
    
    /* Ready-for-transfer packet type */
    responsePacket.type = (PACKET_TYPE) 0;
    
    /* Send ready-for-transfer packet */
    sendto_dbg(ss, (char *)(&responsePacket), sizeof(PACKET_TYPE), 0,
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
