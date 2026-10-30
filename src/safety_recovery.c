#include "scms.h"

void safety_enter(app_t *app, const char *reason) {
  if (app->saved.emergency) {
    return;
  }
  app->saved.emergency = true;
  app->saved.incident++;
  app->saved.building_uncertain = true;
  app->saved.room_uncertain[0] = true;
  app->saved.room_uncertain[1] = true;
  access_cancel(app);
  /* Release commands precede storage operations; this is not physical egress protection. */
  for (int i = 0; i < 3; i++) {
    hw_gate(i, true);
    app->gates[i].door_open = true;
  }
  for (int room = 0; room < 2; room++) {
    hw_light(room, true);
    app->lights[room] = true;
  }
  app->dirty = true;
  storage_save(app);
  record_event(app, "EMERGENCY_LATCHED", reason);
}

bool safety_clear(app_t *app) {
  if (!access_admin(app) || app->emergency_input || app->fire_input ||
      !app->saved.emergency) {
    record_event(app, "CLEAR_REJECTED", "Need admin and inactive emergency/fire inputs");
    return false;
  }
  /* The operator must physically verify safety; counts remain explicitly uncertain. */
  app->saved.emergency = false;
  app->dirty = true;
  if (!storage_save(app)) {
    app->saved.emergency = true;
    app->dirty = true;
    record_event(app, "CLEAR_REJECTED", "Could not persist emergency clearance");
    return false;
  }
  access_cancel(app);
  record_event(app, "EMERGENCY_CLEARED", "Admin confirmed safety; verify occupancy next");
  return true;
}

void safety_tick(app_t *app) {
  if (app->emergency_input || app->fire_input) {
    safety_enter(app, app->emergency_input ? "Emergency switch" : "Fire-alarm signal");
  }
  if (app->saved.emergency) {
    bool flash = ((app->now / 300) % 2) == 0;
    hw_status(flash ? 50 : 0, 0, 0);
    hw_buzzer(flash);
  }
}

void safety_restart(app_t *app) {
  if (!access_admin(app)) {
    record_event(app, "RESTART_REJECTED", "Admin authentication required");
    return;
  }
  record_event(app, "RESTART", "Admin requested restart; occupancy will need verification");
  app->saved.building_uncertain = true;
  app->dirty = true;
  if (!storage_save(app)) {
    record_event(app, "RESTART_REJECTED", "Could not persist recovery state");
    return;
  }
  storage_tick(app);
  hw_restart();
}
