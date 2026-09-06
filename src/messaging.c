#include "messaging.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define ENABLE_NATS

natsConnection *messaging_connect(const char *url)
{
	natsConnection *connection = NULL;

	natsStatus status = natsConnection_ConnectTo(&connection, url);
	if(status != NATS_OK)
	{
		fprintf(stderr, "NATS: %s\n", natsStatus_GetText(status));
		return NULL;
	}

	return connection;
}

void messaging_disconnect(natsConnection *connection)
{
	if(connection == NULL)
		return;

	natsConnection_Destroy(connection);
	nats_Close();
}

static bool generate_event_id(char *buffer, size_t buffer_size)
{
    if(buffer_size < 37)
        return false;

    unsigned char bytes[16];
    arc4random_buf(bytes, sizeof bytes);

    // UUID version 4
    bytes[6] = (bytes[6] & 0x0f) | 0x40;

    // RFC 4122 variant
    bytes[8] = (bytes[8] & 0x3f) | 0x80;

    int result = snprintf(
        buffer,
        buffer_size,
        "%02x%02x%02x%02x-"
        "%02x%02x-"
        "%02x%02x-"
        "%02x%02x-"
        "%02x%02x%02x%02x%02x%02x",
        (unsigned)bytes[0],
        (unsigned)bytes[1],
        (unsigned)bytes[2],
        (unsigned)bytes[3],
        (unsigned)bytes[4],
        (unsigned)bytes[5],
        (unsigned)bytes[6],
        (unsigned)bytes[7],
        (unsigned)bytes[8],
        (unsigned)bytes[9],
        (unsigned)bytes[10],
        (unsigned)bytes[11],
        (unsigned)bytes[12],
        (unsigned)bytes[13],
        (unsigned)bytes[14],
        (unsigned)bytes[15]);

    return result == 36;
}

static bool format_iso8601_timestamp(
    const char *timestamp_utc,
    char *buffer,
    size_t buffer_size)
{
    if(timestamp_utc == NULL)
        return false;

    if(strlen(timestamp_utc) != 19 || timestamp_utc[10] != ' ')
        return false;

    int result = snprintf(
        buffer,
        buffer_size,
        "%.10sT%.8sZ",
        timestamp_utc,
        timestamp_utc + 11);

    if(result < 0)
        return false;

    return (size_t)result < buffer_size;
}

bool messaging_publish_connection(
    natsConnection *connection,
    uint64_t connection_number,
    uint16_t listen_port,
    int ip_version,
    const char *remote_address,
    uint16_t remote_port,
    uint64_t seen_count,
    const char *timestamp_utc)
{
	#ifndef ENABLE_NATS
		return true;
	#endif
	
    if(connection == NULL)
        return false;

    char event_id[37];

    if(!generate_event_id(event_id, sizeof event_id))
    {
        fprintf(stderr, "Failed to generate NATS event ID\n");
        return false;
    }

    char occurred_at[32];

    if(!format_iso8601_timestamp(
        timestamp_utc,
        occurred_at,
        sizeof occurred_at))
    {
        fprintf(stderr, "Failed to encode NATS event timestamp\n");
        return false;
    }

    char json[1024];

    int result = snprintf(
        json,
        sizeof json,
        "{"
        "\"EventId\":\"%s\","
        "\"EventType\":\"tcpnoise.connection\","
        "\"Source\":\"tcpnoise\","
        "\"SchemaVersion\":1,"
        "\"OccurredAt\":\"%s\","
        "\"ReceivedAt\":\"%s\","
        "\"CorrelationId\":null,"
        "\"CausationId\":null,"
        "\"Payload\":{"
            "\"connectionNumber\":%" PRIu64 ","
            "\"listenPort\":%" PRIu16 ","
            "\"ipVersion\":%d,"
            "\"remoteAddress\":\"%s\","
            "\"remotePort\":%" PRIu16 ","
            "\"seenCount\":%" PRIu64
        "}"
        "}",
        event_id,
        occurred_at,
        occurred_at,
        connection_number,
        listen_port,
        ip_version,
        remote_address,
        remote_port,
        seen_count);

    if(result < 0)
    {
        fprintf(stderr, "Failed to encode NATS connection event\n");
        return false;
    }

    if((size_t)result >= sizeof json)
    {
        fprintf(stderr, "NATS connection event is too large\n");
        return false;
    }

    natsStatus status = natsConnection_PublishString(
        connection,
        "tcpnoise.connection",
        json);

    if(status != NATS_OK)
    {
        fprintf(
            stderr,
            "NATS publish: %s\n",
            natsStatus_GetText(status));

        return false;
    }

    return true;
}
