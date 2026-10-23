#pragma once

#include <stdbool.h>

void schedule_init(void);
bool schedule_is_open(int hour_of_day);
void schedule_set_window(int open_hour, int close_hour);
int schedule_open_hour(void);
int schedule_close_hour(void);
