#include "http.h"

#include <strings.h>

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
