#pragma once

#include <stdbool.h>

bool storage_init(void);
bool storage_available(void);
bool storage_append(const char *line);
void storage_print_logs(unsigned limit);
bool storage_schedule_load(unsigned *open, unsigned *close);
bool storage_schedule_save(unsigned open, unsigned close);
bool storage_nvs_available(void);
