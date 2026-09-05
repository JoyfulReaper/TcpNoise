#include "database.h"

#include <stdbool.h>
#include <sqlite3.h>
#include <stdio.h>

bool database_open(const char *filename, sqlite3 **db)
{
	if(filename == NULL || db == NULL)
	{
		return false;
	}
	
	int result = sqlite3_open(filename, db);
	if(result != SQLITE_OK)
	{
		if(*db != NULL)
		{
			const char *error_message = sqlite3_errmsg(*db);
			fprintf(stderr, "sqlite3_open: %s\n", error_message);
			
			int close_result = sqlite3_close(*db);
			if(close_result != SQLITE_OK)
			{
				fprintf(
					stderr,
					"sqlite3_close after failed open: %s\n",
					sqlite3_errstr(close_result));
					
				return false;
			}
			*db = NULL;
		}
		
		return false;
	}
	
	return true;
}

bool database_close(sqlite3 *db)
{
	if(db == NULL)
		return false;
	
	int result = sqlite3_close(db);
	if(result != SQLITE_OK)
	{
		const char *error_message = sqlite3_errmsg(db);
		fprintf(stderr, "sqlite3_close: %s\n", error_message);
		return false;
	}
	return true;
}
