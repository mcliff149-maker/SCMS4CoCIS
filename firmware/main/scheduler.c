#include "scheduler.h"
#include "scms_config.h"

static unsigned g_open;
static unsigned g_close;

void schedule_init(void) {
  g_open = DEFAULT_OPEN_HOUR * 60;
  g_close = DEFAULT_CLOSE_HOUR * 60;
}

bool schedule_is_open(int minute) {
  return minute >= 0 && (unsigned)minute >= g_open && (unsigned)minute < g_close;
}

bool schedule_set_window(unsigned open, unsigned close) {
  if (open >= close || close >= 1440) return false;
  g_open = open;
  g_close = close;
  return true;
}

unsigned schedule_open_minute(void) { return g_open; }
unsigned schedule_close_minute(void) { return g_close; }
