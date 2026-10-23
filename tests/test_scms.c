#include "scms.h"
#include "access_control.h"
#include "scheduler.h"
#include "serial_menu.h"
#include "logger.h"
#include "scms_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
    exit(1); \
  } \
} while (0)

static unsigned submitted, access_calls, sink_calls, schedule_calls;
static int last_direction;
static bool last_granted, save_ok;
static unsigned saved_open, saved_close;
static char last_line[SERIAL_MENU_LINE_CAPACITY];

static void submit(const char *line) {
  ++submitted;
  snprintf(last_line, sizeof(last_line), "%s", line);
}
static void feed(const char *s) { while (*s) serial_menu_feed(*s++, submit); }
static void access_signal(int direction, bool granted) {
  ++access_calls;
  last_direction = direction;
  last_granted = granted;
}
static void log_sink(const char *line) {
  CHECK(line && *line);
  ++sink_calls;
}
static void failing_log_sink(const char *line) {
  CHECK(line && *line);
  scms_set_storage(false);
}
static bool save_schedule(unsigned open, unsigned close) {
  ++schedule_calls;
  saved_open = open;
  saved_close = close;
  return save_ok;
}
static unsigned occurrences(const char *text) {
  unsigned n = 0;
  for (size_t i = 0; i < logger_count(); ++i)
    if (strstr(logger_get(i), text)) ++n;
  return n;
}

static void test_access(void) {
  scms_init();
  scms_set_access_sink(access_signal);
  access_calls = 0;
  scms_execute("time 06:59");
  scms_execute("scan_entry 1001");
  CHECK(!last_granted && access_calls == 1 && last_direction == 0);
  CHECK(scms_building() == 0);
  scms_execute("time 07:00");
  scms_execute("scan_entry 9999");
  CHECK(!last_granted);
  scms_execute("scan_exit 1001");
  CHECK(!last_granted && last_direction == 1);
  scms_execute("scan_entry 1001");
  CHECK(last_granted && scms_user_inside(1001) && scms_building() == 1);
  scms_execute("scan_entry 1001");
  CHECK(!last_granted && scms_building() == 1);
  scms_execute("scan_entry 1002");
  CHECK(scms_building() == 2);
  scms_execute("time 20:00");
  scms_execute("status");
  scms_execute("scan_entry 2001");
  CHECK(!last_granted && scms_building() == 2);
  scms_execute("scan_exit 1001");
  CHECK(!last_granted && scms_building() == 2 && scms_user_inside(1001));
  scms_execute("time 19:59");
  scms_execute("scan_exit 1001");
  CHECK(last_granted && scms_building() == 1 && !scms_user_inside(1001));
  scms_execute("scan_exit 1001");
  CHECK(!last_granted && scms_building() == 1 && access_calls == 10);
  CHECK(occurrences("DENY_SCHEDULE_CLOSED") == 3);
  CHECK(occurrences("DENY_UNKNOWN_ID") == 1);
  CHECK(occurrences("DENY_ALREADY_INSIDE") == 1);
  CHECK(occurrences("DENY_NOT_INSIDE") == 2);
}

static void test_rooms_buttons(void) {
  scms_init();
  scms_execute("room 1 1001 in");
  CHECK(scms_room(1) == 0 && !scms_light(1));
  scms_execute("button 0");
  scms_execute("button 2");
  CHECK(scms_user_room(1001) == 1 && scms_room(1) == 1 && scms_light(1));
  scms_execute("room 1 1001 in");
  scms_execute("room 2 1001 in");
  scms_execute("room 2 1001 out");
  CHECK(scms_room(1) == 1 && scms_room(2) == 0 && scms_user_room(1001) == 1);
  scms_execute("select 1002");
  CHECK(scms_selected_id() == 1002);
  scms_execute("button 0");
  scms_execute("button 2");
  CHECK(scms_room(1) == 2);
  scms_execute("button 1");
  CHECK(scms_room(1) == 1 && scms_light(1) && scms_user_room(1002) == 0);
  scms_button(2, 1001);
  CHECK(scms_room(1) == 0 && !scms_light(1));
  scms_button(3, 1001);
  CHECK(scms_room(2) == 1 && scms_light(2));
  scms_button(1, 1001);
  CHECK(scms_building() == 0 && scms_room(2) == 0 && !scms_light(2));
  scms_button(4, 1001);
  scms_button(2, 9999);
  CHECK(scms_building() == 0);
  CHECK(!scms_light(0) && !scms_light(3) && scms_room(3) == 0);
}

static void test_schedule_auth(void) {
  scms_init();
  schedule_calls = 0;
  save_ok = true;
  scms_set_schedule_sink(save_schedule);
  scms_execute("set_schedule 08:30 17:45");
  CHECK(schedule_open_minute() == 420 && schedule_calls == 0);
  scms_execute("login admin wrong");
  CHECK(!scms_authenticated());
  scms_execute("set_schedule 08:30 17:45");
  CHECK(schedule_calls == 0);
  scms_execute("login admin coursework");
  CHECK(scms_authenticated());
  scms_execute("set_schedule 08:30 17:45");
  CHECK(schedule_calls == 1 && saved_open == 510 && saved_close == 1065);
  CHECK(schedule_open_minute() == 510 && schedule_close_minute() == 1065);
  CHECK(!schedule_is_open(509) && schedule_is_open(510));
  CHECK(schedule_is_open(1064) && !schedule_is_open(1065) && !schedule_is_open(-1));
  scms_execute("set_schedule 22:00 06:00");
  scms_execute("set_schedule 08:00 08:00");
  scms_execute("set_schedule 08:00 24:00");
  scms_execute("set_schedule 8:00 19:00");
  CHECK(schedule_calls == 1 && schedule_open_minute() == 510);
  save_ok = false;
  scms_set_storage(true);
  scms_execute("set_schedule 09:00 18:00");
  CHECK(schedule_calls == 2 && schedule_open_minute() == 510 && scms_storage_available());
  CHECK(occurrences("PERSIST_FAILED_UNCHANGED") == 1);
  scms_execute("logout");
  CHECK(!scms_authenticated());
  scms_execute("set_schedule 07:00 20:00");
  CHECK(schedule_calls == 2);
  CHECK(!scms_restore_schedule(600, 500));
  CHECK(!scms_restore_schedule(500, 1440));
  CHECK(!scms_restore_schedule(500, 500));
  CHECK(scms_restore_schedule(481, 1201));
  CHECK(schedule_open_minute() == 481);
  for (size_t i = 0; i < logger_count(); ++i) {
    CHECK(!strstr(logger_get(i), "coursework"));
    CHECK(!strstr(logger_get(i), "wrong"));
  }
  scms_set_schedule_sink(NULL);
  scms_execute("login admin coursework");
  scms_execute("set_schedule 08:00 18:00");
  CHECK(schedule_open_minute() == 480 && occurrences("APPLIED_RAM_ONLY") == 1);
}

static void test_energy_warnings(void) {
  scms_init();
  scms_execute("scan_entry 1001");
  scms_execute("room 1 1001 in");
  scms_execute("advance 60");
  CHECK(scms_minute() == 480 && scms_energy_watt_minutes(1) == 3600);
  CHECK(scms_elapsed_minutes() == 60 && scms_light_on_minutes(1) == 60);
  CHECK(scms_light_on_minutes(2) == 0 && scms_saved_watt_minutes(2) == 3600);
  CHECK(scms_saved_watt_minutes(1) == 0 && scms_saved_watt_minutes(0) == 0);
  CHECK(scms_energy_watt_minutes(2) == 0 && scms_energy_watt_minutes(0) == 0);
  scms_execute("time 19:59");
  CHECK(scms_energy_watt_minutes(1) == 3600);
  CHECK(scms_elapsed_minutes() == 60 && occurrences("CLOSING_SOON") == 1);
  scms_execute("advance 1");
  CHECK(occurrences("CLOSING_WARNING") == 1);
  CHECK(scms_energy_watt_minutes(1) == 3660);
  scms_execute("advance 1");
  scms_execute("time 21:00");
  scms_execute("time 19:00");
  scms_execute("advance 120");
  CHECK(occurrences("CLOSING_WARNING") == 1);
  CHECK(occurrences("CLOSING_SOON") == 1);
  CHECK(scms_energy_watt_minutes(1) == 10920);
  scms_execute("advance 1440");
  CHECK(scms_day() == 1 && scms_minute() == 1260);
  CHECK(occurrences("CLOSING_WARNING") == 2);
  CHECK(occurrences("CLOSING_SOON") == 2);
  scms_execute("time 19:00");
  scms_execute("scan_exit 1001");
  scms_execute("advance 120");
  CHECK(occurrences("CLOSING_WARNING") == 2);
  uint64_t energy = scms_energy_watt_minutes(1);
  scms_execute("advance 1440");
  CHECK(scms_energy_watt_minutes(1) == energy);
  CHECK(scms_saved_watt_minutes(1) > 0);
  CHECK(scms_light_on_minutes(1) * 60 == energy);
  CHECK(scms_saved_watt_minutes(1) + energy == scms_elapsed_minutes() * 60);

  scms_init();
  scms_execute("scan_entry 1001");
  scms_execute("room 1 1001 in");
  scms_execute("scan_entry 1002");
  scms_execute("room 2 1002 in");
  scms_execute("advance 10080");
  CHECK(scms_day() == 7 && scms_minute() == 420);
  CHECK(scms_energy_watt_minutes(1) == 604800);
  CHECK(scms_energy_watt_minutes(2) == 604800);
  CHECK(occurrences("CLOSING_WARNING") == 7);
  CHECK(occurrences("CLOSING_SOON") == 7);
  scms_init();
  scms_execute("scan_entry 1001");
  scms_execute("time 22:00");
  CHECK(occurrences("CLOSING_WARNING") == 1 && scms_energy_watt_minutes(1) == 0);
  scms_init();
  scms_execute("scan_entry 1001");
  scms_execute("time 19:54");
  CHECK(occurrences("CLOSING_SOON") == 0);
  scms_execute("advance 1");
  CHECK(occurrences("CLOSING_SOON") == 1 && occurrences("CLOSING_WARNING") == 0);
  scms_execute("time 19:55");
  scms_execute("advance 5");
  CHECK(occurrences("CLOSING_SOON") == 1 && occurrences("CLOSING_WARNING") == 1);
  scms_init();
  scms_execute("time 19:54");
  scms_execute("advance 10");
  CHECK(occurrences("CLOSING_SOON") == 0 && occurrences("CLOSING_WARNING") == 0);
  scms_init();
  scms_execute("time 19:55");
  CHECK(occurrences("CLOSING_SOON") == 0);
  scms_execute("scan_entry 1001");
  CHECK(occurrences("CLOSING_SOON") == 1);
  scms_execute("scan_entry 1002");
  CHECK(occurrences("CLOSING_SOON") == 1);
  scms_execute("login admin coursework");
  scms_execute("set_schedule 07:00 21:00");
  CHECK(occurrences("CLOSING_SOON") == 1);
  scms_execute("set_schedule 07:00 20:00");
  CHECK(occurrences("CLOSING_SOON") == 2);
  scms_execute("set_schedule 07:00 20:00");
  CHECK(occurrences("CLOSING_SOON") == 2);
  scms_execute("time 20:00");
  CHECK(occurrences("CLOSING_WARNING") == 1);
  scms_execute("set_schedule 07:00 20:00");
  CHECK(occurrences("CLOSING_WARNING") == 1);
  scms_execute("set_schedule 07:00 19:58");
  CHECK(occurrences("CLOSING_WARNING") == 2);
  scms_execute("set_schedule 07:00 19:58");
  CHECK(occurrences("CLOSING_WARNING") == 2);
  scms_set_schedule_sink(save_schedule);
  save_ok = false;
  scms_execute("set_schedule 07:00 19:57");
  scms_execute("advance 0");
  CHECK(occurrences("CLOSING_WARNING") == 2 && schedule_close_minute() == 1198);
}

static void test_logger(void) {
  logger_init();
  sink_calls = 0;
  logger_set_sink(log_sink);
  for (unsigned i = 0; i < SCMS_MAX_LOGS + 17; ++i) {
    char text[32];
    snprintf(text, sizeof(text), "event-%u", i);
    log_event(text);
  }
  CHECK(sink_calls == SCMS_MAX_LOGS + 17);
  CHECK(logger_count() == SCMS_MAX_LOGS);
  CHECK(!strcmp(logger_get(0), "event-17"));
  CHECK(!strcmp(logger_get(SCMS_MAX_LOGS - 1), "event-272"));
  CHECK(logger_get(SCMS_MAX_LOGS) == NULL);
  log_event(NULL);
  CHECK(sink_calls == SCMS_MAX_LOGS + 17);
  char large[500];
  memset(large, 'x', sizeof(large));
  large[499] = 0;
  log_event(large);
  CHECK(strlen(logger_get(SCMS_MAX_LOGS - 1)) == 191);
  logger_init();
  CHECK(logger_count() == 0 && logger_get(0) == NULL);
  log_event("reset");
  CHECK(sink_calls == SCMS_MAX_LOGS + 18);
}

static void test_framing(void) {
  serial_menu_init();
  submitted = 0;
  feed("\r\n\nhelp\r\nstatus\n");
  CHECK(submitted == 2 && !strcmp(last_line, "status"));
  for (unsigned i = 0; i < SERIAL_MENU_LINE_CAPACITY - 1; ++i) serial_menu_feed('x', submit);
  serial_menu_feed('\n', submit);
  CHECK(submitted == 3 && strlen(last_line) == SERIAL_MENU_LINE_CAPACITY - 1);
  for (unsigned i = 0; i < SERIAL_MENU_LINE_CAPACITY; ++i) serial_menu_feed('x', submit);
  feed("scan_entry 1001\r\nhelp\n");
  CHECK(submitted == 4 && !strcmp(last_line, "help"));
  feed("scan_entry ");
  serial_menu_feed('\0', submit);
  feed("1001\nstatus\n");
  CHECK(submitted == 5 && !strcmp(last_line, "status"));
  feed("bad");
  serial_menu_feed('\b', submit);
  feed("\n");
  CHECK(submitted == 5);
  feed("partial");
  serial_menu_init();
  feed("help\r");
  CHECK(submitted == 6 && !strcmp(last_line, "help"));
  serial_menu_feed('\n', submit);
  CHECK(submitted == 6);
  feed("room\t1\t1001\tin\n");
  CHECK(submitted == 7);
  serial_menu_feed('x', NULL);
  serial_menu_feed('\n', NULL);
}

static void test_invalid_and_reset(void) {
  static const char *bad[] = {
    "scan_entry -1", "scan_entry +1001", "scan_entry 4294967296",
    "scan_entry 1001junk", "scan_entry 1001 extra", "scan_entry",
    "time 24:00", "time 12:60", "time 1:00", "time 12:00 extra",
    "advance -1", "advance 10081", "advance 42949672960",
    "room 0 1001 in", "room 3 1001 in", "room 1 -1 in",
    "room 1 1001 yes", "button 4", "select -1",
    "set_schedule", "login admin coursework extra", "a b c d e f",
    "logs 0", "logs -1", "logs 257", "unknown"
  };
  scms_init();
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) scms_execute(bad[i]);
  scms_execute(NULL);
  scms_execute("");
  scms_execute(" \t ");
  char overlong[200];
  memset(overlong, 'x', sizeof(overlong));
  overlong[199] = 0;
  scms_execute(overlong);
  CHECK(scms_building() == 0 && scms_minute() == 420);
  CHECK(!scms_authenticated() && scms_selected_id() == 1001);
  CHECK(logger_count() == 0 && schedule_open_minute() == 420);
  scms_execute("scan_entry 4294967295");
  CHECK(scms_building() == 0 && occurrences("DENY_UNKNOWN_ID") == 1);
  scms_execute("help");
  scms_execute("status");
  scms_execute("users");
  scms_execute("who");
  scms_execute("logs 1");
  scms_execute("storage_logs");
  scms_set_storage(true);
  scms_set_log_sink(failing_log_sink);
  scms_execute("scan_entry 1001");
  CHECK(!scms_storage_available() && scms_building() == 1);
  scms_execute("room 1 1001 in");
  scms_execute("advance 10");
  scms_execute("login admin coursework");
  scms_set_access_sink(access_signal);
  scms_set_schedule_sink(save_schedule);
  scms_execute("select 2001");
  scms_init();
  CHECK(scms_building() == 0 && !scms_light(1) && !scms_light(2));
  CHECK(scms_room(1) == 0 && !scms_user_inside(1001) && scms_user_room(1001) == 0);
  CHECK(scms_energy_watt_minutes(1) == 0 && scms_day() == 0 && scms_minute() == 420);
  CHECK(scms_elapsed_minutes() == 0 && scms_light_on_minutes(1) == 0);
  CHECK(scms_saved_watt_minutes(2) == 0);
  CHECK(!scms_authenticated() && !scms_storage_available() && logger_count() == 0);
  CHECK(scms_selected_id() == 1001 && schedule_open_minute() == 420);
  unsigned previous = access_calls;
  scms_execute("scan_entry 1001");
  CHECK(access_calls == previous);
  scms_execute("login admin coursework");
  scms_execute("set_schedule 09:00 18:00");
  CHECK(schedule_open_minute() == 540);
  CHECK(occurrences("APPLIED_RAM_ONLY") == 1);
}

int main(void) {
  test_access();
  test_rooms_buttons();
  test_schedule_auth();
  test_energy_warnings();
  test_logger();
  test_framing();
  test_invalid_and_reset();
  puts("PASS: 7 deterministic production-model test groups");
  return 0;
}
