#include "scms.h"
#include <stdio.h>
#include <string.h>

void statistics_tick(app_t *app) {
  if (!app->time_valid) {
    app->last_minute = -1;
    return;
  }
  time_t local = app->utc + 3 * 3600;
  struct tm tm;
  gmtime_r(&local, &tm);
  app->last_minute = tm.tm_hour * 60 + tm.tm_min;
  int day = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
  if (day != app->saved.day) {
    if (app->saved.day != 0) {
      char detail[100];
      snprintf(detail, sizeof(detail), "Day=%d entries=%u peak=%u unique_mask=%u",
               app->saved.day, app->saved.entries, app->saved.peak, app->saved.unique);
      record_event(app, "DAY_SUMMARY", detail);
    }
    app->saved.day = day;
    app->saved.entries = 0;
    app->saved.unique = 0;
    app->saved.peak = app->saved.building;
    memset(app->saved.hours, 0, sizeof(app->saved.hours));
    app->dirty = true;
  }
}

void statistics_entry(app_t *app, int user) {
  if (!app->time_valid) {
    record_event(app, "STATISTICS_GAP", "Entry without valid calendar time");
    return;
  }
  statistics_tick(app);
  app->saved.entries++;
  app->saved.unique |= 1u << user;
  app->saved.hours[app->last_minute / 60]++;
  if ((unsigned int)app->saved.building > app->saved.peak) {
    app->saved.peak = app->saved.building;
  }
  app->dirty = true;
}
