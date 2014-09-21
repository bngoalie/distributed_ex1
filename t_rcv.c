#include "net_include.h"
#include <unistd.h>
#define MEG    1048576
#define MEG50  50*MEG
int main()
{
	fd_set	    mask;
	fd_set	    dummy_mask, temp_mask;
    struct      sockaddr_in name;
    struct      timeval start_time;
    struct      timeval prev_time;
    struct      timeval end_time;
    int         total_time;
    int         mess_len;
    int         name_len;
	int	        s;
	int	        num;
	char	    mess_buf[MAX_PACKET_SIZE];
    long	    on = 1;
    long        sent = 0;
    long        threshold = MEG50;
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
                    gettimeofday(&start_time, 0); /* Start time */
                    prev_time.tv_sec = start_time.tv_sec;
                    prev_time.tv_usec = start_time.tv_usec;
                    s = accept (s, 0, 0);
                    FD_SET(s, &mask);
                    conn = 1;
                    printf("Connection established\n");
                    if (recv(s, &mess_len,sizeof(mess_len), 0) > 0) {
                        name_len = mess_len - sizeof(mess_len);
                        bytes = recv(s, mess_buf, name_len, 0 );
                        mess_buf[name_len] = '\0';
                        printf("Opening file %s for writing...\n", &mess_buf[0]);
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
                        sent += bytes;
                        if(sent >= threshold){
                            threshold += MEG50;
                            gettimeofday(&end_time, 0);
                            total_time = (end_time.tv_sec*1e6 + end_time.tv_usec) - 
                                    (prev_time.tv_sec*1e6 + prev_time.tv_usec);
                            printf("Amount transferred: %f Megabytes\n",
                                    (double) sent / MEG);
                            printf("Rate of last 50M: %f megabits/sec\n", 
                                    8.0 * MEG50 / (double) total_time);
                            prev_time.tv_sec = end_time.tv_sec;
                            prev_time.tv_usec = end_time.tv_usec;
                        }
                        fwrite(&mess_buf[0], 1, bytes, fw);
                    }
                    /*if (bytes < MAX_PACKET_SIZE || feof(fw))*/
                    if (bytes == 0)
                        done = 1;
                }   
            }
        }
    }
    
    /* Measure time, close and return */
    gettimeofday(&end_time, 0);
    total_time = (end_time.tv_sec*1e6 + end_time.tv_usec) - 
                    (start_time.tv_sec*1e6 + start_time.tv_usec);
    printf("Total size: %lu bytes\n", sent);
    printf("Total time: %d usec\n", total_time);
    printf("Total rate: %f megabits/sec", 8.0*sent/total_time);
    fclose(fw);
    return 0;	
}
