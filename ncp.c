#include "net_include.h"
#include "sendto_dbg.h"

#define NAME_LENGTH 80

int gethostname(char*,size_t);

void PromptForHostName( char *my_name, char *host_name, size_t max_len ); 

int main(int argc, char **argv)
{
    struct sockaddr_in    name;
    struct sockaddr_in    send_addr;
    struct sockaddr_in    from_addr;
    socklen_t             from_len;
    struct hostent        h_ent;
    struct hostent        *p_h_ent;
    char                  *host_name;
    char                  my_name[NAME_LENGTH] = {'\0'};
    int                   host_num;
    int                   from_ip;
    int                   ss,sr;
    fd_set                mask;
    fd_set                dummy_mask,temp_mask;
    int                   bytes;
    int                   num;
    char                  mess_buf[MAX_MESS_LEN];
    char                  input_buf[80];
    struct timeval        timeout;
    int loss_rate;
    /* Pointer to source file, which we read */
    FILE *fr;
    char *dest_file_name;
    char *source_file_name; 
    int dest_file_str_len, host_str_len;
    Packet *packet;
    int packet_size;

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

    printf( "Sending file %s from %s to %s on host %s.\n", source_file_name, 
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
    /* TODO: If use enums, then have to consider size in Packet */
    packet->type = (char) 0;
    strcpy(packet->payload, dest_file_name);

    /* Size of packet is only as big as it needs to be (size of ID + size of
       payload/name of destination file name) */
    sendto_dbg(ss, (char *)packet, packet_size, 0,
		   (struct sockaddr *)&send_addr, sizeof(send_addr));

    FD_ZERO( &mask );
    FD_ZERO( &dummy_mask );
    FD_SET( sr, &mask );
    for(;;)
    {
        temp_mask = mask;
        
        /* TODO: set number of usec (microsec) appropriately */
        timeout.tv_sec = 0;
	    timeout.tv_usec = 0;
        num = select( FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, 
                      &timeout);
        if (num > 0) {
            if ( FD_ISSET( sr, &temp_mask) ) {
                from_len = sizeof(from_addr);
                /* get data from ethernet interface*/
                bytes = recvfrom( sr, mess_buf, sizeof(mess_buf), 0,  
                          (struct sockaddr *)&from_addr, 
                          &from_len );
                mess_buf[bytes] = 0;
                from_ip = from_addr.sin_addr.s_addr;

                printf( "Received from (%d.%d.%d.%d): %s\n", 
								(htonl(from_ip) & 0xff000000)>>24,
								(htonl(from_ip) & 0x00ff0000)>>16,
								(htonl(from_ip) & 0x0000ff00)>>8,
								(htonl(from_ip) & 0x000000ff),
								mess_buf );

            } else {
                /* TODO: what happened? */
            }
	} else {
                /* Select timed out. Send a packet.
                   TODO: If haven't reached end of window, and nack queue is 
                   empty or nothing in nack queue should be sent again 
                   TODO: read chunk from file (see filecopy.c) */
                bytes = read( 0, input_buf, sizeof(input_buf) );
                input_buf[bytes] = 0;
                printf( "There is an input: %s\n", input_buf );
                /* TODO: change to use sendto_dbg */
                sendto( ss, input_buf, strlen(input_buf), 0, 
                    (struct sockaddr *)&send_addr, sizeof(send_addr) );
        }
    }

    return 0;

}
