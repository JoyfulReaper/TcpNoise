#include "messaging.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

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

bool messaging_publish_connection(
    natsConnection *connection,
    uint64_t connection_number,
    uint16_t listen_port,
    int ip_version,
    const char *remote_address,
    uint16_t remote_port,
    uint64_t seen_count)
{
	if(connection == NULL)
		return false;
	
	char json[256];
    int result = snprintf(
        json,
        sizeof json,
        "{"
        "\"connectionNumber\":%" PRIu64 ","
        "\"listenPort\":%" PRIu16 ","
        "\"ipVersion\":%d,"
        "\"remoteAddress\":\"%s\","
        "\"remotePort\":%" PRIu16 ","
        "\"seenCount\":%" PRIu64
        "}",
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
		fprintf(stderr, "NATS publish: %s\n", natsStatus_GetText(status));
		return false;
	}
	
	return true;
}
