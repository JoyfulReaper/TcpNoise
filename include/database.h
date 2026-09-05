#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <sqlite3.h>

bool database_open(const char *filename, sqlite3 **db);
bool database_close(sqlite3 *db);

#endif
