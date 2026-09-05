#ifndef NETWORK_H
#define NETWORK_H

enum receive_status
{
    RECEIVE_DATA,
    RECEIVE_CLOSED,
    RECEIVE_TIMEOUT,
    RECEIVE_INTERRUPTED,
    RECEIVE_ERROR
};

#endif
