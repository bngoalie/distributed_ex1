/* Includes */
#include "net_include.h"
#include "sendto_dbg.h"

/* Constants */
#define NAME_LENGTH 80
#define NUM_OF_NACKS_TO_WAIT 8

/* Function prototypes */
int gethostname(char*,size_t);
void PromptForHostName( char *my_name, char *host_name, size_t max_len );

typedef struct dummy_nack_node {
    PACKET_ID       id;
    struct dummy_nack_node *next;
} NackNode;

/* Main TODO: This is way too long, break it into functions */
int main(int argc, char **argv)
{
    struct sockaddr_in      name;
    struct sockaddr_in      send_addr;
    struct sockaddr_in      from_addr;
    socklen_t               from_len;
    struct hostent          h_ent;
    struct hostent          *p_h_ent;
    char                    *host_name;
    char                    my_name[NAME_LENGTH] = {'\0'};
    int                     host_num;
    int                     ss,sr;
    fd_set                  mask;
    fd_set                  dummy_mask,temp_mask;
    int                     bytes;
    int                     num;
    char                    mess_buf[MAX_PACKET_SIZE];
    char                    input_buf[PAYLOAD_SIZE];
    PACKET_ID               packet_id;
    char                    begun;
    struct timeval          timeout;
    int                     loss_rate;
    FILE                    *fr; /* Pointer to source file, which we read */
    char                    *dest_file_name;
    char                    *source_file_name; 
    int                     dest_file_str_len, host_str_len;
    Packet                  *packet;
    Packet                  *rcvd_packet;
    DataPacket              *dPacket;
    Packet                  *response_packet;
    AckNackPacket           *ack_nack_packet;
    int                     packet_size;    
    char                    end_of_window;
    PACKET_ID                    start_of_window;
    char                    at_end_of_window;
    NackNode                nack_list_head;
    DataPacket              *window[WINDOW_SIZE];
    int                     size_of_last_packet = 0;
    int                     timeout_counter;
    PACKET_ID               ack_id;
    /* Need three arguements: loss_rate_percent, source_file_name, and 
       dest_file_name@comp_name */
    if(argc != 4) {
        printf("Usage: ncp <loss_rate_percent> <source_file_name> \
            <dest_file_name>@<comp_name>\n");
        exit(0);
    }

    /* Set loss rate */
    loss_rate = atoi(argv[1]);
    sendto_dbg_init(loss_rate);

    /* Open the source file for reading */
    source_file_name = argv[2];
    if((fr = fopen(source_file_name, "r")) == NULL) {
      perror("fopen");
      exit(0);
    }
    printf("Opened %s for reading...\n", argv[2]);

    /* Get dest_file_name and set length of the string*/
    if (!(dest_file_name = strtok(argv[3], "@"))) {
        perror("incorrect form for <dest_file_name>@<comp_name>");
        exit(0);
    }
    dest_file_str_len = strlen(dest_file_name);

    /* Get host_name to which to send the file. Set and check str length */
    host_name = strtok(NULL, " ");   
    host_str_len = strlen(host_name);
    if (host_str_len > NAME_LENGTH) {
        perror("Too long host name");
        exit(0);
    }

    gethostname(my_name, NAME_LENGTH);
    printf("My host name is %s.\n", my_name);

    printf("Attempting to send file %s from %s to %s on host %s.\n", source_file_name, 
            my_name, dest_file_name, host_name );

    /* AF_INET: interested in doing it on the internet. SOCK_DGRAM: 
       socket of datagram? */
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
    /* Socket on which to send */
    ss = socket(AF_INET, SOCK_DGRAM, 0); /* socket for sending (udp) */
    if (ss<0) {
        perror("Ucast: socket");
        exit(1);
    }
    /* Using Domain Name Service. Get IP address that is 4 bytes long. */
    /* PromptForHostName(my_name,host_name,NAME_LENGTH);*/
    
    p_h_ent = gethostbyname(host_name);
    if ( p_h_ent == NULL ) {
        printf("Ucast: gethostbyname error.\n");
        exit(1);
    }

    memcpy( &h_ent, p_h_ent, sizeof(h_ent));
    memcpy( &host_num, h_ent.h_addr_list[0], sizeof(host_num) );

    send_addr.sin_family = AF_INET;
    /* IP address of host to send to. */
    send_addr.sin_addr.s_addr = host_num; 
    send_addr.sin_port = htons(PORT);

    /* Send transfer request packet */
    /* Add 1 for null-terminator. */
    packet_size = sizeof(char) + dest_file_str_len + 1;
    if (packet_size > sizeof(Packet)) {
        perror("Packet size for tranfer request packet to large");
        exit(0);
    }
    packet = malloc(sizeof(Packet));
    if (!packet) {
        printf("Malloc failed.\n");
        exit(0);
    }
    packet->type = (char) 0;
    strcpy(packet->payload, dest_file_name);

    /* Size of packet is only as big as it needs to be (size of ID + size of
       payload/name of destination file name) */
    sendto_dbg(ss, (char *)packet, packet_size, 0,
		   (struct sockaddr *)&send_addr, sizeof(send_addr));
    
    /* Prepare for transfer */
    packet_id = 0;  /* Start with packet 0 */
    begun = 0;      /* We have not begun transfer */

    /* Format masks for IO multiplexing */
    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );

    /* Set delay to three seconds while waiting to init */
    timeout.tv_sec = 1; 
	timeout.tv_usec = 0;

    /* Set window DataPacket pointers to NULL*/ 
    int i;
    for (i = 0; i < WINDOW_SIZE; i++) {
        window[i] = NULL;
    }
    
    nack_list_head.next = NULL;

    /* Indefinite I/O multiplexing loop */
    int eof = 1;
    while(eof != 0)
    {
        if (begun == 0) {
             /* Set delay to three seconds while waiting to init */
            timeout.tv_sec = 1; 
	    timeout.tv_usec = 0;
        } else {
           timeout.tv_sec = 0;
           timeout.tv_usec= 1000; /* Send packet every 0.5ms */
        }


        printf("sec: %d. usec: %d.\n", timeout.tv_sec, timeout.tv_usec); 
        temp_mask = mask;
        
        /* Multiplex select */
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, &timeout);
        if (num > 0)    /* Select has been triggered */ 
        {
            if ( FD_ISSET( sr, &temp_mask) ) /* Receiving socket has packet */
            {
                printf("RECEIVED A PACKET\n");
                /* Get data from ethernet interface */
                from_len = sizeof(from_addr);
                bytes = recvfrom( sr, mess_buf, sizeof(mess_buf), 0,  
                          (struct sockaddr *)&from_addr, 
                          &from_len );
                printf("size of packet is %d bytes\n", bytes);
                rcvd_packet = malloc(sizeof(Packet));
                if (!rcvd_packet) {
                    printf("Malloc failed for new tranfer queue node.\n");
                    exit(0);
                }
                memcpy((char *)rcvd_packet, mess_buf, bytes);

                if(begun == 0) /* Transfer has not yet begun */
                {
                    printf("entered begun if\n");
                    if (rcvd_packet->type == (PACKET_TYPE)0)  
                    { /* Receiver is ready TODO: cast to packet type */

                        if (packet != NULL) {
                            free(packet);
                            packet = NULL;
                        }
                        begun = 1;
                        start_of_window = 0;
                        at_end_of_window = 0;
                        printf("Transfer has begun...");
                   }
                    else    /* Receiver is NOT ready */
                    {
                        printf("The receiver is currently busy handling another transfer.");
                        timeout.tv_sec = 5;
                    }
                } else if (rcvd_packet->type == (PACKET_TYPE)2) {
                    /* Transfer has already begun. Received ack/nack packet */
                    ack_nack_packet = (AckNackPacket *)rcvd_packet;
                    ack_id = ack_nack_packet->ack_id;
                    printf("ack_id %d\n",ack_id);
                    
                    int nackIdx;
                    for (nackIdx = 0; nackIdx < (bytes - sizeof(PACKET_TYPE) - 
                            sizeof(PACKET_ID))/sizeof(PACKET_ID); nackIdx++) {
                        printf("packet has nack id %d,", 
                                ack_nack_packet->nacks[nackIdx]); 
                    }
                    printf("\n");

                    /* If the cummuluative ack is in the window. we remove 
                     * packets from the sender's window and shift the window*/
                    if (ack_id >= start_of_window) {
                        /* free packets up to ack_id, move window*/
                        for (;start_of_window < ack_id + 1;start_of_window++) {
                            if (window[start_of_window % WINDOW_SIZE] == NULL) 
                                printf("null\n");
                            free(window[start_of_window % WINDOW_SIZE]);
                            window[start_of_window % WINDOW_SIZE] = NULL;
                        }
                        /*start_of_window++;*/
                    }
                    /* ACK/NACK QUEUE/RESPONSE LOGIC HERE */
                    if (((bytes - sizeof(PACKET_TYPE) - sizeof(PACKET_ID)) 
                        >= sizeof(PACKET_ID)) && ack_id >= start_of_window-1) {
                        NackNode *nackNodeItr = &nack_list_head;
                        while (nackNodeItr->next != NULL) {
                            printf("nack queueue has %d\n", 
nackNodeItr->next->id);
                            nackNodeItr = nackNodeItr->next;
                        }
                        /* If there is at least one nack in the packet*/
                        
                        /* send response packet for first nack */
                        response_packet = 
                            (Packet *)window[(ack_nack_packet->nacks[0]) % 
                                             WINDOW_SIZE];
                        packet_size = MAX_PACKET_SIZE;
                        if (response_packet->type == (PACKET_TYPE)2) {
                            packet_size = size_of_last_packet;
                        }
                        sendto_dbg( ss, (char *)response_packet, packet_size, 0,
                            (struct sockaddr *)&send_addr, sizeof(send_addr) );
                        NackNode *nack_list_itr = &nack_list_head;
                        NackNode *tmp;
                        while (nack_list_itr->next != NULL 
                            && nack_list_itr->next->id 
                                < ack_nack_packet->nacks[0]) {
                            nack_list_itr = nack_list_itr->next;
                        }
                        if (nack_list_itr->next != NULL 
                            && nack_list_itr->next->id 
                                == ack_nack_packet->nacks[0]) {
                            tmp = nack_list_itr->next;
                            nack_list_itr->next = tmp->next;
                            free(tmp);
                        }
                        
                        /* Merge nacks into nack queue. */
                        nack_list_itr = &nack_list_head;
                        int idx;
                        for (idx = 1; idx < (bytes - sizeof(PACKET_TYPE) - 
                             2*sizeof(PACKET_ID))/sizeof(PACKET_ID);) {
                            if (nack_list_itr->next != NULL 
                                && nack_list_itr->next->id  
                                    == ack_nack_packet->nacks[idx]) {
                                idx++;
                            } else if (nack_list_itr->next == NULL 
                                || nack_list_itr->next->id 
                                    > ack_nack_packet->nacks[idx]) {
                                tmp = malloc(sizeof(NackNode));
                                if (!tmp) {
                                    perror("Malloc failed.\n");
                                    exit(0);
                                }
                                tmp->next = nack_list_itr->next;
                                tmp->id = ack_nack_packet->nacks[idx];
                                nack_list_itr->next = tmp;
                                idx++;
                            }
                            nack_list_itr = nack_list_itr->next;
                        }
                    }
                }
                free(rcvd_packet);
            } 
            else    /* Something else triggered select (shouldn't happen) */
            {
                perror("ncp: invalid select option");
                exit(1);    
            }
	} 
        else 
        {
            NackNode *nackNodeItr = &nack_list_head;
            while (nackNodeItr->next != NULL) {
                printf("nack queueue has %d\n", nackNodeItr->next->id);
                nackNodeItr = nackNodeItr->next;
            }
            printf("timeout\n");
            /* Select has timed out. Send a packet. */
            if (begun == 0) /* Transfer has not yet begun. Send transfer packet */
            {
                sendto_dbg(ss, (char *)packet, packet_size, 0,
		        (struct sockaddr *)&send_addr, sizeof(send_addr));
                printf("Attempting to initiate transfer\n");
            } else if (nack_list_head.next != NULL) {
                printf("first node in nack list: %d\n", 
nack_list_head.next->id);
                dPacket = window[nack_list_head.next->id % WINDOW_SIZE];
                
                packet_size = MAX_PACKET_SIZE;
                if (dPacket->type == (PACKET_TYPE) 2) {
                    packet_size = size_of_last_packet;
                }
                sendto_dbg( ss, (char *)dPacket, packet_size, 0,
                    (struct sockaddr *)&send_addr, sizeof(send_addr));
                NackNode *tmp_nack_node = nack_list_head.next;
                nack_list_head.next = nack_list_head.next->next;
                free(tmp_nack_node);
            }
            else if (packet_id < start_of_window + WINDOW_SIZE) {
                /* Transfer has already begun. Not at end of window. Send data 
                 * packet */
                /* Read file into char buffer */
                bytes = fread (input_buf, 1, PAYLOAD_SIZE, fr);
                
                /* Form data packet */
                dPacket = malloc(sizeof(DataPacket));
                if (!dPacket) {
                    printf("Malloc failed.\n");
                    exit(0);
                }          
 
                packet_size = MAX_PACKET_SIZE;
                if(feof(fr)) /* If we've reached the EOF, set type = 2 */ {
                    fclose(fr);
                    eof = 0;
                    dPacket->type = (PACKET_TYPE)2;
                    size_of_last_packet = bytes + sizeof(PACKET_ID) + 
                                          sizeof(PACKET_TYPE);
                    packet_size = size_of_last_packet;
                } else {
                    /* If full-size packet, set type = 1 */
                    dPacket->type = (PACKET_TYPE)1;
                }
                dPacket->id = packet_id;
                
                memcpy(dPacket->payload, input_buf, bytes);
                printf("sending data packet\n");
                sendto_dbg( ss, (char *)dPacket, packet_size, 0,
                    (struct sockaddr *)&send_addr, sizeof(send_addr) );

                /* Store packet in array for future use */
                window[packet_id % WINDOW_SIZE] = dPacket;
                packet_id++;
            } 
            else {
                /* TODO: We have timed out after hitting the end of the window*/
                /* increment timeout counter.*/ 
            }
        }
    }
    /* Free memory from window*/ 
    int itr;
    for (itr = 0; itr < WINDOW_SIZE; itr++) {
        if (window[itr] != NULL) {
            free(window[itr]);
            window[itr] = NULL;
        }
    } 
    return 0;
}
