#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

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

#endif
