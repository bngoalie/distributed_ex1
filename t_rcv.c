#include "net_include.h"

int main()
{
	struct	sockaddr_in name;
	int	s;
	fd_set	mask;
	int	recv_s[10];
	int	valid[10];
	fd_set	dummy_mask, temp_mask;
	int	i,j,num;
	int	mess_len;
	int 	neto_len;
	char	mess_buf[MAX_MESS_LEN];
	long	on=1;

	// Create TCP socket:
	s = socket(AF_INIT, SOCK_STREAM, 0);
	if (s<0)
	{
		perror("t_rcv: socket");
		exit(1);
	}

	// No clue about this:
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
	{
		perror("t_rcv: bind");
		exit(1);
	}

	if (listen(s, 4) < 0)
	{
		perror("t_rcv: listen");
		exit(1);
	}

	// Set up multiplex select:
	i = 0;
	FD_ZERO(&mask);
	FD_ZERO(&dummy_mask);
	FD_SET(s,&mask);

	// Loop select:
	for(;;) // indefinite
	{
		temp_mask = mask;
		num = select( FD_SETSIZE, &teamp_mask, &dummy_mask, &dummy_mask, NULL);
		if (num > 0)
		{
			if (FD_ISSET(s,&temp_mask))
			{
				recv_s[i] = accept(s, 0, 0);
				FD_SET(recv_s[i], &mask);
				valid[i] = 1;
				i++;
			}
			// for (j = 0; i
		}
	}	
}
