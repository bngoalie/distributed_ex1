#include "net_include.h"

/*
 * Copies a file from the current machine to a target machine.
 * Arguments: t_ncp <file_src> <file_dst>@<target>
 */
	
int main(int argc, char *argv[])
{
	struct 	    sockaddr_in host;
	struct 	    hostent		h_ent, *p_h_ent;
	int 	    s;		    /* Socket */
	int         ret;
    int	        bytes = 0;
    int         done = 0;
    int         name_len;
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

    printf("Destination filename: %s\n", test);

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
        printf("running loop");
        /* Read chunk from file */
		bytes = fread(file_buf, 1, MAX_PACKET_SIZE, file_src); 
		if (bytes > 0) { 
            /* Send chunk */
			send(s, file_buf, bytes, 0);
            /* Close if EOF */
            if (feof(file_src))
                done = 1;
		} else { /* Else 0 bytes read (file divides into packet size) */
            send(s, file_buf, 0, 0);    /* Send empty closing packet */
            done = 1;
		}
	}

    /* Close and return */
    fclose(file_src);
	return 0;
}
