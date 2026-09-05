#ifndef BANNER_H
#define BANNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct banner_pool
{
	char **lines;
	size_t count;
};

bool load_banner_file(uint16_t port, struct banner_pool *pool);
void free_banner_pool(struct banner_pool *pool);
void free_banner_pools(struct banner_pool banner_pools[], size_t pool_count);
const char* choose_banner(const struct banner_pool *pool);
bool send_banner(int cfd, const char *banner);

#endif
