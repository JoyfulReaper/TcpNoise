#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <sqlite3.h>
#include <stdint.h>

bool database_initialize(sqlite3 *db);

bool database_open(const char *filename, sqlite3 **db);

bool database_close(sqlite3 *db);

bool database_record_seen_ip(
	sqlite3 *db,
	const char *address,
	const char *timestamp,
	uint64_t *seen_count);
	
#endif
