#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <poll.h>
#include <netdb.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>

#define ENABLE_LOGGING
#define MAX_SEEN_IPS 1000
#define MAX_PAYLOAD_LEN 256
#define LISTEN_BACKLOG 64
#define MAX_PORT 10

volatile sig_atomic_t running = 1;

struct seen_ip
{
	char ip[INET_ADDRSTRLEN];
	unsigned int count;
};

struct listener
{
	int fd;
	uint16_t port;
	int family;
	uint64_t connection_count;
};

struct connection_event
{
	uint64_t connection_number;
	uint16_t port;
	uint16_t remote_port;
	char ip[INET6_ADDRSTRLEN];
	uint64_t seen_count;
	char timestamp[64];
	char payload[MAX_PAYLOAD_LEN];
	ssize_t payload_len;
};

int find_seen_ip(
	struct seen_ip records[],
	size_t record_count,
	const char *ip)
{
	for(size_t i = 0; i < record_count; i++)
	{
		if(strcmp(ip, records[i].ip) == 0)
			return (int)i;
	}
	
	return -1;
}

uint64_t increment_seen_ip(
	struct seen_ip records[],
	size_t *record_count,
	const char *ip)
{
	int index = find_seen_ip(records, *record_count, ip);
	if(index < 0) // We haven't see this IP address before
	{
		if(*record_count >= MAX_SEEN_IPS)
		{
			fprintf(stderr, "Seen IP buffer is full.\n");
			return 0;
		}
		
		records[*record_count].count = 1;
		snprintf(records[*record_count].ip, sizeof records[*record_count].ip, "%s", ip);
		
		(*record_count)++;
		
		return 1;
	}
		
	records[index].count++;
	return records[index].count;
}

void print_usage(char *program)
{
	fprintf(stderr, "Usage: %s <port> [port ...]\n", program);
}

bool uint16_exists(const uint16_t *arr, size_t size, uint16_t target)
{
	for (size_t i = 0; i < size; i++)
	{
		if (arr[i] == target)
		{
			return true;
		}
	}
	return false;
}

bool process_arguments(
	int argc,
	char *argv[],
	uint16_t *ports,
	size_t *port_count)
{
	if(argc < 2 || argc - 1 > MAX_PORT)
	{
		return false;
	}

	for(int i = 1; i < argc; i++)
	{
		char *end;
		long value = strtol(argv[i], &end, 10);

		if(end == argv[i] || *end != '\0')
		{
			return false;
		}

		if(value < 1 || value > 65535)
		{
			return false;
		}

		uint16_t parsed_port = (uint16_t)value;

		if(!uint16_exists(ports, *port_count, parsed_port))
		{
			ports[*port_count] = parsed_port;
			(*port_count)++;
		}
	}

	return true;
}

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

void close_listeners(struct listener listeners[], size_t listener_count)
{
	for(size_t i = 0; i < listener_count; i++)
	{
		close(listeners[i].fd);
	}
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

void handle_signal(int signal)
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
	uint16_t port, 
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
	// Parse port
	uint16_t ports[MAX_PORT];
	size_t port_count = 0;
	
	struct listener listeners[MAX_PORT];
	size_t listener_count = 0;
	
	struct pollfd pollfds[MAX_PORT];
	
	if(!process_arguments(argc, argv, ports, &port_count))
	{
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}
	
	// Register signal handler
	struct sigaction action;
	memset(&action, 0, sizeof action);
	action.sa_handler = handle_signal;
	sigemptyset(&action.sa_mask);
	
	if(sigaction(SIGINT, &action, NULL) == -1)
	{
		perror("sigaction SIGINT");
		return EXIT_FAILURE;
	}
	
	if(sigaction(SIGTERM, &action, NULL) == -1)
	{
		perror("sigaction SIGTERM");
		return EXIT_FAILURE;
	}
	
	// Create, Bind and Listen on socket
	for(size_t i = 0; i < port_count; i++)
	{
		listeners[i].family = AF_INET;
		listeners[i].port = ports[i];
		listeners[i].fd = create_socket(listeners[i].family);
		listeners[i].connection_count = 0;
		
		pollfds[i].fd = listeners[i].fd;
		pollfds[i].events = POLLIN;
		pollfds[i].revents = 0;
		
		if (listeners[i].fd < 0)
		{
			close_listeners(listeners, listener_count);
			return EXIT_FAILURE;
		}
		
		if(!bind_socket(listeners[i].fd, listeners[i].port, listeners[i].family))
		{
			close(listeners[i].fd);
			close_listeners(listeners, listener_count);
			return EXIT_FAILURE;
		}
		
		if(!listen_socket(listeners[i].fd))
		{
			close(listeners[i].fd);
			close_listeners(listeners, listener_count);
			return EXIT_FAILURE;
		}
		
		listener_count++;
		
		printf("Listening for noise on port: %hu\n", listeners[i].port);
	}
	
	struct seen_ip records[MAX_SEEN_IPS];
	size_t record_count = 0;
	
	// Main loop
	while (running) {
		int num_selected = poll(pollfds, listener_count, -1);
		if(num_selected == -1 && errno == EINTR && running == 0)
		{
			printf("\nShutdown signal caught, shutting down...\n");
			break;
		}
		if (num_selected == -1)
		{
			perror("poll");
			close_listeners(listeners, listener_count);
			return EXIT_FAILURE;
		}
		
		// Poll
		for(size_t i = 0; i < listener_count; i++)
		{
			if(pollfds[i].revents & POLLIN)
			{
				struct sockaddr_storage peer_addr;
				socklen_t peer_addr_size = sizeof peer_addr;
				char service[NI_MAXSERV];
				int cfd = accept_connection(listeners[i].fd, &peer_addr, &peer_addr_size);
				
				if(cfd < 0 && errno == EINTR && running == 0)
				{
					printf("\nShutdown signal caught, shutting down...\n");
					break;
				}
				else if(cfd < 0)
				{
					close_listeners(listeners, listener_count);
					return EXIT_FAILURE;
				}
				
				struct connection_event event;
				
				// Capture timestamp immediately upon accepting
				snprintf(event.timestamp, sizeof event.timestamp, "(unknown time)");
				time_t now = time(NULL);
				if(now != (time_t) -1)
				{
					struct tm *t_info = localtime(&now);
					if(t_info != NULL)
					{
						if(strftime(event.timestamp, sizeof event.timestamp, "%Y-%m-%d %H:%M:%S", t_info) == 0)
						{
							snprintf(event.timestamp, sizeof event.timestamp, "(unknown time)");
						}
					}
				}
				
				listeners[i].connection_count++;
				event.connection_number = listeners[i].connection_count;
				event.port = listeners[i].port;
				
				// Setup receive timeout
				struct timeval timeout;
				timeout.tv_sec = 0;
				timeout.tv_usec = 250000;
				
				if(setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) != 0)
				{
					close(cfd);
					close_listeners(listeners, listener_count);
					perror("setsockopt");
					return EXIT_FAILURE;
				} 
				
				// Read payload
				event.payload_len = recv(cfd, event.payload, sizeof event.payload, 0);
				if(event.payload_len < 0 && errno == EINTR && running == 0)
				{
					close(cfd);
					break;
				}
				
				if(event.payload_len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
				{
					perror("recv");
				}
				
				// Get remote information
				int res = getnameinfo(
					(struct sockaddr *)&peer_addr,
					peer_addr_size,
					event.ip,
					sizeof event.ip,
					service,
					sizeof service,
					NI_NUMERICHOST | NI_NUMERICSERV);
				if(res != 0)
				{
					fprintf(stderr, "getnameinfo: %s\n", gai_strerror(res));
					snprintf(event.ip, sizeof event.ip, "%s", "(unknown)");
					event.remote_port = 0;
				} else {		
					char *end;
					errno = 0;
					
					long remote_port = strtol(service, &end, 10);
					if(errno == ERANGE ||
					   end == service ||
					   *end != '\0' ||
					   remote_port < 1 ||
					   remote_port > 65535)
					{
						fprintf(stderr, "Failed to parse remote port: %s\n", service);
						event.remote_port = 0;
					}
					else
					{
						event.remote_port = (uint16_t)remote_port;
					}
				}
								
				// Have we seen this IP before during this run?
				event.seen_count = increment_seen_ip(records, &record_count, event.ip);
				if( event.seen_count == 0)
				{
					fprintf(stderr, "Failed to increment seen count\n");
				}
				
				char output_buffer[100];
				int cx = snprintf(
					output_buffer,
					sizeof output_buffer,
					"click! [connection #%" PRIu64 "] [port: %hu] [remote: %s:%" PRIu16 "] [%s] [seen: %" PRIu64 "]",
					event.connection_number,
					event.port,
					event.ip,
					event.remote_port,
					event.timestamp,
					event.seen_count);
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

				// Print final results
				printf("%s\n", output_buffer);
				if(event.payload_len == 0 || event.payload_len < 0)
				{
					printf("- Payload: <none>\n");
				}
				else if(event.payload_len > 0)
				{
					printf("- Payload: ");
					print_payload(stdout, event.payload, event.payload_len);
				}
				
				close(cfd);
				log_connection(listeners[i].port, output_buffer, event.payload, event.payload_len);
				
			}
		}
	}
	
	close_listeners(listeners, listener_count);
	for(size_t i = 0; i < listener_count; i++)
	{
		printf("Port %"PRIu16" connection attempts: %" PRIu64 "\n", listeners[i].port,listeners[i].connection_count);
	}
	
	return EXIT_SUCCESS;
}
