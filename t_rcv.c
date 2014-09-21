#include "net_include.h"

int main()
{
	fd_set	    mask;
	fd_set	    dummy_mask, temp_mask;
    struct      sockaddr_in name;
    int         mess_len;
    int         name_len;
	int	        s;
	int	        num;
	char	    mess_buf[MAX_PACKET_SIZE];
    long	    on=1;
    int         bytes = 0;
    int         conn = 0;
    int         done = 0;
    FILE        *fw = NULL;
    
	/* Create TCP socket */
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s<0) {
		perror("t_rcv: socket");
		exit(1);
	}

    /* Configure socket */
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0) {
		perror("t_rcv: bind");
		exit(1);
	}
    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(PORT);

    /* Bind and listen */
    if ( bind( s, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("t_rcv: bind");
        exit(1);
    }
	if (listen(s, 4) < 0) {
		perror("t_rcv: listen");
		exit(1);
	}

	/* Set up multiplexed I/O masks: */
	FD_ZERO(&mask);
	FD_ZERO(&dummy_mask);
	FD_SET(s,&mask);

	/* Multiplexed I/O loop: */
	while(!done) {
		temp_mask = mask;
		num = select(FD_SETSIZE, &temp_mask, &dummy_mask, &dummy_mask, NULL);
		if (num > 0) {
			if (FD_ISSET(s, &temp_mask)) {
                /* If no connection, establish and get filename */
                if (!conn) {
                    s = accept (s, 0, 0);
                    FD_SET(s, &mask);
                    conn = 1;
                    printf("Connection established\n");
                    if (recv(s, &mess_len,sizeof(mess_len), 0) > 0) {
                        name_len = mess_len - sizeof(mess_len);
                        bytes = recv(s, mess_buf, name_len, 0 );
                        mess_buf[name_len] = '\0';
                        printf("Opening file %s for reading...\n", &mess_buf[0]);
                        /* Filename packet - open file for writing */
                        if((fw = fopen(mess_buf, "w")) == NULL) {
                            perror("t_rcv: fopen");
                            exit(0);
                        }
                    } else {
                        perror("t_rcv: zero bytes returned from TCP stream");
                        exit(1);
                    }
                } else {  /* If connection is already established, get data */
                    bytes = recv(s, mess_buf, MAX_PACKET_SIZE, 0);
                    if (bytes > 0) {
                        fwrite(&mess_buf[0], 1, bytes, fw);
                    }
                    if (bytes < MAX_PACKET_SIZE || feof(fw))
                        done = 1;
                }   
            }
        }
    }
    
    /* Close and return */
    fclose(fw);
    return 0;	
}
