#include "net_include.h"

int main()
{
	struct	    sockaddr_in name;
	int	        s;
	fd_set	    mask;
	int	        recv_s[10];
	int	        valid[10];
	fd_set	    dummy_mask, temp_mask;
	int	        i,j,num;
	int	        mess_len;
	int 	    neto_len;
	char	    mess_buf[MAX_PACKET_SIZE];
	long	    on=1;
    TcpPacket   *packet;
    struct      sockaddr_in from_addr;
    socklen_t   from_len;
    int         bytes;
    int         done = 0;
    FILE        *fw = NULL;

	/* Create TCP socket: */
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s<0) {
		perror("t_rcv: socket");
		exit(1);
	}

	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0) {
		perror("t_rcv: bind");
		exit(1);
	}

	if (listen(s, 4) < 0) {
		perror("t_rcv: listen");
		exit(1);
	}

	/* Set up multiplexed I/O masks: */
	i = 0;
	FD_ZERO(&mask);
	FD_ZERO(&dummy_mask);
	FD_SET(s,&mask);

	/* Multiplexed I/O loop: */
	while(!done) {
		temp_mask = mask;
		num = select(FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, NULL);
		if (num > 0) {
			if (FD_ISSET(s, &temp_mask)) {
                /* Get data from ethernet interface */
                from_len    = sizeof(from_addr);
                bytes       = recvfrom( s, mess_buf, sizeof(mess_buf), 0,
                              (struct sockaddr *)&from_addr, &from_len );

                /* Form packet */
                packet = malloc(sizeof(TcpPacket));                 
                if (!packet) {
                    printf("Malloc failed for new tranfer queue node.\n");
                    exit(0);
                }
                memcpy((char *)packet, mess_buf, bytes);

                /* Handle according to type */
                if (packet->type == (PACKET_TYPE) 0) {
                    /* Filename packet - open file for writing */
                    if((fw = fopen(packet->payload, "w")) == NULL) {
                        perror("t_rcv: fopen");
                        exit(0);
                    }
                } else if (packet->type == (PACKET_TYPE) 1) {
                    /* Data packet - write to file*/
                    fwrite(packet->payload, 1, bytes - sizeof(PACKET_TYPE), fw);
                } else if (packet->type == (PACKET_TYPE) 2) {
                    /* End packet - write to file and close file */
                    fwrite(packet->payload, 1, bytes - sizeof(PACKET_TYPE), fw);
                    fclose(fw);
                    done = 1;
                }

                /* We're done with the packet, free it! */
                free(packet);
			}
		}
	}	
}
