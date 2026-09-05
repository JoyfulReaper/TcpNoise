#include "logging.h"

#include <stdbool.h>
#include <stdio.h>

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
	else if(result >= (int)sizeof buffer)
	{
		fprintf(stderr, "buffer is too small\n");
		return false;
	}
	
	return true;
}
