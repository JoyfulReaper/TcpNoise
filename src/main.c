#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>

#define ENABLE_LOGGING

volatile sig_atomic_t running = 1;

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
		if (errno != EINTR)
		{
			perror("accept");
		}

		return -1;
	}
	
	return cfd;
}

void handle_sigint(int signal)
{
	(void)signal;
	running = 0;
}

void print_payload(FILE *output, const char *payload, ssize_t length)
{
	for (ssize_t i = 0; i < length; i++)
	{
		unsigned char c = (unsigned char)payload[i];

		if (c >= 32 && c <= 126)
		{
			fputc(c, output);
		}
		else if (c == '\n')
		{
			fprintf(output, "\\n");
		}
		else if (c == '\r')
		{
			fprintf(output, "\\r");
		}
		else if (c == '\t')
		{
			fprintf(output, "\\t");
		}
		else
		{
			fprintf(output, "\\x%02x", c);
		}
	}

	fputc('\n', output);
}

// Possible improvment, keep the file open
bool log_connection (
	int port, 
	const char *message,
	const char *payload,
	ssize_t length)
{
	#ifndef ENABLE_LOGGING
		return true;
	#endif
	
	char log_filename[32];
	snprintf(log_filename, sizeof log_filename, "%d.log", port);
	
	FILE *file = fopen(log_filename, "a");
	if(file == NULL)
	{
		perror("fopen");
		return false;
	}
	
	if(fprintf(file, "%s\n", message) < 0)
	{
		fprintf(stderr, "Failed to write to log file.\n");
			
		fclose(file);
		return false;
	}
	
	if(length > 0)
	{
		fprintf(file, "- Payload: ");
		print_payload(file, payload, length);
	}
	else
	{
		fprintf(file, "- Payload: <none>\n");
	}
	
	if(fclose(file) != 0)
	{
		perror("fclose");
		return false;
	}
	return true;
}

int main (int argc, char *argv[])
{
	int port;
	if(!process_arguments(argc, argv, &port))
	{
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}
	
	struct sigaction action;
	memset(&action, 0, sizeof action);
	action.sa_handler = handle_sigint;
	sigemptyset(&action.sa_mask);
	
	if(sigaction(SIGINT, &action, NULL) == -1)
	{
		perror("sigaction");
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
	
	while (running) {
		struct sockaddr_in peer_addr;
		int cfd = accept_connection(sfd,  &peer_addr);	
		if(cfd < 0 && errno == EINTR && running == 0)
		{
			printf("\nSIGINT caught, shutting down...\n");
			break;
		}
		else if(cfd < 0)
		{
			close(sfd);
			return EXIT_FAILURE;
		}
		
		// Capture timestamp immediately upon accepting
		time_t now = time(NULL);
		struct tm *t_info = localtime(&now);
		char time_buffer[64];
		strftime(time_buffer, sizeof time_buffer, "%Y-%m-%d %H:%M:%S", t_info);
		
		connection_count++;
		
		struct timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 250000;
		
		if(setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) != 0)
		{
			close(cfd);
			close(sfd);
			perror("setsockopt");
			return EXIT_FAILURE;
		} 
		
		char payload[256];
		ssize_t bytes_received;
		
		bytes_received = recv(cfd, payload, sizeof payload, 0);
		
		if(bytes_received < 0 && errno == EINTR && running == 0)
		{
			close(cfd);
			break;
		}
		
		if(bytes_received < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			perror("recv");
		}
		
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &peer_addr.sin_addr, ip, sizeof ip);
		
		char output_buffer[100];
		int cx = snprintf(output_buffer, sizeof output_buffer, "click! [%d] [remote: %s] [%s]", connection_count, ip, time_buffer);
		if(cx >= (int)sizeof output_buffer)
		{
			fprintf(stderr, "output_buffer is too small\n");
			close(cfd);
			continue;
		}
		else if(cx < 0)
		{
			fprintf(stderr, "Encoding error\n");
			close(cfd);
			continue;
		}
		
		//printf("click! [%d] [remote: %s] [%s]\n", connection_count, ip, time_buffer);
		printf("%s\n", output_buffer);
		if(bytes_received == 0 || bytes_received < 0)
		{
			printf("- Payload: <none>\n");
		}
		else if(bytes_received > 0)
		{
			printf("- Payload: ");
			print_payload(stdout, payload, bytes_received);
		}
		
		close(cfd);
		log_connection(port, output_buffer, payload, bytes_received);
	}
	
	printf("Connection attempts: %d\n", connection_count);
	
	close(sfd);
	return EXIT_SUCCESS;
}
