#include "access_control.h"
#include "occupancy.h"
#include "scheduler.h"
#include "logger.h"

static scms_user_t g_users[SCMS_MAX_USERS];
static int g_user_count = 0;

static scms_user_t* find_user(uint32_t id) {
  for (int i = 0; i < g_user_count; i++) {
    if (g_users[i].user_id == id) return &g_users[i];
  }
  return NULL;
}

void access_init(void) {
  g_user_count = 3;
  g_users[0] = (scms_user_t){ .user_id = 1001, .is_authorized = true, .is_inside = false };
  g_users[1] = (scms_user_t){ .user_id = 1002, .is_authorized = true, .is_inside = false };
  g_users[2] = (scms_user_t){ .user_id = 2001, .is_authorized = true, .is_inside = false };
}

scms_access_result_t access_attempt(uint32_t user_id, scms_direction_t dir, int hour_of_day) {
  scms_user_t* u = find_user(user_id);
  if (!u || !u->is_authorized) {
    log_event("ACCESS DENY: unknown ID");
    return SCMS_DENY_UNKNOWN_ID;
  }

  if (!schedule_is_open(hour_of_day)) {
    log_event("ACCESS DENY: schedule closed");
    return SCMS_DENY_SCHEDULE_CLOSED;
  }

  if (dir == SCMS_DIR_ENTRY) {
    if (u->is_inside) {
      log_event("ACCESS DENY: duplicate entry");
      return SCMS_DENY_ALREADY_INSIDE;
    }
    u->is_inside = true;
    occupancy_on_entry();
    log_event("ACCESS GRANTED: entry");
    return SCMS_ACCESS_GRANTED;
  }

  if (!u->is_inside) {
    log_event("ACCESS DENY: exit without entry");
    return SCMS_DENY_NOT_INSIDE;
  }

  u->is_inside = false;
  occupancy_on_exit();
  log_event("ACCESS GRANTED: exit");
  return SCMS_ACCESS_GRANTED;
}

const char* access_result_str(scms_access_result_t r) {
  switch (r) {
    case SCMS_ACCESS_GRANTED: return "GRANTED";
    case SCMS_DENY_UNKNOWN_ID: return "DENY_UNKNOWN_ID";
    case SCMS_DENY_ALREADY_INSIDE: return "DENY_ALREADY_INSIDE";
    case SCMS_DENY_NOT_INSIDE: return "DENY_NOT_INSIDE";
    case SCMS_DENY_SCHEDULE_CLOSED: return "DENY_SCHEDULE_CLOSED";
    default: return "UNKNOWN";
  }
}
