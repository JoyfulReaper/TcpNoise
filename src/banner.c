#include "banner.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <inttypes.h>

const char* choose_banner(const struct banner_pool *pool)
{
	if(pool == NULL || pool->count == 0)
		return NULL;
	
	int no_banner_chance = rand() % 4;
	if(no_banner_chance == 3)
		return NULL;
	
	size_t index = rand() % pool->count;
	return pool->lines[index];
}

bool send_banner(int cfd, const char *banner)
{
	char buffer[256];
	int res = snprintf(buffer, sizeof buffer, "%s\r\n", banner);
	if(res < 0)
	{
		fprintf(stderr, "Encoding error.\n");
		return false;
	}
	if(res >= (int)sizeof buffer)
	{
		fprintf(stderr, "Banner too large for buffer.\n");
		return false;
	}
	
	size_t total_bytes = (size_t)res;
	size_t bytes_sent = 0;
	
	while(bytes_sent < total_bytes)
	{
		ssize_t sent_now = send(cfd, buffer + bytes_sent, total_bytes - bytes_sent, 0);
		if(sent_now == -1)
		{
			if(errno == EINTR)
			{
				continue;
			}
			
			perror("send");
			return false;
		}
		else if(sent_now == 0)
		{
			fprintf(stderr, "Zero bytes sent\n");
			return false;
		}
		bytes_sent += (size_t)sent_now;
	}
	
	return true;
}

static bool get_banner_filename(uint16_t port, char *buffer, size_t buffer_size)
{
	int result = snprintf(buffer, buffer_size, "%" PRIu16 "_banner.txt", port);
	if (result < 0)
	{
		fprintf(stderr, "get_banner_filename: encoding error.\n");
		return false;
	}
	else if(result >= (int)buffer_size)
	{
		fprintf(stderr, "get_banner_filename: buffer is too small.\n");
		return false;
	}
	
	return true;
}

bool load_banner_file(uint16_t port, struct banner_pool *pool)
{
	char filename[32];
	if(!get_banner_filename(port, filename, sizeof filename))
	{
		return false;
	}
	
	FILE *banner_file;
	banner_file = fopen(filename, "r");
	if(banner_file == NULL && errno != ENOENT)
	{
		perror("fopen");
		return false;
	} else if (banner_file == NULL) {
		return true;
	}
	
	char *line = NULL;
	size_t capacity = 0;
	
	while(getline(&line, &capacity, banner_file) != -1)
	{
		line[strcspn(line, "\r\n")] = '\0';

		char **new_lines = realloc(pool->lines, (pool->count + 1) * sizeof *pool->lines);
		if(new_lines == NULL)
		{
			free(line);
			if(fclose(banner_file) != 0)
			{
				perror("fclose");
				return false;
			}
			fprintf(stderr, "realloc failed.\n");
			return false;
		}
		pool->lines = new_lines;
		char *dup = strdup(line);
		if(dup == NULL)
		{
			perror("strdup");
			free(line);
			fclose(banner_file);
			return false;
		}
		pool->lines[pool->count] = dup;
		pool->count++;
	}

	if(ferror(banner_file))
	{
		perror("getline");
		free(line);
		fclose(banner_file);
		return false;
	}
	
	free(line);
	fclose(banner_file);
	return true;
}

void free_banner_pool(struct banner_pool *pool)
{
	if(pool == NULL)
		return;
		
	for(size_t i = 0; i < pool->count; i++)
	{
		free(pool->lines[i]);
	}
	free(pool->lines);
	
	pool->lines = NULL;
	pool->count = 0;
}

void free_banner_pools(struct banner_pool banner_pools[], size_t pool_count)
{
	for(size_t i = 0; i < pool_count; i++)
	{
		free_banner_pool(&banner_pools[i]);
	}
}
