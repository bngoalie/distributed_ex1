/* Includes */
#include "net_include.h"
#include "sendto_dbg.h"

/* Constants */
#define NAME_LENGTH 80

/* Function prototypes */
int gethostname(char*,size_t);
void PromptForHostName( char *my_name, char *host_name, size_t max_len ); 
void handleTransferPacket(Packet *packet, FILE *fw, int ip, int ss, struct sockaddr_in *send_addr);
char isInQueue(int ip);
void addToQueue(Packet *packet, int ip);
void initiateTransfer(Packet *packet, FILE *fw, int ip, int ss, struct sockaddr_in *send_addr);

    
/* Structs */
typedef struct dummy_node 
{
    int         sender_ip;
    char        *name;
    struct dummy_node *next;
} Node;

Node *transfer_queue_head = NULL;
Node *transfer_queue_tail = NULL;
    
int main()
{
    struct sockaddr_in    name;
    struct sockaddr_in    send_addr;
    struct sockaddr_in    from_addr;
    socklen_t             from_len;
    struct hostent        h_ent;
    struct hostent        *p_h_ent;
    char                  host_name[NAME_LENGTH] = {'\0'};
    char                  my_name[NAME_LENGTH] = {'\0'};
    int                   host_num;
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

    FILE *fw = NULL; /* Pointer to dest file, to which we write  */
    
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
    FD_SET( (long)0, &mask ); /* stdin */
    /* TODO: For receiving tranfer request packets, the payload does not include
       the null-terminator character. Must be added. */
    for(;;)
    {
        temp_mask = mask;
        timeout.tv_sec = 10;
	timeout.tv_usec = 0;
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
                rcvd_packet = (Packet *)mess_buf;
                if (rcvd_packet->type == (char) 0) {
                    /* TODO: Handle tranfer packet */
                    handleTransferPacket(rcvd_packet, fw, from_ip, ss, &send_addr);             
                } else {
                    /* TODO: use function for handling data packet. */
                }

                printf( "Received from (%d.%d.%d.%d): %s\n", 
						(htonl(from_ip) & 0xff000000)>>24,
						(htonl(from_ip) & 0x00ff0000)>>16,
						(htonl(from_ip) & 0x0000ff00)>>8,
						(htonl(from_ip) & 0x000000ff),
						mess_buf );

            }else if( FD_ISSET(0, &temp_mask) ) {
                bytes = read( 0, input_buf, sizeof(input_buf) );
                input_buf[bytes] = 0;
                printf( "There is an input: %s\n", input_buf );
                sendto( ss, input_buf, strlen(input_buf), 0, 
                    (struct sockaddr *)&send_addr, sizeof(send_addr) );
            }
	} else {
		printf(".");
		fflush(0);
        }
    }

    return 0;

}

void PromptForHostName( char *my_name, char *host_name, size_t max_len ) {

    char *c;

    gethostname(my_name, max_len );
    printf("My host name is %s.\n", my_name);

    printf( "\nEnter host to send to:\n" );
    if ( fgets(host_name,max_len,stdin) == NULL ) {
        perror("Ucast: read_name");
        exit(1);
    }
    
    c = strchr(host_name,'\n');
    if ( c ) *c = '\0';
    c = strchr(host_name,'\r');
    if ( c ) *c = '\0';

    printf( "Sending from %s to %s.\n", my_name, host_name );

}

void handleTransferPacket(Packet *packet, FILE *fw, int ip, int ss, struct sockaddr_in *send_addr) {
    /* If the ip is not in the queue, we want to add it to the tranfer queue */
    if (!isInQueue(ip)) {
        addToQueue(packet, ip);
        /* If the current sender is first in the queue, want to initiate 
tranfer. */
        if (transfer_queue_head->sender_ip == ip) {
            /* handle tranfer initiation: ready for tranfer packet, open file 
writer */
            initiateTransfer(packet, fw, ip, ss, send_addr);
            /* Only open file for writing if not already opened. */
            if(fw != NULL && (fw = fopen(packet->payload, "w")) == NULL) {
                perror("fopen");
                exit(0);
            }
        }
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
void initiateTransfer(Packet *packet, FILE *fw, int ip, int ss, 
                      struct sockaddr_in *send_addr) {
    Packet *responsePacket = malloc(sizeof(packet));
    if (!responsePacket) {
        printf("Malloc failed for ready-for-tranfer response packet.\n");
        exit(0);
    }
    /*IP address of host to send to.*/
    send_addr->sin_addr.s_addr = host_num; 
    
    responsePacket->type = (char) 0;
    sendto_dbg(ss, (char *)packet, sizeof(char), 0,
               (struct sockaddr *)send_addr, sizeof(*send_addr));
    /* Only open file for writing if not already opened. */
    if(fw != NULL && (fw = fopen(packet->payload, "w")) == NULL) {
        perror("fopen");
        exit(0);
    }
}
