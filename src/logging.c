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

void get_log_filename(uint16_t port, char *buffer, size_t buffer_size)
{
	snprintf(buffer, buffer_size, "%d.log", port);
}
