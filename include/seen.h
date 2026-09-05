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

enum seen_result
{
    SEEN_EXISTING,
    SEEN_NEW,
    SEEN_FULL,
    SEEN_ERROR
};

int find_seen_ip(
	struct seen_ip records[],
	size_t record_count,
	const char *ip);
	
enum seen_result increment_seen_ip(
	struct seen_ip records[],
	size_t *record_count,
	const char *ip,
	uint64_t *seen_count);

#endif
