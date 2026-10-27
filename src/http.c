#include "http.h"

#include <strings.h>
#include <string.h>
#include <stdint.h>

bool is_http1_request(const char *payload, size_t payload_len)
{
    if (payload == NULL)
        return false;

    size_t line_end = 0;

    while (line_end < payload_len && payload[line_end] != '\n')
        line_end++;

    if (line_end == payload_len)
        return false;

    if (line_end > 0 && payload[line_end - 1] == '\r')
        line_end--;

    const char version_prefix[] = " HTTP/1.";
    const size_t prefix_len = sizeof version_prefix - 1;

    if (line_end < prefix_len + 2)
        return false;

    size_t version_start = line_end - prefix_len - 1;

    if (memcmp(payload + version_start, version_prefix, prefix_len) != 0)
        return false;

    char minor_version = payload[line_end - 1];

    if (minor_version != '0' && minor_version != '1')
        return false;

    return memchr(payload, ' ', version_start) != NULL;
}

bool find_http_header_end(
    const char *payload,
    size_t payload_len,
    size_t *header_end)
{
    if (payload == NULL || header_end == NULL)
        return false;

    *header_end = 0;

    for (size_t i = 3; i < payload_len; i++)
    {
        if (payload[i - 3] == '\r' &&
            payload[i - 2] == '\n' &&
            payload[i - 1] == '\r' &&
            payload[i] == '\n')
        {
            *header_end = i + 1;
            return true;
        }
    }

    return false;
}

bool extract_http_user_agent(
	const char *payload,
	size_t payload_len,
	char *output,
	size_t output_size)
	{
		if (output == NULL || output_size == 0)
			return false;

		output[0] = '\0';

		if (payload == NULL)
			return false;

		const char header_name[] = "User-Agent:";
		const size_t header_len = sizeof(header_name) - 1;

		size_t pos = 0;

		while (pos < payload_len)
		{
			size_t line_start = pos;

			while(pos < payload_len && payload[pos] != '\n')
				pos++;

			if(pos == payload_len)
				break;

			size_t line_end = pos;
			pos++;

			if(line_end > line_start && payload[line_end -1] == '\r')
				line_end--;

			if(line_end == line_start)
				break;

			size_t line_len = line_end - line_start;

			if (line_len < header_len ||
				strncasecmp(payload + line_start, header_name, header_len) != 0)
			{
				continue;
			}

			size_t value_start = line_start + header_len;
			while(value_start < line_end &&
				(payload[value_start] == ' ' || payload[value_start] == '\t'))
				{
					value_start++;
				}

			while (line_end > value_start &&
				(payload[line_end -1] == ' ' || payload[line_end -1] == '\t'))
				{
					line_end--;
				}

			size_t value_len = line_end - value_start;

			if(value_len == 0)
				return false;

			if(value_len >= output_size)
				value_len = output_size - 1;

			for(size_t i = 0; i < value_len; i++)
			{
				unsigned char c = (unsigned char)payload[value_start + i];
				output[i] = (c >= 32 && c <= 126) ? (char)c : '?';
			}

			output[value_len] = '\0';
			return value_len > 0;
		}

		return false;
	}

bool http_request_complete(const char *payload, size_t payload_len)
{
    if (!is_http1_request(payload, payload_len))
        return false;

    size_t header_end = 0;

    if (!find_http_header_end(payload, payload_len, &header_end))
        return false;

    size_t content_length = 0;
	bool has_content_length = false;

	const char length_header[] = "Content-Length:";
	const char encoding_header[] = "Transfer-Encoding:";

	size_t pos = 0;

	while (pos < header_end)
	{
		size_t line_start = pos;

		while (pos < header_end && payload[pos] != '\n')
			pos++;

		size_t line_end = pos;

		if (pos < header_end)
			pos++;

		if (line_end > line_start && payload[line_end - 1] == '\r')
			line_end--;

		size_t line_len = line_end - line_start;

		// Don't stop early for chunked or other transfer encodings.
		if (line_len >= sizeof encoding_header - 1 &&
			strncasecmp(payload + line_start, encoding_header, sizeof encoding_header - 1) == 0)
			return false;

		if (line_len < sizeof length_header - 1 ||
			strncasecmp(payload + line_start, length_header, sizeof length_header - 1) != 0)
			continue;

		// Reject duplicate Content-Length headers conservatively.
		if (has_content_length)
			return false;

		size_t i = line_start + sizeof length_header - 1;

		while (i < line_end && (payload[i] == ' ' || payload[i] == '\t'))
			i++;

		if (i == line_end)
			return false;

		size_t value = 0;
		bool found_digit = false;

		while (i < line_end && payload[i] >= '0' && payload[i] <= '9')
		{
			size_t digit = (size_t)(payload[i] - '0');

			if (value > (SIZE_MAX - digit) / 10)
				return false;

			value = value * 10 + digit;
			found_digit = true;
			i++;
		}

		while (i < line_end && (payload[i] == ' ' || payload[i] == '\t'))
			i++;

		if (!found_digit || i != line_end)
			return false;

		content_length = value;
		has_content_length = true;
	}

	return !has_content_length || payload_len - header_end >= content_length;
}
