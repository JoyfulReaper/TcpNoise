#include "logging.h"

#include <stdbool.h>
#include <stdio.h>

#define ENABLE_LOGGING

bool close_log_files(FILE *log_files[], size_t count)
{
	bool success = true;
	for(size_t i = 0; i < count; i++)
	{
		if(log_files[i] != NULL)
		{
			if(fclose(log_files[i]) != 0)
			{
				perror("fclose");
				success = false;
			}
			log_files[i] = NULL;
		}
	}
	
	return success;
}

bool get_log_filename(uint16_t port, char *buffer, size_t buffer_size)
{
	int result = snprintf(buffer, buffer_size, "%d.log", port);
	if(result < 0)
	{
		fprintf(stderr, "Encoding error\n");
		return false;
	}
	else if(result >= (int)buffer_size)
	{
		fprintf(stderr, "buffer is too small\n");
		return false;
	}
	
	return true;
}

void print_payload(FILE *output, const char *payload, ssize_t length)
{
	for (ssize_t i = 0; i < length; i++)
	{
		unsigned char c = (unsigned char)payload[i];

		if (c >= 32 && c <= 126)
		{
			fputc(c, output);
		}
		else if (c == '\n')
		{
			fprintf(output, "\\n");
		}
		else if (c == '\r')
		{
			fprintf(output, "\\r");
		}
		else if (c == '\t')
		{
			fprintf(output, "\\t");
		}
		else
		{
			fprintf(output, "\\x%02x", c);
		}
	}

	fputc('\n', output);
}

// TODO: always check return value of fprintf for failure
bool log_connection (
	FILE *file, 
	const char *message,
	const char *payload,
	ssize_t length,
	enum receive_status status,
	const char *banner,
	bool banner_sent)
{
	#ifndef ENABLE_LOGGING
		return true;
	#endif
	
	if(fprintf(file, "%s\n", message) < 0)
	{
		fprintf(stderr, "Failed to write to log file.\n");
		return false;
	}
	
	if(banner == NULL)
	{
		fprintf(file, "- banner: <none>\n");
	}
	else if(banner_sent)
	{
		fprintf(file, "- banner: %s\n", banner);
	}
	else
	{
		fprintf(file, "- banner: <send failed> %s\n", banner);
	}
	
	if(status == RECEIVE_DATA)
	{
		fprintf(file, "- Payload: ");
		print_payload(file, payload, length);
	}
	else if(status == RECEIVE_CLOSED)
	{
		fprintf(file, "- Payload: <peer closed>\n");
	}
	else if(status == RECEIVE_TIMEOUT)
	{
		fprintf(file, "- Payload: <timeout>\n");
	}
	else if(status == RECEIVE_INTERRUPTED)
	{
		fprintf(file, "- Payload: <interrupted>\n");
	}
	else
	{
		fprintf(file, "- Payload: <receive error>\n");
	}
	
	if(fflush(file) != 0)
	{
		perror("fflush");
		return false;
	}
	
	return true;
}
