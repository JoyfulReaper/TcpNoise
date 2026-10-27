#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>

bool is_http1_request(const char *payload, size_t payload_len);

bool http_request_complete(const char *payload, size_t payload_len);

bool extract_http_user_agent(
	const char *payload,
	size_t playload_len,
	char *output,
	size_t output_size);

bool find_http_header_end(
    const char *payload,
    size_t payload_len,
    size_t *header_end);

#endif
