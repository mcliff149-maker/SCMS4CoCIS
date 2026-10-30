#include "scms.h"
#include <stdio.h>
#include <string.h>

static int64_t feedback_until;
static bool feedback_success;
static int64_t denied_since;
static unsigned int denied_count;

const char *access_name(int user) {
  static const char *names[] = {"Blue", "Green", "Admin"};
  return user >= 0 && user < SCMS_USERS ? names[user] : "Unknown";
}

int access_user(const scms_uid_t *uid) {
  static const uint8_t cards[SCMS_USERS][4] = {
    {0x01, 0x02, 0x03, 0x04},
    {0x11, 0x22, 0x33, 0x44},
    {0x55, 0x66, 0x77, 0x88}
  };
  for (int i = 0; i < SCMS_USERS; i++) {
    if (uid->size == 4 && memcmp(cards[i], uid->bytes, 4) == 0) {
      return i;
    }
  }
  return -1;
}

bool access_admin(const app_t *app) {
  return app->admin_until > app->now && !(app->saved.disabled & (1u << 2));
}

bool access_hours(const saved_t *saved, int minute) {
  if (minute < 0 || minute >= 1440 ||
      saved->open_min == saved->close_min) {
    return false;
  }
  if (saved->open_min < saved->close_min) {
    return minute >= saved->open_min && minute < saved->close_min;
  }
  return minute >= saved->open_min || minute < saved->close_min;
}

bool access_schedule(app_t *app, int opening, int closing) {
  if (!access_admin(app) || opening < 0 || opening >= 1440 ||
      closing < 0 || closing >= 1440 || opening == closing) {
    record_event(app, "ADMIN_REJECTED", "Invalid schedule or admin session");
    return false;
  }
  int old_open = app->saved.open_min;
  int old_close = app->saved.close_min;
  app->saved.open_min = opening;
  app->saved.close_min = closing;
  app->dirty = true;
  bool saved = storage_save(app);
  char detail[100];
  snprintf(detail, sizeof(detail), "Admin schedule %04d-%04d -> %04d-%04d%s",
           old_open, old_close, opening, closing,
           saved ? "" : " PERSISTENCE FAILED");
  record_event(app, "SCHEDULE", detail);
  return saved;
}

void access_feedback(app_t *app, bool success) {
  feedback_success = success;
  feedback_until = app->now + (success ? 500 : 1000);
}

static void deny(app_t *app, int reader, int user, const char *reason) {
  char detail[100];
  snprintf(detail, sizeof(detail), "Reader%d %s: %s",
           reader + 1, access_name(user), reason);
  record_event(app, "DENIED", detail);
  if (app->now - denied_since > 30000) {
    denied_since = app->now;
    denied_count = 0;
  }
  denied_count++;
  if (denied_count == 3) {
    record_event(app, "SECURITY_ALERT", "Three denied attempts within 30 seconds; investigate");
  }
  access_feedback(app, false);
}

static void open_gate(app_t *app, int reader, int user) {
  gate_t *gate = &app->gates[reader];
  gate->user = user;
  gate->until = app->now + SCMS_DOOR_MS;
  gate->opened_at = app->now;
  gate->door_open = true;
  hw_gate(reader, true);
  access_feedback(app, true);
  char detail[80];
  snprintf(detail, sizeof(detail), "Reader%d %s; awaiting passage",
           reader + 1, access_name(user));
  record_event(app, "AUTHORIZED", detail);
}

void access_scan(app_t *app, int reader, int user) {
  if (reader != 2 && user >= 0 && (app->saved.disabled & (1u << user))) {
    deny(app, reader, user, "Card disabled; safe exit remains independent");
    return;
  }
  if (reader == 0 && user == 2) {
    app->admin_until = app->now + SCMS_ADMIN_MS;
    record_event(app, "ADMIN_LOGIN", "Admin session opened for 60 seconds");
    access_feedback(app, true);
    return;
  }
  if (app->saved.emergency) {
    record_event(app, "EMERGENCY", "Ordinary access suspended; use evacuation path");
    return;
  }
  if (app->gates[reader].door_open) {
    deny(app, reader, user, "Passage already in progress");
    return;
  }
  if (app->gates[reader].door_fault) {
    deny(app, reader, user, "Door needs checking; use safe egress if exiting");
    return;
  }
  if (reader == 2) {
    if (user < 0) {
      record_event(app, "UNTRACKED_EXIT_REQUEST", "Unknown card; allow exit, flag identity on passage");
    }
    open_gate(app, reader, user);
    return;
  }
  if (user < 0) {
    deny(app, reader, user, "Unregistered card");
    return;
  }
  if (!app->time_valid || !access_hours(&app->saved, app->last_minute)) {
    deny(app, reader, user, app->time_valid ? "Outside access hours" : "Time invalid");
    return;
  }
  if (!app->nvs_ok || app->pending >= 28 || !app->input_ok) {
    deny(app, reader, user, "Critical fault or logging queue nearly full");
    return;
  }
  if (reader == 0) {
    if (app->saved.building_uncertain) {
      deny(app, reader, user, "Admin must verify building occupancy first");
      return;
    }
    if (app->saved.inside & (1u << user)) {
      deny(app, reader, user, "Already recorded inside");
      return;
    }
  } else if (!(app->saved.inside & (1u << user)) ||
             app->pass_until[user] <= app->now) {
    deny(app, reader, user, "Fast-pass missing or expired; contact admin");
    return;
  }
  open_gate(app, reader, user);
}

void access_cancel(app_t *app) {
  for (int i = 0; i < SCMS_USERS; i++) {
    app->pass_until[i] = 0;
  }
  for (int i = 0; i < 3; i++) {
    app->gates[i].user = -1;
    app->gates[i].until = 0;
  }
}

void access_passage(app_t *app, int zone, int direction) {
  gate_t *gate = &app->gates[zone];
  int user = gate->until > app->now ? gate->user : -1;
  char detail[100];
  snprintf(detail, sizeof(detail), "Zone%d %s %s", zone + 1,
           access_name(user), direction > 0 ? "forward" : "reverse");

  if (zone == 1) {
    if (direction > 0 && user >= 0) {
      app->pass_until[user] = 0;
    } else {
      record_event(app, "CHECKPOINT_ANOMALY", detail);
    }
  } else {
    bool entering = (zone == 0 && direction > 0) ||
                    (zone == 2 && direction < 0);
    if (entering) {
      if (app->saved.building < 1000) {
        app->saved.building++;
      } else {
        app->saved.building_uncertain = true;
      }
      if (zone == 0 && user >= 0 && !app->saved.emergency &&
          !(app->saved.inside & (1u << user))) {
        app->saved.inside |= 1u << user;
        app->pass_until[user] = app->now + SCMS_PASS_MS;
        statistics_entry(app, user);
      } else {
        app->saved.building_uncertain = true;
        record_event(app, "UNAUTHORIZED_PASSAGE", detail);
      }
    } else {
      if (app->saved.building > 0) {
        app->saved.building--;
      } else {
        app->saved.building_uncertain = true;
      }
      if (user >= 0 && (app->saved.inside & (1u << user))) {
        app->saved.inside &= ~(1u << user);
        app->pass_until[user] = 0;
      } else {
        app->saved.building_uncertain = true;
        record_event(app, "UNIDENTIFIED_EXIT", detail);
      }
      if (app->saved.rooms[0] + app->saved.rooms[1] > app->saved.building) {
        for (int room = 0; room < 2; room++) {
          if (app->saved.rooms[room] > 0) {
            app->saved.room_uncertain[room] = true;
          }
        }
        record_event(app, "COUNT_MISMATCH", "Room totals exceed building; physically verify rooms");
      }
    }
    app->dirty = true;
  }
  record_event(app, zone == 1 ? "CHECKPOINT_PASSAGE" : "BUILDING_PASSAGE", detail);
  gate->user = -1;
  gate->until = 0;
}

void access_tick(app_t *app, uint16_t inputs) {
  static const int feedback_bits[3] = {10, 11, 12};
  for (int i = 0; i < 3; i++) {
    gate_t *gate = &app->gates[i];
    bool open = (inputs & (1u << feedback_bits[i])) != 0;
    if (open && !gate->feedback_open) {
      gate->opened_at = app->now;
      if (!gate->door_open && !app->saved.emergency) {
        gate->door_fault = true;
        record_event(app, "DOOR_FORCED", "Door feedback opened without authorization");
      }
    }
    gate->feedback_open = open;
    if (open && app->now - gate->opened_at > 10000 &&
        !gate->door_fault && !app->saved.emergency) {
      gate->door_fault = true;
      record_event(app, "DOOR_HELD", "Door has remained open over 10 seconds");
    }
    if (!open && gate->door_fault) {
      gate->door_fault = false;
      record_event(app, "DOOR_RESTORED", "Door closed; fault condition removed");
    }
    if (app->saved.emergency) {
      if (!gate->door_open) {
        hw_gate(i, true);
        gate->door_open = true;
      }
    } else if (gate->door_open && app->now >= gate->until) {
      /* Do not command closure until feedback and passage beams are clear. */
      bool beams_clear = ((inputs >> (i * 2)) & 3) == 0;
      if (!open && beams_clear && app->beams[i].state == 0) {
        hw_gate(i, false);
        gate->door_open = false;
        if (gate->user >= 0) {
          record_event(app, "NO_PASSAGE", "Authorization timed out without crossing");
        }
        gate->user = -1;
      }
    }
  }
  if (!app->saved.emergency) {
    bool active = app->now < feedback_until;
    bool fault = !app->storage_ok || !app->nvs_ok || !app->time_valid;
    hw_buzzer(active && !feedback_success);
    if (active) {
      hw_status(feedback_success ? 0 : 35, feedback_success ? 35 : 0, 0);
    } else {
      hw_status(fault ? 20 : 0, fault ? 10 : 8, fault ? 0 : 8);
    }
  }
}
