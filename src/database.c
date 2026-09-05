#include "database.h"

#include <stdbool.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdint.h>

bool database_initialize(sqlite3 *db)
{
	if(db == NULL)
		return false;
		
	const char *sql =
		"CREATE TABLE IF NOT EXISTS seen_ip ("
		"address TEXT PRIMARY KEY,"
		"first_seen_utc TEXT NOT NULL,"
		"last_seen_utc TEXT NOT NULL,"
		"seen_count INTEGER NOT NULL"
		");";
		
	char *error_message = NULL;
	int result = sqlite3_exec(
		db,
		sql,
		NULL,
		NULL,
		&error_message);
		
	if(result != SQLITE_OK)
	{
		fprintf(stderr, "sqlite3_exec: %s\n",
			error_message != NULL ? error_message : "unknown error");
			
		sqlite3_free(error_message);
		return false;
	}
	
	return true;
}

bool database_record_seen_ip(
	sqlite3 *db,
	const char *address,
	const char *timestamp,
	uint64_t *seen_count)
{
	if(db == NULL ||
	   address == NULL ||
	   timestamp == NULL ||
	   seen_count == NULL)
	{
		return false;
	}
	
	const char *sql =
		"INSERT INTO seen_ip ("
		"address, first_seen_utc, last_seen_utc, seen_count"
		") VALUES (?, ?, ?, 1) "
		"ON CONFLICT(address) DO UPDATE SET "
		"last_seen_utc = excluded.last_seen_utc, "
		"seen_count = seen_count + 1 "
		"RETURNING seen_count;";
	
	sqlite3_stmt *statement = NULL;
	
	int result = sqlite3_prepare_v2(db, sql, -1, &statement, NULL);
	if(result != SQLITE_OK)
	{
		fprintf(stderr, "sqlite3_prepare_v2: %s\n", sqlite3_errmsg(db));
		return false;
	}
	
	result = sqlite3_bind_text(statement, 1, address, -1, SQLITE_TRANSIENT);
	if(result != SQLITE_OK)
	{
		fprintf(stderr, "sqlite3_bind_text address: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(statement);
		return false;
	}
	
	result = sqlite3_bind_text(statement, 2, timestamp, -1, SQLITE_TRANSIENT);
	if(result != SQLITE_OK)
	{
		fprintf(stderr, "sqlite3_bind_text first_seen_utc: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(statement);
		return false;
	}
	
	result = sqlite3_bind_text(statement, 3, timestamp, -1, SQLITE_TRANSIENT);
	if(result != SQLITE_OK)
	{
		fprintf(stderr, "sqlite3_bind_text last_seen_utc: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(statement);
		return false;
	}
	
	result = sqlite3_step(statement);
	if(result != SQLITE_ROW)
	{
		fprintf(stderr, "sqlite3_step: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(statement);
		return false;
	}
	
	sqlite3_int64 value = sqlite3_column_int64(statement, 0);
	if(value < 0)
	{
		fprintf(stderr, "Invalid negative seen count from database.\n");
		sqlite3_finalize(statement);
		return false;
	}
	
	*seen_count = (uint64_t)value;
	
	sqlite3_finalize(statement);
	return true;
}

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
