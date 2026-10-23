#pragma once

void logger_init(void);
void log_event(const char* msg);
void log_print_recent(int max_items);
