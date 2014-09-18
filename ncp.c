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
    int             nack_count;
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
    char                    input_buf[80];
    int                     packet_id;
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
    int                     packet_size;    
    char                    end_of_window;
    char                    start_of_window;
    char                    at_end_of_window;
    NackNode                *nack_list_head = NULL;
    DataPacket              *window[WINDOW_SIZE];
    int                     size_of_last_packet = 0;

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

    /* Indefinite I/O multiplexing loop */
    int eof = 1;
    while(eof != 0)
    {
        if (begun == 0) {
             /* Set delay to three seconds while waiting to init */
            timeout.tv_sec = 1; 
	        timeout.tv_usec = 0;
        } else {
           timeout.tv_sec = 1;
           timeout.tv_usec= 0; /* Send packet every 0.5ms */
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

                bytes = recvfrom( sr, mess_buf, sizeof(mess_buf), 0,  
                          (struct sockaddr *)&from_addr, 
                          &from_len );
                rcvd_packet = (Packet *)mess_buf;
                from_len = sizeof(from_addr);
                if(begun == 0) /* Transfer has not yet begun */
                {
                    if (rcvd_packet->type == (PACKET_ID)0) /* Receiver is ready TODO: cast to packet type */
                    {
                        if (packet != NULL) {
                            free(packet);
                            packet = NULL;
                        }
                        begun = 1;
                        end_of_window = (char) (WINDOW_SIZE - 1);
                        start_of_window = 0;
                        at_end_of_window = 0;
                        printf("Transfer has begun...");
                   }
                    else    /* Receiver is NOT ready */
                    {
                        printf("The receiver is currently busy handling another transfer.");
                        timeout.tv_sec = 5;
                        /* TODO: possibly use new begun state to prevent sending redundant 
                         *  initiate transfer packets */
                    }
                }
                else    /* Transfer has already begun. Process ack/nacks */
                {
                    unsigned char ack_id = rcvd_packet->payload[0];
                    if (ack_id > start_of_window || ack_id < end_of_window) {
                        /* free packets up to ack_id, move window*/
                        while (window[ack_id % WINDOW_SIZE] != NULL) {
                            free(window[ack_id % WINDOW_SIZE]);
                            window[ack_id % WINDOW_SIZE] = NULL;
                            ack_id++;
                        }
                        start_of_window = ack_id % WINDOW_SIZE; 
                        end_of_window = (ack_id - 1) % WINDOW_SIZE;
                    }
                    /* TODO: ACK/NACK QUEUE/RESPONSE LOGIC HERE */
                    if ((bytes - sizeof(PACKET_TYPE) - sizeof(PACKET_ID)) >= sizeof(PACKET_ID)) {
                        response_packet = window[(unsigned char)(rcvd_packet->payload[1])];
                        int packet_size = MAX_PACKET_SIZE;
                        if (response_packet->type == (PACKET_TYPE)2) {
                            packet_size = size_of_last_packet;
                        }
                        sendto_dbg( ss, (char *)response_packet, packet_size, 0, /* change to use cast of dPacket */
                            (struct sockaddr *)&send_addr, sizeof(send_addr) );
                    } 

                }
            } 
            else    /* Something else triggered select (shouldn't happen) */
            {
                perror("ncp: invalid select option");
                exit(1);    
            }
	    } 
        else 
        {
            printf("timeout\n");
            /* Select has timed out. Send a packet. */
            if (begun == 0) /* Transfer has not yet begun. Send transfer packet */
            {
                sendto_dbg(ss, (char *)packet, packet_size, 0,
		        (struct sockaddr *)&send_addr, sizeof(send_addr));
                printf("Attempting to initiate transfer\n");
            }
            else if (window[packet_id % WINDOW_SIZE] == NULL)/* Transfer has already begun. Not at end of window. Send data packet */
            {
                /* TODO: Check if nack queue exisits. If so, process & send nack */
                /* TODO: If haven't reached end of window, and nack queue is 
                empty or nothing in nack queue should be sent again (what???) */ 
                
                /* Read file into char buffer */
                bytes = fread (input_buf, 1, PAYLOAD_SIZE, fr);
                
                /* Form data packet */
                dPacket = malloc(sizeof(DataPacket));
                if (!dPacket) {
                    printf("Malloc failed.\n");
                    exit(0);
                }

                /* TODO: Check window limit - Window start is oldest packet not acked*/               
 
                if(feof(fr)) /* If we've reached the EOF, set type = 2 */ {
                    eof = 0;
                    dPacket->type = (char)2;
                    size_of_last_packet = bytes + sizeof(PACKET_ID) + sizeof(PACKET_TYPE);
                }
                else /* If full-size packet, set type = 1 */
                    dPacket->type = (char)1;

                if (packet_id == end_of_window) {
                    at_end_of_window = 1;
                }

                dPacket->id = (char) (packet_id % WINDOW_SIZE);
                
                memcpy(&(dPacket->payload), input_buf, bytes);
                printf("sending packet with payload: %\n", dPacket->payload);
                sendto_dbg( ss, (char *)dPacket, bytes + 2*sizeof(char), 0, /* change to use cast of dPacket */
                (struct sockaddr *)&send_addr, sizeof(send_addr) );

                /* TODO: Store packet in array for future use */
                window[packet_id % WINDOW_SIZE] = dPacket;
                packet_id++;
            } 
            else {
                /* We have timed out after hitting the end of the window*/
                /* increment timeout counter. attempt to send nacks if necessary.*/ 
            }
        }
    }
    /* Free memory from window*/ 
    int itr;
    for (itr = 0; itr < WINDOW_SIZE; itr++) {
        if (window[itr] != NULL) {
            printf("free 314\n");
            free(window[itr]);
            window[itr] = NULL;
        }
    } 
    return 0;
}
