#include "net_include.h"

/*
 * Copies a file from the current machine to a target machine.
 * Arguments: t_ncp <file_src> <file_dst>@<target>
 */

int main(int argc, char *argv[])
{
	struct 	sockaddr_in 	host;
	struct 	hostent		h_ent, *p_h_ent;
	char	host_name[80];
	
	FILE	*file_src;	// File source
	char	*file_dst;	// File destination
	char	*target;	// Target machine
	
	int	s;		// Socket
	int	ret;
	int	nread;
	int	mess_len;
	char	mess_buf[MAX_MESS_LEN];
	char	*neto_mess_ptr = &mess_buf[sizeof(mess_len)];

	// Open file source and get destination from arguments:
	if ((file_src = fopen(argv[1], "r")) == NULL)
	{
		perror("fopen");
		exit(0);
	}

	file_dst = strtok(argv[2], "@");
	target = strtok(NULL, "\0");

	// Open TCP socket:	
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s<0) 
	{
		perror("t_ncp: socket error");
		exit(1);
	}

	host.sin_family = AF_INET; 	// Set internet address family
	host.sin_port	= htons(PORT); 	// Set port

	// Get target machine info from name:
	p_h_ent = gethostbyname(target);
	if (p_h_ent == NULL)
	{
		printf("t_ncp: gethostbyname error.\n");
		exit(1);
	}

	memcpy(&h_ent, p_h_ent, sizeof(h_ent));
	memcpy(&host.sin_addr, h_ent.h_addr_list[0], sizeof(host.sin_addr));

	// Open TCP connection:
	ret = connect(s, (struct sockaddr *)&host, sizeof(host));
	if (ret < 0)
	{
		perror("t_ncp: could not connect to server");
		exit(1);
	}

	// Send file:
	for (;;)
	{
		nread = fread(mess_buf, 1, MAX_MESS_LEN, file_src);
		if (nread > 0)
			// SEND PACKET
			printf("SEND PACKET HERE\n");
			ret = send(s, mess_buf, nread, 0);
			if (ret != nread)
			{
				perror("t_ncp: error in writing chunk");
				exit(0);
			}
		else if (nread < MAX_MESS_LEN)
		{
			if (feof(file_src))
			{ 
				printf("Finished writing!\n");
				break;
			}
			else
			{
				perror("t_ncp: error in reading file\n");
				exit(0);
			}
		}
	}

	// Close file:
	fclose(file_src);
	return 0;
}
