#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <stddef.h>
#include <sys/types.h>

enum receive_status
{
    RECEIVE_DATA,
    RECEIVE_CLOSED,
    RECEIVE_TIMEOUT,
    RECEIVE_INTERRUPTED,
    RECEIVE_ERROR
};

int create_socket(int family);

bool bind_socket(
    int sfd,
    uint16_t port,
    int family);

bool listen_socket(int sfd);

int accept_connection(
    int sfd,
    struct sockaddr_storage *peer_addr,
    socklen_t *peer_addr_size);

bool set_receive_timeout(int cfd);

enum receive_status receive_payload(
    int cfd,
    char *buffer,
    size_t buffer_size,
    ssize_t *bytes_received);

bool resolve_remote(
    const struct sockaddr_storage *peer_addr,
    socklen_t peer_addr_size,
    char *ip,
    size_t ip_size,
    uint16_t *remote_port);

void format_remote_endpoint(
    char *buffer,
    size_t buffer_size,
    int family,
    const char *ip,
    uint16_t port);

#endif
