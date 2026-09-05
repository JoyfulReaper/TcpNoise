#include "network.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>

#define LISTEN_BACKLOG 64

int create_socket(int family)
{
	int sfd;
	int enabled = 1;
	
	if ((sfd = socket(family, SOCK_STREAM, 0)) < 0)
	{
		perror("socket");
		return -1;
	}
	
	if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) == -1)
	{
		perror("setsockopt SO_REUSEADDR");
		close(sfd);
		return -1;
	}
	
	if(family == AF_INET6)
	{
		if(setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &enabled, sizeof enabled) == -1)
		{
			perror("setsockopt IPV6_V6ONLY");
			close(sfd);
			return -1;
		}
	}
	
	return sfd;
}

bool bind_socket(int sfd, uint16_t port, int family)
{	
	if(family == AF_INET)
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
	}
	else if (family == AF_INET6)
	{
		struct sockaddr_in6 address;
		memset(&address, 0, sizeof address);
		address.sin6_family = AF_INET6;
		address.sin6_addr = in6addr_any;
		address.sin6_port = htons(port);
		
		if(bind(sfd, (struct sockaddr*)&address, sizeof address) != 0)
		{
			perror("bind");
			return false;
		} 
	}
	else
	{
		fprintf(stderr, "Invalid family\n");
		return false;
	}
	
	return true;
}


bool listen_socket(int sfd)
{
	if(listen(sfd, LISTEN_BACKLOG) != 0)
	{
		perror("listen");
		return false;
	}
	
	return true;
}

int accept_connection(int sfd, struct sockaddr_storage *peer_addr, socklen_t *peer_addr_size)
{
	int cfd;
	if((cfd = accept(sfd, (struct sockaddr *)peer_addr, peer_addr_size)) < 0)
	{
		if (errno != EINTR)
		{
			perror("accept");
		}

		return -1;
	}
	
	return cfd;
}
