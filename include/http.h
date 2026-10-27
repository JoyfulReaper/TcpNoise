#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>

bool extract_http_user_agent(
	const char *payload,
	size_t playload_len,
	char *output,
	size_t output_size);

#endif
