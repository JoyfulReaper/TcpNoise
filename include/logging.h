#ifndef LOGGING_H
#define LOGGING_H

#include "network.h"
#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

bool get_log_filename(
	uint16_t port, 
	char *buffer, 
	size_t buffer_size);

bool close_log_files(
	FILE *log_files[],
	size_t count);

void print_payload(
    FILE *output,
    const char *payload,
    ssize_t length);

bool log_connection(
    FILE *file,
    const char *message,
    const char *payload,
    ssize_t length,
    enum receive_status status,
    const char *banner,
    bool banner_sent);

#endif
