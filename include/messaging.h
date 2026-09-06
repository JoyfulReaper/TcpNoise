#ifndef MESSAGING_H
#define MESSAGING_H

#include <nats/nats.h>

natsConnection *messaging_connect(const char *url);
void messaging_disconnect(natsConnection *connection);

#endif
