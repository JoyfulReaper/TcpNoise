#include "messaging.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define ENABLE_NATS
// #define ENABLE_MISSIONCONTROL

#define LIVE_CONNECTION_SUBJECT "tcpnoise.connection"
#define MISSION_CONTROL_CONNECTION_SUBJECT "events.tcpnoise.connection"

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

static char *escape_json_bytes(
    const char *data,
    size_t length)
{
    static const char hex[] = "0123456789ABCDEF";

    /*
     * Worst case is a binary byte becoming:
     *
     * \\xFF
     *
     * which requires five output characters.
     */
    if(length > (SIZE_MAX - 1) / 5)
        return NULL;

    size_t capacity = length * 5 + 1;

    char *output = malloc(capacity);
    if(output == NULL)
        return NULL;

    size_t j = 0;

    for(size_t i = 0; i < length; i++)
    {
        unsigned char c = (unsigned char)data[i];

        switch(c)
        {
            case '"':
                output[j++] = '\\';
                output[j++] = '"';
                break;

            case '\\':
                output[j++] = '\\';
                output[j++] = '\\';
                break;

            case '\b':
                output[j++] = '\\';
                output[j++] = 'b';
                break;

            case '\f':
                output[j++] = '\\';
                output[j++] = 'f';
                break;

            case '\n':
                output[j++] = '\\';
                output[j++] = 'n';
                break;

            case '\r':
                output[j++] = '\\';
                output[j++] = 'r';
                break;

            case '\t':
                output[j++] = '\\';
                output[j++] = 't';
                break;

            default:
                if(c >= 0x20 && c <= 0x7e)
                {
                    output[j++] = (char)c;
                }
                else
                {
                    /*
                     * Put a literal \xNN into the resulting JSON string.
                     * The first two backslashes become one after JSON
                     * decoding.
                     */
                    output[j++] = '\\';
                    output[j++] = '\\';
                    output[j++] = 'x';
                    output[j++] = hex[c >> 4];
                    output[j++] = hex[c & 0x0f];
                }
                break;
        }
    }

    output[j] = '\0';

    return output;
}

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
    
    char *escaped_payload = escape_json_bytes(payload, payload_len);
    if(escaped_payload == NULL)
    {
		fprintf(stderr, "Failed to encode NATS payload\n");
		return false;
	}

	char *escaped_sensor = escape_json_bytes(sensor_name, strlen(sensor_name));
	if(escaped_sensor == NULL)
	{
		fprintf(stderr, "Failed to encode sensor name\n");
		free(escaped_payload);
		return false;
	}

    size_t json_size =
		strlen(escaped_payload) +
		strlen(escaped_sensor) +
		2048;
		
	char *json = malloc(json_size);
	if(json == NULL)
	{
		fprintf(stderr, "Failed to allocate NATS event\n");
		free(escaped_payload);
		free(escaped_sensor);
		
		return false;
	}

	int result = snprintf(
		json,
		json_size,
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
			"\"sensor\":\"%s\","
			"\"connectionNumber\":%" PRIu64 ","
			"\"listenPort\":%" PRIu16 ","
			"\"ipVersion\":%d,"
			"\"remoteAddress\":\"%s\","
			"\"remotePort\":%" PRIu16 ","
			"\"seenCount\":%" PRIu64 ","
			"\"payloadLength\":%zu,"
			"\"payload\":\"%s\""
		"}"
		"}",
		event_id,
		occurred_at,
		occurred_at,
		escaped_sensor,
		connection_number,
		listen_port,
		ip_version,
		remote_address,
		remote_port,
		seen_count,
		payload_len,
		escaped_payload);

	free(escaped_payload);
	free(escaped_sensor);

    if(result < 0)
    {
        fprintf(stderr, "Failed to encode NATS connection event\n");
        free(json);
        
        return false;
    }

    if((size_t)result >= json_size)
    {
        fprintf(stderr, "NATS connection event is too large\n");
        free(json);
        
        return false;
    }

	bool success = true;

	natsStatus status = natsConnection_PublishString(
		connection,
		LIVE_CONNECTION_SUBJECT,
		json);

	if(status != NATS_OK)
	{
		fprintf(
			stderr,
			"Live NATS publish: %s\n",
			natsStatus_GetText(status));

		success = false;
	}

#ifdef ENABLE_MISSIONCONTROL
	status = natsConnection_PublishString(
		connection,
		MISSION_CONTROL_CONNECTION_SUBJECT,
		json);

	if(status != NATS_OK)
	{
		fprintf(
			stderr,
			"Mission Control NATS publish: %s\n",
			natsStatus_GetText(status));

		success = false;
	}
#endif

	free(json);

	return success;
}
