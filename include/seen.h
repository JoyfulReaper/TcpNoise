#ifndef SEEN_H
#define SEEN_H

#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>

#define MAX_SEEN_IPS 1000

struct seen_ip
{
	char ip[INET6_ADDRSTRLEN];
    uint64_t count;
};

int find_seen_ip(
	struct seen_ip records[],
	size_t record_count,
	const char *ip);
	
uint64_t increment_seen_ip(
	struct seen_ip records[],
	size_t *record_count,
	const char *ip);

#endif
