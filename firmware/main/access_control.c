#include "access_control.h"
#include "scms.h"
#include "occupancy.h"
#include "lighting.h"
#include "scheduler.h"
#include "logger.h"
#include "serial_menu.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static scms_user_t users[3];
static unsigned minute;
static uint64_t day, warned_day, soon_warned_day, energy[2], elapsed, on_minutes[2];
static uint32_t selected;
static bool authenticated, storage;
static bool (*schedule_sink)(unsigned, unsigned);
static void (*access_sink)(int, bool);

static scms_user_t *find_user(uint32_t id) {
  for (unsigned i = 0; i < 3; ++i)
    if (users[i].user_id == id) return &users[i];
  return NULL;
}

static void event(const char *text, uint32_t id, const char *result) {
  char buffer[192];
  snprintf(buffer, sizeof(buffer),
           "day=%" PRIu64 " %02u:%02u user=%" PRIu32 " %s result=%s building=%d rooms=%d,%d lights=%u,%u",
           day, minute / 60, minute % 60, id, text, result,
           occupancy_building(), occupancy_room1(), occupancy_room2(),
           lighting_room1_on() ? 1u : 0u, lighting_room2_on() ? 1u : 0u);
  log_event(buffer);
  puts(buffer);
}

static void update_lights(uint32_t id) {
  bool old1 = lighting_room1_on(), old2 = lighting_room2_on();
  lighting_update();
  if (old1 != lighting_room1_on() || old2 != lighting_room2_on())
    event("LIGHT_CHANGE", id, "UPDATED");
}

static void closing_warning(void) {
  unsigned close = schedule_close_minute();
  unsigned soon = close >= 5 ? close - 5 : 0;
  if (minute >= soon && minute < close && occupancy_building() > 0 && soon_warned_day != day) {
    soon_warned_day = day;
    event("CLOSING_SOON", 0, "OCCUPIED");
  }
  if (minute >= schedule_close_minute() && occupancy_building() > 0 && warned_day != day) {
    warned_day = day;
    event("CLOSING_WARNING", 0, "OCCUPIED");
  }
}

void access_init(void) {
  memset(users, 0, sizeof(users));
  users[0].user_id = 1001;
  users[1].user_id = 1002;
  users[2].user_id = 2001;
  for (unsigned i = 0; i < 3; ++i) users[i].is_authorized = true;
}

static void change_room_count(unsigned room, int delta) {
  if (room == 1) occupancy_set_room1(occupancy_room1() + delta);
  if (room == 2) occupancy_set_room2(occupancy_room2() + delta);
}

scms_access_result_t access_attempt(uint32_t id, scms_direction_t direction, int now) {
  scms_user_t *u = find_user(id);
  scms_access_result_t result;
  if (!u || !u->is_authorized) result = SCMS_DENY_UNKNOWN_ID;
  else if (direction == SCMS_DIR_ENTRY && u->is_inside) result = SCMS_DENY_ALREADY_INSIDE;
  else if (direction == SCMS_DIR_EXIT && !u->is_inside) result = SCMS_DENY_NOT_INSIDE;
  else if (!schedule_is_open(now)) result = SCMS_DENY_SCHEDULE_CLOSED;
  else if (direction != SCMS_DIR_ENTRY && direction != SCMS_DIR_EXIT) result = SCMS_DENY_NOT_INSIDE;
  else {
    result = SCMS_ACCESS_GRANTED;
    if (direction == SCMS_DIR_ENTRY) {
      u->is_inside = true;
      occupancy_on_entry();
    } else {
      change_room_count(u->room, -1);
      u->room = 0;
      u->is_inside = false;
      occupancy_on_exit();
    }
    update_lights(id);
  }
  event(direction == SCMS_DIR_ENTRY ? "ENTRY" : "EXIT", id, access_result_str(result));
  if (result == SCMS_ACCESS_GRANTED) closing_warning();
  if (access_sink) access_sink((int)direction, result == SCMS_ACCESS_GRANTED);
  return result;
}

const char *access_result_str(scms_access_result_t result) {
  switch (result) {
    case SCMS_ACCESS_GRANTED: return "GRANTED";
    case SCMS_DENY_UNKNOWN_ID: return "DENY_UNKNOWN_ID";
    case SCMS_DENY_ALREADY_INSIDE: return "DENY_ALREADY_INSIDE";
    case SCMS_DENY_NOT_INSIDE: return "DENY_NOT_INSIDE";
    case SCMS_DENY_SCHEDULE_CLOSED: return "DENY_SCHEDULE_CLOSED";
    default: return "INVALID";
  }
}

static void room_attempt(unsigned room, uint32_t id, bool enter) {
  scms_user_t *u = find_user(id);
  const char *result = "GRANTED";
  if (!u) result = "DENY_UNKNOWN_ID";
  else if (!u->is_inside) result = "DENY_NOT_INSIDE";
  else if (enter && u->room != 0) result = "DENY_ALREADY_IN_ROOM";
  else if (!enter && u->room != room) result = "DENY_NOT_IN_ROOM";
  else {
    u->room = enter ? room : 0;
    change_room_count(room, enter ? 1 : -1);
    update_lights(id);
  }
  char action[32];
  snprintf(action, sizeof(action), "ROOM%u_%s", room, enter ? "IN" : "OUT");
  event(action, id, result);
}

void scms_init(void) {
  access_init();
  occupancy_init();
  lighting_init();
  schedule_init();
  logger_init();
  minute = schedule_open_minute();
  day = 0;
  warned_day = soon_warned_day = UINT64_MAX;
  energy[0] = energy[1] = 0;
  elapsed = on_minutes[0] = on_minutes[1] = 0;
  selected = 1001;
  authenticated = storage = false;
  schedule_sink = NULL;
  access_sink = NULL;
}

void scms_set_storage(bool available) { storage = available; }
void scms_set_log_sink(void (*sink)(const char *)) { logger_set_sink(sink); }
void scms_set_schedule_sink(bool (*sink)(unsigned, unsigned)) { schedule_sink = sink; }
void scms_set_access_sink(void (*sink)(int, bool)) { access_sink = sink; }
bool scms_restore_schedule(unsigned open, unsigned close) { return schedule_set_window(open, close); }
uint32_t scms_selected_id(void) { return selected; }
bool scms_light(unsigned room) {
  return room == 1 ? lighting_room1_on() : room == 2 ? lighting_room2_on() : false;
}
unsigned scms_building(void) { return (unsigned)occupancy_building(); }
unsigned scms_room(unsigned room) {
  return room == 1 ? (unsigned)occupancy_room1() : room == 2 ? (unsigned)occupancy_room2() : 0;
}
unsigned scms_user_room(uint32_t id) { scms_user_t *u = find_user(id); return u ? u->room : 0; }
bool scms_user_inside(uint32_t id) { scms_user_t *u = find_user(id); return u && u->is_inside; }
bool scms_authenticated(void) { return authenticated; }
bool scms_storage_available(void) { return storage; }
unsigned scms_minute(void) { return minute; }
uint64_t scms_day(void) { return day; }
uint64_t scms_energy_watt_minutes(unsigned room) { return room >= 1 && room <= 2 ? energy[room - 1] : 0; }
uint64_t scms_elapsed_minutes(void) { return elapsed; }
uint64_t scms_light_on_minutes(unsigned room) { return room >= 1 && room <= 2 ? on_minutes[room - 1] : 0; }
uint64_t scms_saved_watt_minutes(unsigned room) {
  return room >= 1 && room <= 2 ? elapsed * 60 - energy[room - 1] : 0;
}

void scms_button(unsigned terminal, uint32_t id) {
  if (terminal < 2) access_attempt(id, (scms_direction_t)terminal, (int)minute);
  else if (terminal < 4) {
    unsigned room = terminal - 1;
    scms_user_t *u = find_user(id);
    room_attempt(room, id, !u || u->room != room);
  }
}

static bool number(const char *s, uint32_t max, uint32_t *out) {
  uint32_t n = 0;
  if (!s || !*s) return false;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9') return false;
    unsigned digit = (unsigned)(*s - '0');
    if (n > max / 10 || (n == max / 10 && digit > max % 10)) return false;
    n = n * 10 + digit;
  }
  *out = n;
  return true;
}

static bool clock_value(const char *s, unsigned *out) {
  if (strlen(s) != 5 || s[2] != ':' || s[0] < '0' || s[0] > '9' ||
      s[1] < '0' || s[1] > '9' || s[3] < '0' || s[3] > '9' ||
      s[4] < '0' || s[4] > '9') return false;
  unsigned h = (unsigned)(s[0] - '0') * 10 + (unsigned)(s[1] - '0');
  unsigned m = (unsigned)(s[3] - '0') * 10 + (unsigned)(s[4] - '0');
  if (h >= 24 || m >= 60) return false;
  *out = h * 60 + m;
  return true;
}

void scms_execute(const char *input) {
  char buffer[SERIAL_MENU_LINE_CAPACITY], *args[5];
  size_t length = 0;
  unsigned argc = 0;
  uint32_t n, id;
  unsigned open, close;
  if (!input) return;
  while (length < sizeof(buffer) && input[length]) ++length;
  if (length == sizeof(buffer)) goto invalid;
  memcpy(buffer, input, length + 1);
  char *p = buffer;
  while (*p) {
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) break;
    if (argc == 5) goto invalid;
    args[argc++] = p;
    while (*p && *p != ' ' && *p != '\t') ++p;
    if (*p) *p++ = '\0';
  }
  if (!argc) return;
  if (!strcmp(args[0], "help") && argc == 1) serial_menu_print_help();
  else if (!strcmp(args[0], "status") && argc == 1) {
    printf("day=%" PRIu64 " %02u:%02u schedule=%02u:%02u-%02u:%02u %s building=%u rooms=%u,%u lights=%u,%u "
           "elapsed_min=%" PRIu64 " on_min=%" PRIu64 ",%" PRIu64 " energy_Wh=%.2f,%.2f "
           "baseline_Wh=%.2f,%.2f saved_Wh=%.2f,%.2f storage=%s admin=%s selected=%" PRIu32 "\n",
           day, minute / 60, minute % 60, schedule_open_minute() / 60,
           schedule_open_minute() % 60, schedule_close_minute() / 60,
           schedule_close_minute() % 60, schedule_is_open((int)minute) ? "OPEN" : "CLOSED",
           scms_building(), scms_room(1), scms_room(2),
           scms_light(1) ? 1u : 0u, scms_light(2) ? 1u : 0u,
           elapsed, on_minutes[0], on_minutes[1],
           (double)energy[0] / 60.0, (double)energy[1] / 60.0,
           (double)elapsed, (double)elapsed,
           (double)scms_saved_watt_minutes(1) / 60.0, (double)scms_saved_watt_minutes(2) / 60.0,
           storage ? "available" : "RAM-only", authenticated ? "yes" : "no", selected);
  } else if ((!strcmp(args[0], "users") || !strcmp(args[0], "who")) && argc == 1) {
    for (unsigned i = 0; i < 3; ++i)
      if (!strcmp(args[0], "users") || users[i].is_inside)
        printf("user=%" PRIu32 " authorized=%u inside=%u room=%u\n", users[i].user_id,
               users[i].is_authorized ? 1u : 0u, users[i].is_inside ? 1u : 0u, users[i].room);
  } else if (!strcmp(args[0], "logs") && (argc == 1 || argc == 2)) {
    n = SCMS_MAX_LOGS;
    if (argc == 2 && (!number(args[1], SCMS_MAX_LOGS, &n) || !n)) goto invalid;
    log_print_recent((int)n);
  } else if ((!strcmp(args[0], "scan_entry") || !strcmp(args[0], "scan_exit")) &&
             argc == 2 && number(args[1], UINT32_MAX, &id)) {
    access_attempt(id, !strcmp(args[0], "scan_entry") ? SCMS_DIR_ENTRY : SCMS_DIR_EXIT, (int)minute);
  } else if (!strcmp(args[0], "room") && argc == 4 &&
             number(args[1], 2, &n) && n && number(args[2], UINT32_MAX, &id) &&
             (!strcmp(args[3], "in") || !strcmp(args[3], "out"))) {
    room_attempt(n, id, !strcmp(args[3], "in"));
  } else if (!strcmp(args[0], "select") && argc == 2 && number(args[1], UINT32_MAX, &id)) {
    selected = id;
    event("SELECT", id, "UPDATED");
  } else if (!strcmp(args[0], "button") && argc == 2 && number(args[1], 3, &n)) {
    scms_button(n, selected);
  } else if (!strcmp(args[0], "time") && argc == 2 && clock_value(args[1], &open)) {
    minute = open;
    event("CLOCK_SET", 0, "UPDATED_NO_ELAPSED_ENERGY");
    closing_warning();
  } else if (!strcmp(args[0], "advance") && argc == 2 && number(args[1], 10080, &n)) {
    /* Occupancy cannot change during a command: charge the old light state exactly once. */
    elapsed += n;
    if (scms_light(1)) { energy[0] += (uint64_t)n * 60; on_minutes[0] += n; }
    if (scms_light(2)) { energy[1] += (uint64_t)n * 60; on_minutes[1] += n; }
    for (uint32_t i = 0; i < n; ++i) {
      if (++minute == 1440) { minute = 0; ++day; }
      closing_warning();
    }
    event("CLOCK_ADVANCE", 0, "UPDATED");
  } else if (!strcmp(args[0], "login") && argc == 3) {
    authenticated = !strcmp(args[1], "admin") && !strcmp(args[2], "coursework");
    event("LOGIN", 0, authenticated ? "DEMO_ADMIN_GRANTED" : "DENIED");
  } else if (!strcmp(args[0], "logout") && argc == 1) {
    authenticated = false;
    event("LOGOUT", 0, "OK");
  } else if (!strcmp(args[0], "set_schedule") && argc == 3) {
    if (!authenticated) { event("SCHEDULE", 0, "DENY_AUTH"); return; }
    if (!clock_value(args[1], &open) || !clock_value(args[2], &close) || open >= close) {
      puts("Invalid schedule: require same-day open < close (no overnight/equal window).");
      event("SCHEDULE", 0, "INVALID");
      return;
    }
    if (schedule_sink && !schedule_sink(open, close)) {
      event("SCHEDULE", 0, "PERSIST_FAILED_UNCHANGED");
      return;
    }
    bool changed = open != schedule_open_minute() || close != schedule_close_minute();
    schedule_set_window(open, close);
    if (changed) warned_day = soon_warned_day = UINT64_MAX;
    event("SCHEDULE", 0, schedule_sink ? "SAVED" : "APPLIED_RAM_ONLY");
    closing_warning();
  } else if (!strcmp(args[0], "storage_logs") && argc == 1) {
    puts("Persistent log replay is provided by the platform; use logs for RAM history.");
  } else goto invalid;
  return;
invalid:
  /* Never echo untrusted input, particularly authentication credentials. */
  puts("Invalid command or arguments; type help.");
}
