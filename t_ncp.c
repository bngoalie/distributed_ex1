#include "net_include.h"
#include <unistd.h>
#define MEG     1048576
#define MEG50   50*MEG

/*
 * Copies a file from the current machine to a target machine.
 * Arguments: t_ncp <file_src> <file_dst>@<target>
 */

#define delay 100
	
int main(int argc, char *argv[])
{
	struct 	    sockaddr_in host;
	struct 	    hostent		h_ent, *p_h_ent;
	struct      timeval start_time;
    struct      timeval prev_time;
    struct      timeval end_time;
    int         total_time;
    int 	    s;		    /* Socket */
	int         ret;
    int	        bytes = 0;
    int         done = 0;
    int         name_len;
    long        sent = 0;
    long        threshold = MEG50;
	char	    file_buf[MAX_PACKET_SIZE];
    char        *test;
    char        *file_dst = &file_buf[sizeof(name_len)];
	char	    *target;	/* Target machine string */
	FILE	    *file_src;	/* File source handler */
	/* Open file source and get destination from arguments: */
	if ((file_src = fopen(argv[1], "r")) == NULL) {
		perror("fopen");
		exit(0);
	}

	test = strtok(argv[2], "@");
	target = strtok(NULL, "\0");

	/* Open TCP socket: */
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s<0) {
		perror("t_ncp: socket error\n");
		exit(1);
	}
	host.sin_family = AF_INET; 	/* Set internet address family */
	host.sin_port	= htons(PORT); 	/* Set port */

	/* Get target machine info from name: */
	p_h_ent = gethostbyname(target);
	if (p_h_ent == NULL) {
		printf("t_ncp: gethostbyname error.\n");
		exit(1);
	}
	memcpy(&h_ent, p_h_ent, sizeof(h_ent));
	memcpy(&host.sin_addr, h_ent.h_addr_list[0], sizeof(host.sin_addr));

    /* Begin measuring time */
    gettimeofday(&start_time, 0); /* Start time */
    prev_time.tv_sec = start_time.tv_sec;
    prev_time.tv_usec = start_time.tv_usec;
	
    /* Open TCP connection: */
	ret = connect(s, (struct sockaddr *)&host, sizeof(host));
	if (ret < 0) {
		perror("t_ncp: could not connect to server\n");
		exit(1);
	}

    /* Send filename (and size) in first packet: */
    name_len = strlen(test) + sizeof(name_len);
    memcpy(file_buf, &name_len, sizeof(name_len));
    memcpy(file_dst, test, strlen(test));
    send(s, file_buf, name_len, 0);

	/* Send file chunks: */
    while(!done)
	{
        /* Read chunk from file */
		bytes = fread(file_buf, 1, MAX_PACKET_SIZE, file_src); 
        send(s, file_buf, bytes, 0);
        sent += bytes;
        /* Output status */ 
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
        /* Check if end */
        if(bytes < MAX_PACKET_SIZE) {
            done = 1;
            if (done != 0) /* Send zero byte if haven't already */
                send(s, file_buf, 0, 0);
        }
	}

    /* Measure time, close and return */
    gettimeofday(&end_time, 0);
    total_time = (end_time.tv_sec*1e6 + end_time.tv_usec) -
                    (start_time.tv_sec*1e6 + start_time.tv_usec);
    printf("Total size: %lu bytes\n", sent);
    printf("Total time: %d usec\n", total_time);
    printf("Total rate: %f megabits/sec", 8.0*sent/total_time);
    fclose(file_src);
	return 0;
}
