#include "banner.h"
#include "seen.h"
#include "logging.h"
#include "network.h"
#include "database.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <poll.h>
#include <stddef.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>

#define MAX_PAYLOAD_LEN 256
#define MAX_PORT 10
#define MAX_LISTENERS (MAX_PORT * 2)

volatile sig_atomic_t running = 1;

struct listener
{
	int fd;
	uint16_t port;
	int family;
	size_t port_index;
	struct banner_pool const *banners;
	FILE *log_file;
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

enum connection_result
{
	CONNECTION_OK,
	CONNECTION_SHUTDOWN,
	CONNECTION_FATAL
};

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

void close_listeners(struct listener listeners[], size_t listener_count)
{
	for(size_t i = 0; i < listener_count; i++)
	{
		close(listeners[i].fd);
	}
}

void handle_signal(int signal)
{
	(void)signal;
	running = 0;
}

void capture_timestamp(char *buffer, size_t buffer_size)
{
	snprintf(buffer, buffer_size, "(unknown time)");
	
	time_t now = time(NULL);
	if(now == (time_t)-1)
	{
		return;
	}
	
	struct tm *t_info = localtime(&now);
	if(t_info == NULL)
	{
		return;
	}
	
	if(strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", t_info) == 0)
	{
		snprintf(buffer, buffer_size, "(unknown time)");
	}
}

enum connection_result handle_connection(
	int cfd,
	const struct listener *listener,
	uint64_t connection_number,
	const struct sockaddr_storage *peer_addr,
	socklen_t peer_addr_size,
	struct seen_ip records[],
	size_t *record_count)
{
	struct connection_event event;
	
	bool banner_sent = false;
	
	capture_timestamp(event.timestamp, sizeof event.timestamp);
	
	const char *banner = choose_banner(listener->banners);
	if(banner != NULL)
	{
		if(send_banner(cfd, banner))
		{
			banner_sent = true;
		} else {
			// printf("Failed to send banner: %s\n", banner);
		}
	}
	
	event.connection_number = connection_number;
	event.port = listener->port;

	if(!set_receive_timeout(cfd))
	{
		close(cfd);
		return CONNECTION_FATAL;
	}
	
	enum receive_status payload_result =
	receive_payload(
		cfd,
		event.payload,
		sizeof event.payload,
		&event.payload_len);

	if(payload_result == RECEIVE_INTERRUPTED)
	{
		if(!running)
		{
			close(cfd);
			return CONNECTION_SHUTDOWN;
		}

		perror("recv");
	}
	else if(payload_result == RECEIVE_ERROR)
	{
		perror("recv");
	}

	resolve_remote(
		peer_addr,
		peer_addr_size,
		event.ip,
		sizeof event.ip,
		&event.remote_port);
		
	enum seen_result seen_result = increment_seen_ip(records, record_count, event.ip, &event.seen_count);
	if(seen_result == SEEN_ERROR)
	{
		fprintf(stderr, "Failed to update seen-IP table\n");
		event.seen_count = 0;
	} else if (seen_result == SEEN_FULL)
	{
		fprintf(stderr, "Seen-IP table is full.\n");
		event.seen_count = 0;
	}

	char remote_endpoint[INET6_ADDRSTRLEN + 8];
	format_remote_endpoint(
		remote_endpoint,
		sizeof remote_endpoint,
		peer_addr->ss_family,
		event.ip,
		event.remote_port);
		
	const char *family_name =
		listener->family == AF_INET6 ? "IPv6" : "IPv4";
		
	const char *console_time = event.timestamp;
	if(strlen(event.timestamp) >= 19)
	{
		console_time = event.timestamp + 11;
	}

	char output_buffer[256];

	int cx = snprintf(
		output_buffer,
		sizeof output_buffer,
		"[%s] connection #%" PRIu64 "  TCP/%hu  %s  %s  seen=%" PRIu64,
		console_time,
		event.connection_number,
		event.port,
		family_name,
		remote_endpoint,
		event.seen_count);

	if(cx >= (int)sizeof output_buffer)
	{
		fprintf(stderr, "output_buffer is too small\n");
		close(cfd);
		return CONNECTION_OK;
	}
	else if(cx < 0)
	{
		fprintf(stderr, "Encoding error\n");
		close(cfd);
		return CONNECTION_OK;
	}
	
	char log_buffer[256];

	int lx = snprintf(
		log_buffer,
		sizeof log_buffer,
		"[%s] connection #%" PRIu64 "  TCP/%hu  %s  %s  seen=%" PRIu64,
		event.timestamp,
		event.connection_number,
		event.port,
		family_name,
		remote_endpoint,
		event.seen_count);

	if(lx >= (int)sizeof log_buffer)
	{
		fprintf(stderr, "log_buffer is too small\n");
		close(cfd);
		return CONNECTION_OK;
	}
	else if(lx < 0)
	{
		fprintf(stderr, "Encoding error\n");
		close(cfd);
		return CONNECTION_OK;
	}

	printf("%s\n", output_buffer);

	if(banner == NULL)
	{
		printf("           banner: <none>\n");
	}
	else if(banner_sent)
	{
		printf("           banner: %s\n", banner);
	}
	else
	{
		printf("           banner: <send failed> %s\n", banner);
	}

	if(payload_result == RECEIVE_DATA)
	{
		printf("           payload: ");
		print_payload(stdout, event.payload, event.payload_len);
	}
	else if(payload_result == RECEIVE_CLOSED)
	{
		printf("           payload: <peer closed>\n");
	}
	else if(payload_result == RECEIVE_TIMEOUT)
	{
		printf("           payload: <timeout>\n");
	}
	else if(payload_result == RECEIVE_INTERRUPTED)
	{
		printf("           payload: <interrupted>\n");
	}
	else
	{
		printf("           payload: <receive error>\n");
	}

	close(cfd);

	log_connection(
		listener->log_file,
		log_buffer,
		event.payload,
		event.payload_len,
		payload_result,
		banner,
		banner_sent);

	return CONNECTION_OK;
}

int main (int argc, char *argv[])
{
	srand(time(NULL));
	
	// Parse port
	uint16_t ports[MAX_PORT];
	size_t port_count = 0;
	
	struct listener listeners[MAX_LISTENERS];
	size_t listener_count = 0;
	
	uint64_t connection_counts[MAX_PORT] = {0};
	struct banner_pool banner_pools[MAX_PORT] = {0};
	
	FILE *log_files[MAX_PORT] = {0};
	
	struct pollfd pollfds[MAX_LISTENERS];
	
	int families[] = { AF_INET, AF_INET6 };
	size_t family_count = sizeof families / sizeof families[0];
	
	if(!process_arguments(argc, argv, ports, &port_count))
	{
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}
	
	// DEBUG CODE
	/*
	sqlite3 *db = NULL;

	if(!database_open("tcpnoise.db", &db))
	{
		fprintf(stderr, "Failed to open tcpnoise database.\n");
		return EXIT_FAILURE;
	}
	
	if(!database_initialize(db))
	{
		database_close(db);
		return EXIT_FAILURE;
	}
	
	uint64_t seen_count = 0;

	if(!database_record_seen_ip(
		db,
		"203.0.113.10",
		"2026-09-05 17:10:00",
		&seen_count))
	{
		fprintf(stderr, "Failed to record test seen IP.\n");
		database_close(db);
		return EXIT_FAILURE;
	}

	if(!database_close(db))
	{
		fprintf(stderr, "Failed to close tcpnoise database.\n");
		return EXIT_FAILURE;
	}

	printf("Test seen count: %" PRIu64 "\n", seen_count);
	*/
	// END DEBUG CODE
	
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
	
	action.sa_handler = SIG_IGN;
	if(sigaction(SIGPIPE, &action, NULL) == -1)
	{
		perror("sigaction SIGPIPE");
		return EXIT_FAILURE;
	}
	
	// Create, Bind and Listen on socket
	for(size_t i = 0; i < port_count; i++)
	{
		if(!load_banner_file(ports[i], &banner_pools[i]))
		{
			fprintf(stderr, "Failed to load banner for port: %" PRIu16 "\n", ports[i]);
			free_banner_pools(banner_pools, port_count);
			close_listeners(listeners, listener_count);
			close_log_files(log_files, port_count);
			return EXIT_FAILURE;
		}
		
		// Open log files
		char logfilename[64];
		if(!get_log_filename(ports[i], logfilename, sizeof logfilename))
		{
			fprintf(stderr, "Failed to get log filename for port: %" PRIu16 "\n", ports[i]);
			close_log_files(log_files, port_count);
			close_listeners(listeners, listener_count);
			free_banner_pools(banner_pools, port_count);
			return EXIT_FAILURE;
		}
		log_files[i] = fopen(logfilename, "a");
		if(log_files[i] == NULL)
		{
			perror("fopen");
			close_log_files(log_files, port_count);
			close_listeners(listeners, listener_count);
			free_banner_pools(banner_pools, port_count);
			return EXIT_FAILURE;
		}
		
		for(size_t f = 0; f < family_count; f++)
		{
			size_t listener_index = listener_count;
			
			listeners[listener_index].family = families[f];
			listeners[listener_index].port = ports[i];
			listeners[listener_index].port_index = i;
			listeners[listener_index].fd = create_socket(listeners[listener_index].family);
			listeners[listener_index].banners = &banner_pools[i];
			listeners[listener_index].log_file = log_files[i];
			
			if (listeners[listener_index].fd < 0)
			{
				close_log_files(log_files, port_count);
				close_listeners(listeners, listener_count);
				free_banner_pools(banner_pools, port_count);
				return EXIT_FAILURE;
			}
			
			if(!bind_socket(listeners[listener_index].fd, listeners[listener_index].port, listeners[listener_index].family))
			{
				close_log_files(log_files, port_count);
				close(listeners[listener_index].fd);
				free_banner_pools(banner_pools, port_count);
				close_listeners(listeners, listener_count);
				return EXIT_FAILURE;
			}
			
			if(!listen_socket(listeners[listener_index].fd))
			{
				close_log_files(log_files, port_count);
				close(listeners[listener_index].fd);
				free_banner_pools(banner_pools, port_count);
				close_listeners(listeners, listener_count);
				return EXIT_FAILURE;
			}
			
			pollfds[listener_index].fd = listeners[listener_index].fd;
			pollfds[listener_index].events = POLLIN;
			pollfds[listener_index].revents = 0;
			
			listener_count++;
			
			const char *family_name = listeners[listener_index].family == AF_INET ? "IPv4" : "IPv6";
			printf("Listening for noise on port: %hu (%s)\n", listeners[listener_index].port, family_name);
		}
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
			close_log_files(log_files, port_count);
			free_banner_pools(banner_pools, port_count);
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
				int cfd = accept_connection(listeners[i].fd, &peer_addr, &peer_addr_size);
				
				if(cfd < 0 && errno == EINTR && running == 0)
				{
					printf("\nShutdown signal caught, shutting down...\n");
					break;
				}
				else if(cfd < 0)
				{
					close_log_files(log_files, port_count);
					free_banner_pools(banner_pools, port_count);
					close_listeners(listeners, listener_count);
					return EXIT_FAILURE;
				}
				size_t port_index = listeners[i].port_index;
				connection_counts[port_index]++;

				enum connection_result result =
					handle_connection(
						cfd,
						&listeners[i],
						connection_counts[port_index],
						&peer_addr,
						peer_addr_size,
						records,
						&record_count);

				if(result == CONNECTION_SHUTDOWN)
				{
					break;
				}
				else if(result == CONNECTION_FATAL)
				{
					close_log_files(log_files, port_count);
					free_banner_pools(banner_pools, port_count);
					close_listeners(listeners, listener_count);
					return EXIT_FAILURE;
				}
			}
		}
	}
	
	close_listeners(listeners, listener_count);
	close_log_files(log_files, port_count);
	
	for(size_t i = 0; i < port_count; i++)
	{
		free_banner_pool(&banner_pools[i]);
		printf("Port %"PRIu16" connection attempts: %" PRIu64 "\n", ports[i], connection_counts[i]);
	}
	
	return EXIT_SUCCESS;
}
