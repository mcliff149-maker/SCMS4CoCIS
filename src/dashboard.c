#include "scms.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "driver/uart.h"

static int page;
static int admin_item;
static int mode;
static int edit_value;
static int opening;
static int closing;
static unsigned int log_index;
static int64_t select_since = -1;
static int64_t next_display;
static bool previous_next;
static bool previous_select;
static bool held_back;
static char serial_line[128];
static size_t serial_length;
static bool serial_overflow;

static unsigned int unique_count(unsigned int bits) {
  unsigned int count = 0;
  while (bits != 0) {
    count += bits & 1;
    bits >>= 1;
  }
  return count;
}

static int busiest(const app_t *app) {
  int hour = 0;
  for (int i = 1; i < 24; i++) {
    if (app->saved.hours[i] > app->saved.hours[hour]) {
      hour = i;
    }
  }
  return hour;
}

static void show(const app_t *app) {
  printf("\nMODE=%s ADMIN=%s BUILDING=%d %s\n",
         app->saved.emergency ? "EMERGENCY" : "NORMAL/DEGRADED",
         access_admin(app) ? "YES" : "NO", app->saved.building,
         app->saved.building_uncertain ? "VERIFY" : "CONFIRMED");
  printf("ROOM1=%d %s LIGHT=%s; ROOM2=%d %s LIGHT=%s (commanded)\n",
         app->saved.rooms[0], app->saved.room_uncertain[0] ? "VERIFY" : "OK",
         app->lights[0] ? "ON" : "OFF", app->saved.rooms[1],
         app->saved.room_uncertain[1] ? "VERIFY" : "OK",
         app->lights[1] ? "ON" : "OFF");
  printf("SCHEDULE=%02d:%02d-%02d:%02d EAT; TIME=%s\n",
         app->saved.open_min / 60, app->saved.open_min % 60,
         app->saved.close_min / 60, app->saved.close_min % 60,
         app->time_valid ? "VALID" : "INVALID");
  printf("SD=%s NVS=%s PENDING=%u LOST=%u LCD=%s INITIALIZATION=%s\n",
         app->storage_ok ? "OK" : "FAULT", app->nvs_ok ? "OK" : "FAULT",
         app->pending, app->lost, app->display_ok ? "OK" : "FAULT",
         app->input_ok ? "OK" : "FAULT");
  printf("DAY=%d ENTRIES=%u UNIQUE=%u PEAK=%u BUSIEST=%s",
         app->saved.day, app->saved.entries, unique_count(app->saved.unique),
         app->saved.peak, app->saved.entries ? "" : "N/A\n");
  if (app->saved.entries) {
    printf("%02d:00-%02d:00 EAT\n", busiest(app), (busiest(app) + 1) % 24);
  }
  for (int i = 0; i < SCMS_USERS; i++) {
    printf("%s: %s %s\n", access_name(i),
           (app->saved.inside & (1u << i)) ? "INSIDE" : "OUTSIDE",
           (app->saved.disabled & (1u << i)) ? "DISABLED" : "ENABLED");
  }
  printf("Identity list is last-known when BUILDING=VERIFY.\n%s\n", app->message);
  printf("RFID readers=%d%d%d (1=responding)\n", app->readers_ok[0],
         app->readers_ok[1], app->readers_ok[2]);
}

static void help(void) {
  puts("\nPUBLIC: help | status | logs | people | stats");
  puts("Scan Yellow Admin card at entry to authenticate for 60 seconds.");
  puts("ADMIN: schedule HH:MM HH:MM | verify-empty CONFIRM");
  puts("       room 1 COUNT CONFIRM | room 2 COUNT CONFIRM");
  puts("       clear-emergency CONFIRM | emergency CONFIRM | restart CONFIRM");
  puts("       disable blue | enable blue | disable green | enable green | logout");
  puts("verify-empty requires physically checking the ENTIRE building is empty.");
  puts("clear-emergency requires physical hazard/door checks; counts remain uncertain.");
}

static void command(app_t *app, const char *line) {
  if (strcmp(line, "help") == 0) {
    help();
    return;
  }
  if (strcmp(line, "logs") == 0) {
    storage_print_logs(app);
    return;
  }
  if (strcmp(line, "status") == 0 || strcmp(line, "people") == 0 ||
      strcmp(line, "stats") == 0) {
    show(app);
    return;
  }
  if (!access_admin(app)) {
    record_event(app, "ADMIN_REJECTED", "Authenticate with Admin card first");
    return;
  }
  int a, b, c, d;
  char extra;
  bool ok = false;
  if (strcmp(line, "logout") == 0) {
    app->admin_until = 0;
    record_event(app, "ADMIN_LOGOUT", "Session closed");
    return;
  } else if (sscanf(line, "schedule %2d:%2d %2d:%2d %c", &a, &b, &c, &d, &extra) == 4) {
    if (a >= 0 && a < 24 && b >= 0 && b < 60 &&
        c >= 0 && c < 24 && d >= 0 && d < 60) {
      ok = access_schedule(app, a * 60 + b, c * 60 + d);
    }
  } else if (strcmp(line, "verify-empty CONFIRM") == 0) {
    ok = occupancy_correct(app, 0, 0);
  } else if (sscanf(line, "room %1d %4d CONFIRM %c", &a, &b, &extra) == 2) {
    /* Check the literal too: scanf assignment counts do not validate trailing text. */
    char expected[64];
    snprintf(expected, sizeof(expected), "room %d %d CONFIRM", a, b);
    if (strcmp(line, expected) == 0 && (a == 1 || a == 2)) {
      ok = occupancy_correct(app, a, b);
    }
  } else if (strcmp(line, "clear-emergency CONFIRM") == 0) {
    ok = safety_clear(app);
  } else if (strcmp(line, "emergency CONFIRM") == 0) {
    safety_enter(app, "Authenticated serial emergency");
    return;
  } else if (strcmp(line, "restart CONFIRM") == 0) {
    safety_restart(app);
    return;
  } else {
    for (int i = 0; i < 2; i++) {
      char enable[32], disable[32];
      const char *name = i == 0 ? "blue" : "green";
      snprintf(enable, sizeof(enable), "enable %s", name);
      snprintf(disable, sizeof(disable), "disable %s", name);
      if (strcmp(line, enable) == 0 || strcmp(line, disable) == 0) {
        if (strcmp(line, enable) == 0) {
          app->saved.disabled &= ~(1u << i);
        } else {
          app->saved.disabled |= 1u << i;
          app->pass_until[i] = 0;
        }
        app->dirty = true;
        ok = storage_save(app);
        record_event(app, "CARD_ADMIN", line);
        break;
      }
    }
  }
  if (!ok) {
    record_event(app, "COMMAND_REJECTED", "Invalid input, failed checks or persistence error");
  } else {
    puts("APPLIED AND SAVED");
  }
}

void dashboard_serial(app_t *app) {
  uint8_t bytes[64];
  int count = uart_read_bytes(UART_NUM_0, bytes, sizeof(bytes), 0);
  for (int i = 0; i < count; i++) {
    unsigned char ch = bytes[i];
    if (ch == '\r' || ch == '\n') {
      if (serial_overflow) {
        record_event(app, "COMMAND_REJECTED", "Serial command too long");
      } else if (serial_length > 0) {
        serial_line[serial_length] = '\0';
        command(app, serial_line);
      }
      serial_length = 0;
      serial_overflow = false;
    } else if ((ch == 8 || ch == 127) && serial_length > 0) {
      serial_length--;
    } else if (isprint(ch)) {
      if (serial_length + 1 < sizeof(serial_line)) {
        serial_line[serial_length++] = ch;
      } else {
        serial_overflow = true;
      }
    }
  }
}

static void select_action(app_t *app) {
  if (mode == 0) {
    if (page == 5) {
      char text[100];
      log_index++;
      if (!storage_recent(log_index, text, sizeof(text))) {
        log_index = 0;
      }
    }
    if (page == 6) {
      if (access_admin(app)) {
        mode = 1;
      } else {
        record_event(app, "ADMIN_REQUIRED", "Scan Yellow Admin card at entry");
      }
    }
    return;
  }
  if (!access_admin(app)) {
    mode = 0;
    record_event(app, "ADMIN_EXPIRED", "Scan Admin card again");
    return;
  }
  if (mode == 1) {
    if (admin_item == 0) {
      mode = 2;
    } else if (admin_item == 1 || admin_item == 2) {
      edit_value = app->saved.rooms[admin_item - 1];
      mode = 3;
    } else if (admin_item == 3) {
      opening = app->saved.open_min;
      closing = app->saved.close_min;
      mode = 4;
    } else if (admin_item == 4 || admin_item == 5) {
      mode = 2;
    } else {
      app->admin_until = 0;
      mode = 0;
      record_event(app, "ADMIN_LOGOUT", "LCD session closed");
    }
  } else if (mode == 2) {
    if (admin_item == 0) {
      occupancy_correct(app, 0, 0);
    } else if (admin_item == 1 || admin_item == 2) {
      occupancy_correct(app, admin_item, edit_value);
    } else if (admin_item == 3) {
      access_schedule(app, opening, closing);
    } else if (admin_item == 4) {
      safety_clear(app);
    } else if (admin_item == 5) {
      safety_restart(app);
    }
    mode = 1;
  } else if (mode == 3) {
    mode = 2;
  } else if (mode == 4) {
    mode = 5;
  } else if (mode == 5) {
    mode = 2;
  }
}

void dashboard_tick(app_t *app, uint16_t inputs) {
  bool next = (inputs & (1u << 14)) != 0;
  bool select = (inputs & (1u << 15)) != 0;
  if (next && !previous_next) {
    if (mode == 0) {
      page = (page + 1) % 7;
    } else if (mode == 1) {
      admin_item = (admin_item + 1) % 7;
    } else if (mode == 2) {
      mode = 1;
    } else if (mode == 3) {
      edit_value = (edit_value + 1) % 1001;
    } else if (mode == 4) {
      opening = (opening + 30) % 1440;
    } else if (mode == 5) {
      closing = (closing + 30) % 1440;
    }
  }
  if (select && !previous_select) {
    select_since = app->now;
    held_back = false;
  }
  if (select && select_since >= 0 && app->now - select_since >= 1000 &&
      !held_back) {
    mode = mode > 1 ? 1 : 0;
    page = mode == 0 ? 0 : page;
    held_back = true;
  }
  if (!select && previous_select && !held_back) {
    select_action(app);
  }
  previous_next = next;
  previous_select = select;
  if (mode != 0 && !access_admin(app)) {
    mode = 0;
  }
  if (app->now < next_display) {
    return;
  }
  next_display = app->now + 400;
  char lines[4][21] = {{0}};
  if (mode == 1) {
    static const char *items[] = {"Verify building empty", "Correct Room 1",
      "Correct Room 2", "Set access schedule", "Clear emergency",
      "Restart controller", "Logout"};
    snprintf(lines[0], 21, "ADMIN%s", app->saved.emergency ? " EMERGENCY" : "");
    snprintf(lines[1], 21, "%.20s", items[admin_item]);
    snprintf(lines[2], 21, "Next: item");
    snprintf(lines[3], 21, "Select: open");
  } else if (mode == 2) {
    snprintf(lines[0], 21, "PHYSICALLY VERIFIED?");
    snprintf(lines[1], 21, "%s", admin_item == 4 ? "Hazard/doors safe?" :
             admin_item == 0 ? "Building empty?" : "Confirm selected act");
    snprintf(lines[2], 21, "Select: YES, apply");
    snprintf(lines[3], 21, "Next: CANCEL");
  } else if (mode == 3) {
    snprintf(lines[0], 21, "Room%d verified count", admin_item);
    snprintf(lines[1], 21, "%d", edit_value);
    snprintf(lines[2], 21, "Next: +1");
    snprintf(lines[3], 21, "Select: confirm");
  } else if (mode == 4 || mode == 5) {
    int minutes = mode == 4 ? opening : closing;
    snprintf(lines[0], 21, "%s time (EAT)", mode == 4 ? "Opening" : "Closing");
    snprintf(lines[1], 21, "%02d:%02d", minutes / 60, minutes % 60);
    snprintf(lines[2], 21, "Next: +30 minutes");
    snprintf(lines[3], 21, "Select: next");
  } else if (page == 0) {
    snprintf(lines[0], 21, "CoCIS %s", app->saved.emergency ? "EMERGENCY" : "MONITOR");
    snprintf(lines[1], 21, "Building:%d %s", app->saved.building,
             app->saved.building_uncertain ? "VERIFY" : "OK");
    snprintf(lines[2], 21, "R1:%d R2:%d", app->saved.rooms[0], app->saved.rooms[1]);
    snprintf(lines[3], 21, "%.20s", app->message);
  } else if (page == 1) {
    snprintf(lines[0], 21, "ROOMS / LIGHT CMD");
    for (int i = 0; i < 2; i++) {
      snprintf(lines[i + 1], 21, "R%d:%d %s %s", i + 1, app->saved.rooms[i],
               app->lights[i] ? "ON" : "OFF",
               app->saved.room_uncertain[i] ? "VERIFY" : "OK");
    }
    snprintf(lines[3], 21, "PIR1:%d PIR2:%d", hw_pir(0), hw_pir(1));
  } else if (page == 2) {
    snprintf(lines[0], 21, "ACCESS SCHEDULE EAT");
    snprintf(lines[1], 21, "%02u:%02u to %02u:%02u",
             (unsigned int)app->saved.open_min / 60 % 24,
             (unsigned int)app->saved.open_min % 60,
             (unsigned int)app->saved.close_min / 60 % 24,
             (unsigned int)app->saved.close_min % 60);
    snprintf(lines[2], 21, "%s", !app->time_valid ? "TIME INVALID" :
             access_hours(&app->saved, app->last_minute) ? "ENTRY OPEN" : "ENTRY CLOSED");
    snprintf(lines[3], 21, "Now %02d:%02d", app->last_minute < 0 ? 0 :
             app->last_minute / 60, app->last_minute < 0 ? 0 : app->last_minute % 60);
  } else if (page == 3) {
    snprintf(lines[0], 21, "RFID:%d%d%d RTC:%s", app->readers_ok[0],
             app->readers_ok[1], app->readers_ok[2], app->time_valid ? "OK" : "FAIL");
    snprintf(lines[1], 21, "SD:%s NVS:%s", app->storage_ok ? "OK" : "FAIL",
             app->nvs_ok ? "OK" : "FAIL");
    snprintf(lines[2], 21, "Queue:%u Lost:%u", app->pending, app->lost);
    snprintf(lines[3], 21, "Doors faults:%d%d%d", app->gates[0].door_fault,
             app->gates[1].door_fault, app->gates[2].door_fault);
  } else if (page == 4) {
    snprintf(lines[0], 21, "DAILY %d", app->saved.day);
    snprintf(lines[1], 21, "Entries:%u Unique:%u", app->saved.entries,
             unique_count(app->saved.unique));
    snprintf(lines[2], 21, "Peak:%u", app->saved.peak);
    if (app->saved.entries) {
      snprintf(lines[3], 21, "Busiest:%02d:00 EAT", busiest(app));
    } else {
      snprintf(lines[3], 21, "Busiest: no entries");
    }
  } else if (page == 5) {
    char text[100];
    if (!storage_recent(log_index, text, sizeof(text))) {
      snprintf(text, sizeof(text), "%s", app->message);
    }
    snprintf(lines[0], 21, "RECENT EVENT %u", log_index + 1);
    snprintf(lines[1], 21, "%.20s", text);
    snprintf(lines[2], 21, "%.20s", strlen(text) > 20 ? text + 20 : "");
    snprintf(lines[3], 21, "Select: older event");
  } else {
    snprintf(lines[0], 21, "ADMINISTRATION");
    snprintf(lines[1], 21, "%s", access_admin(app) ? "Authenticated" : "Scan Yellow at entry");
    snprintf(lines[2], 21, "Select: admin menu");
    snprintf(lines[3], 21, "Hold Select: back");
  }
  bool ok = hw_display(lines);
  if (ok != app->display_ok) {
    app->display_ok = ok;
    record_event(app, ok ? "LCD_RESTORED" : "LCD_FAULT",
                 ok ? "Display responds" : "Use serial diagnostics");
  }
}
