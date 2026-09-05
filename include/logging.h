#ifndef LOGGING_H
#define LOGGING_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

void get_log_filename(uint16_t port, char *buffer, size_t buffer_size);

bool close_log_files(FILE *log_files[], size_t count);

#endif
