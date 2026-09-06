#include <stdio.h>

#include "messaging.h"

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
