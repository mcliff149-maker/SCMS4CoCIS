#pragma once

#include <stddef.h>

void logger_init(void);
void log_event(const char* msg);
void log_print_recent(int max_items);
size_t logger_count(void);
/* Index zero is the oldest retained event. Returned storage is borrowed. */
const char *logger_get(size_t index);
void logger_set_sink(void (*sink)(const char *));
