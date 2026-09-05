#include "seen.h"

#include <stdio.h>
#include <string.h>

int find_seen_ip(
	struct seen_ip records[],
	size_t record_count,
	const char *ip)
{
	for(size_t i = 0; i < record_count; i++)
	{
		if(strcmp(ip, records[i].ip) == 0)
			return (int)i;
	}
	
	return -1;
}

enum seen_result increment_seen_ip(
	struct seen_ip records[],
	size_t *record_count,
	const char *ip,
	uint64_t *seen_count)
{
	int index = find_seen_ip(records, *record_count, ip);
	if(index < 0) // We haven't see this IP address before
	{
		if(*record_count >= MAX_SEEN_IPS)
		{
			return SEEN_FULL;
		}
		
		int result = snprintf(records[*record_count].ip, sizeof records[*record_count].ip, "%s", ip);
		if(result < 0)
		{
			fprintf(stderr, "Encoding error\n");
			return SEEN_ERROR;
		} else if (result >= (int)sizeof records[*record_count].ip)
		{
			fprintf(stderr, "Buffer too small.\n");
			return SEEN_ERROR;
		}
		
		records[*record_count].count = 1;
		(*record_count)++;
		*seen_count = 1;
		return SEEN_NEW;
	}
		
	records[index].count++;
	*seen_count = records[index].count;
	return SEEN_EXISTING;
}
