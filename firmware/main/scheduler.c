#include "scheduler.h"
#include "scms_config.h"

static int g_open = DEFAULT_OPEN_HOUR;
static int g_close = DEFAULT_CLOSE_HOUR;

void schedule_init(void) {
  g_open = DEFAULT_OPEN_HOUR;
  g_close = DEFAULT_CLOSE_HOUR;
}

bool schedule_is_open(int hour_of_day) {
  return hour_of_day >= g_open && hour_of_day < g_close;
}

void schedule_set_window(int open_hour, int close_hour) {
  if (open_hour < 0) open_hour = 0;
  if (open_hour > 23) open_hour = 23;
  if (close_hour < 1) close_hour = 1;
  if (close_hour > 24) close_hour = 24;
  if (open_hour >= close_hour) return;

  g_open = open_hour;
  g_close = close_hour;
}

int schedule_open_hour(void) { return g_open; }
int schedule_close_hour(void) { return g_close; }
