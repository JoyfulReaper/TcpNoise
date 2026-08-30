#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>

void print_usage(char *program)
{
	fprintf(stderr, "Usage: %s <port>\n", program);
}

bool process_arguments (int argc, char *argv[], int *port)
{
	if(argc != 2)
	{
		return false;
	}
	
	char *end;
	long value = strtol(argv[1], &end, 10);
	
	if(end == argv[1] || *end != '\0')
	{
		return false;
	}
	
	if (value < 1 || value > 65535)
		return false;
	
	*port = (int)value;
	
	return true;
}

int create_socket(void)
{
	int sfd;
	
	if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket");
		return -1;
	}
	
	return sfd;
}

bool bind_socket(int sfd, int port)
{
	struct sockaddr_in address;
	memset(&address, 0, sizeof address);
	
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);
	
	if(bind(sfd, (struct sockaddr*)&address, sizeof address) != 0)
	{
		perror("bind");
		return false;
	} 
	
	return true;
}

bool listen_socket(int sfd)
{
	if(listen(sfd, 5) != 0)
	{
		perror("listen");
		return false;
	}
	
	return true;
}

int accept_connection(int sfd, struct sockaddr_in *peer_addr)
{
	int cfd;
	socklen_t peer_addr_size = sizeof *peer_addr;
	if((cfd = accept(sfd, (struct sockaddr *)peer_addr, &peer_addr_size)) < 0)
	{
		perror("accept");
		return -1;
	}
	
	return cfd;
}

int main (int argc, char *argv[])
{
	int port;
	if(!process_arguments(argc, argv, &port))
	{
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}
	
	int sfd = create_socket();
	if (sfd < 0)
	{
		return EXIT_FAILURE;
	}
	
	if(!bind_socket(sfd, port))
	{
		close(sfd);
		return EXIT_FAILURE;
	}
	
	if(!listen_socket(sfd))
	{
		close(sfd);
		return EXIT_FAILURE;
	}
	
	printf("Listening for noise on port: %d\n", port);
	
	int connection_count = 0;
	
	while (true) {
		struct sockaddr_in peer_addr;
		int cfd = accept_connection(sfd,  &peer_addr);
		if(cfd < 0)
		{
			close(sfd);
			return EXIT_FAILURE;
		}
		
		connection_count++;
		
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &peer_addr.sin_addr, ip, sizeof ip);
		printf("click! [%d] [remote: %s]\n", connection_count, ip);
		close(cfd);
	}
	
	close(sfd);
	return EXIT_SUCCESS;
}
