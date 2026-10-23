#pragma once

#include <stdbool.h>

void schedule_init(void);
bool schedule_is_open(int minute_of_day);
bool schedule_set_window(unsigned open_minute, unsigned close_minute);
unsigned schedule_open_minute(void);
unsigned schedule_close_minute(void);
