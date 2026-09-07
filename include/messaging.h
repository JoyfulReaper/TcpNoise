#ifndef MESSAGING_H
#define MESSAGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <nats/nats.h>

natsConnection *messaging_connect(const char *url);

void messaging_disconnect(natsConnection *connection);

bool messaging_publish_connection(
    natsConnection *connection,
    const char *sensor_name,
    uint64_t connection_number,
    uint16_t listen_port,
    int ip_version,
    const char *remote_address,
    uint16_t remote_port,
    uint64_t seen_count,
    const char *payload,
    size_t payload_len,
    const char *timestamp_utc);

#endif
