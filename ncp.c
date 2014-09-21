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
    Packet                  transfer_packet;
    Packet                  *rcvd_packet;
    DataPacket              *dPacket;
    Packet                  *response_packet;
    AckNackPacket           *ack_nack_packet;
    int                     packet_size;
    char                    end_of_window;
    PACKET_ID                    start_of_window;
    char                    at_end_of_window;
    NackNode                nack_list_head;
    DataPacket              window[WINDOW_SIZE];
    int                     size_of_last_packet = 0;
    int                     timeout_counter = 0;
    PACKET_ID               ack_id;
    char                    read_last_packet = 0;
    int                     burst_count = 0;

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

    printf("Attempting to send file %s from %s to %s on host %s.\n", 
source_file_name,
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
    packet_size = sizeof(PACKET_TYPE) + dest_file_str_len + 1;
    if (packet_size > sizeof(Packet)) {
        perror("Packet size for tranfer request packet to large");
        exit(0);
    }
    transfer_packet.type = (PACKET_TYPE) 0;
    strcpy(transfer_packet.payload, dest_file_name);

    /* Size of packet is only as big as it needs to be (size of ID + size of
       payload/name of destination file name) */
    sendto_dbg(ss, (char *)(&transfer_packet), packet_size, 0,
               (struct sockaddr *)&send_addr, sizeof(send_addr));

    /* Prepare for transfer */
    packet_id = -1;  /* ID of last packet sent. Start with packet -1 (none
                      * sent) */
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
        window[i].id = -1;
    }

    nack_list_head.next = NULL;

    /* Indefinite I/O multiplexing loop */
    int eof = 0;
    while(eof == 0)
    {
        if (begun == 0) {
            /* Set delay to three seconds while waiting to init */
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
        } else {
            timeout.tv_sec = 0;
            timeout.tv_usec= 50; /* Send packet every 0.5ms */
        }

        temp_mask = mask;

        /* Multiplex select */
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, 
&timeout);
        if (num > 0) {
            /* Select has been triggered */
            if ( FD_ISSET( sr, &temp_mask) ) /* Receiving socket has packet */
            {
                timeout_counter = 0;
                /* printf("RECEIVED A PACKET\n");*/
                /* Get data from ethernet interface */
                from_len = sizeof(from_addr);
                bytes = recvfrom( sr, mess_buf, sizeof(mess_buf), 0,
                                  (struct sockaddr *)&from_addr,
                                  &from_len );
                rcvd_packet = (Packet *) mess_buf;

                if(begun == 0) /* Transfer has not yet begun */
                {
                    if (rcvd_packet->type == (PACKET_TYPE)0)
                    {   /* Receiver is ready */
                        begun = 1;
                        start_of_window = 0;
                        at_end_of_window = 0;
                        printf("Transfer has begun...\n");
                    }
                    else {
                        /* Receiver is NOT ready */ 
                        printf("Receiver currently busy handling \
another transfer.");
                        timeout.tv_sec = 5;
                    }
                } else if (rcvd_packet->type == (PACKET_TYPE)2) {
                    /* Transfer has already begun. Received ack/nack packet */
                    ack_nack_packet = (AckNackPacket *)rcvd_packet;
                    ack_id = ack_nack_packet->ack_id;
                    if (read_last_packet != 0 && ack_id == packet_id ) {
                        eof = 1;
                    } else {
                        /* If the cummuluative ack is in the window. we remove
                         * packets from the sender's window and shift the
                         * window*/
                        if (ack_id >= start_of_window) {
                            /* free packets up to ack_id, move window*/
                            while(start_of_window < ack_id+1) {
                                window[start_of_window % WINDOW_SIZE].id = -1;
                                start_of_window++;
                            }
                        }
                        NackNode *tmp;
                        /* Remove stale nacks from nack list*/
                        while (nack_list_head.next != NULL
                                && nack_list_head.next->id <= ack_id) {
                            tmp = nack_list_head.next;
                            nack_list_head.next = tmp->next;
                            free(tmp);
                        }

                        /* ACK/NACK QUEUE/RESPONSE LOGIC HERE */
                        if (((bytes - sizeof(PACKET_TYPE) - sizeof(PACKET_ID))
                                >= sizeof(PACKET_ID))
                                && ack_id >= start_of_window-1) {
                            /* If there is at least one nack in the packet*/
                            /* send response packet for first nack */
                            response_packet =
                                (Packet *)(&(window[(ack_nack_packet->nacks[0])
                                                    % WINDOW_SIZE]));
                            packet_size = MAX_PACKET_SIZE;
                            if (response_packet->type == (PACKET_TYPE)2) {
                                packet_size = size_of_last_packet;
                            }
                            sendto_dbg( ss, (char *)response_packet,packet_size,
                                        0, (struct sockaddr *)&send_addr,
                                        sizeof(send_addr));
                            NackNode *nack_list_itr = &nack_list_head;
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


                }
            }
            else    /* Something else triggered select (shouldn't happen) */
            {
                perror("ncp: invalid select option");
                exit(1);
            }
        } else {
            burst_count = 0;
            /*printf("timeout\n");*/
            /* Select has timed out. Send a packet. */
            if (begun == 0) /* Transfer has not yet begun. Send transfer packet 
*/
            {
                sendto_dbg(ss, (char *)(&transfer_packet), packet_size, 0,
                           (struct sockaddr *)&send_addr, sizeof(send_addr));
                /*  printf("Attempting to initiate transfer\n");*/
            } else if (nack_list_head.next != NULL) {
                /* printf("first node in nack list: %d\n",
                     nack_list_head.next->id);*/
                dPacket = &(window[nack_list_head.next->id % WINDOW_SIZE]);

                packet_size = MAX_PACKET_SIZE;
                if (dPacket->type == (PACKET_TYPE) 2) {
                    packet_size = size_of_last_packet;
                }
                sendto_dbg( ss, (char *)dPacket, packet_size, 0,
                            (struct sockaddr *)&send_addr, sizeof(send_addr));
                NackNode *tmp_nack_node = nack_list_head.next;
                nack_list_head.next = nack_list_head.next->next;
                free(tmp_nack_node);
            } else if (read_last_packet == 0
                       && packet_id + 1 < start_of_window + WINDOW_SIZE) {
                /* Transfer has already begun. Not at end of window. Send data
                 * packet */
                while (burst_count < BURST_MAX
                        && packet_id + 1 < start_of_window + WINDOW_SIZE
                        && fr != NULL) {
                    /* Increment to get the next id of the packet to be sent. */
                    packet_id++;
                    printf("create packet for id %d\n", packet_id);

                    /* Read file into char buffer */
                    bytes = fread(input_buf, 1, PAYLOAD_SIZE, fr);

                    /* Form data packet within the window */
                    dPacket = &(window[packet_id % WINDOW_SIZE]);

                    dPacket->id = packet_id;
                    packet_size = MAX_PACKET_SIZE;
                    if(feof(fr)) { /* If we've reached the EOF, set type = 2 */
                        printf("read last bytes, creating last packet\n");
                        fclose(fr);
                        fr = NULL;
                        read_last_packet = 1;
                        dPacket->type = (PACKET_TYPE)2;
                        size_of_last_packet = bytes + sizeof(PACKET_ID) +
                                              sizeof(PACKET_TYPE);
                        packet_size = size_of_last_packet;
                    } else {
                        /* If full-size packet, set type = 1 */
                        dPacket->type = (PACKET_TYPE)1;
                    }

                    memcpy(dPacket->payload, input_buf, bytes);
                    /* printf("sending data packet\n");*/
                    sendto_dbg( ss, (char *)dPacket, packet_size, 0,
                                (struct sockaddr *)&send_addr, 
                                sizeof(send_addr));
                    burst_count++;
                }

            } else {
                /* Resend packet from end of the window/most recently sent */
                dPacket = &(window[packet_id % WINDOW_SIZE]);

                packet_size = MAX_PACKET_SIZE;
                if (dPacket->type == (PACKET_TYPE) 2) {
                    packet_size = size_of_last_packet;
                }
                sendto_dbg( ss, (char *)dPacket, packet_size, 0,
                            (struct sockaddr *)&send_addr, sizeof(send_addr));
                /* We have timed out after hitting the end of the window*/
                /* increment timeout counter.*/
                if (++timeout_counter >= 1000) {
                    eof = 1;
                    if (read_last_packet == 0) {
                        fclose(fr);
                        fr = NULL;
                    }
                }
            }
        }
    }
    if (fr != NULL) {
        fclose(fr);
    }
    return 0;
}
