#include "scms.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool presence[2];
static bool gates[3];
static bool lights[2];
static bool save_ok = true;
static unsigned int events;
static unsigned int restarts;
static char last_event[80];

void record_event(app_t *app, const char *type, const char *detail) {
  (void)app;
  (void)detail;
  events++;
  snprintf(last_event, sizeof(last_event), "%s", type);
}
bool storage_save(app_t *app) {
  app->nvs_ok = save_ok;
  if (save_ok) {
    app->dirty = false;
  }
  return save_ok;
}
void storage_tick(app_t *app) { (void)app; }
void hw_gate(int gate, bool open) { gates[gate] = open; }
void hw_light(int room, bool on) { lights[room] = on; }
bool hw_pir(int room) { return presence[room]; }
void hw_buzzer(bool on) { (void)on; }
void hw_status(unsigned int r, unsigned int g, unsigned int b) {
  (void)r; (void)g; (void)b;
}
void hw_restart(void) { restarts++; }

static app_t ready(void) {
  app_t app = {0};
  app.now = 100;
  app.saved.version = SCMS_SNAPSHOT_VERSION;
  app.saved.open_min = 7 * 60;
  app.saved.close_min = 22 * 60;
  app.saved.day = 20261029;
  app.utc = 1793264400; /* 2026-10-29 09:00 UTC */
  app.time_valid = true;
  app.last_minute = 12 * 60;
  app.nvs_ok = true;
  app.storage_ok = true;
  app.input_ok = true;
  app.vacancy_since[0] = -1;
  app.vacancy_since[1] = -1;
  for (int i = 0; i < 3; i++) {
    app.gates[i].user = -1;
  }
  memset(presence, 0, sizeof(presence));
  memset(gates, 0, sizeof(gates));
  memset(lights, 0, sizeof(lights));
  save_ok = true;
  return app;
}

static void crossing(app_t *app, int zone, bool forward) {
  unsigned int a = forward ? 1 : 2;
  unsigned int b = forward ? 2 : 1;
  unsigned int sequence[] = {0, a, 3, b, 0};
  for (int i = 0; i < 5; i++) {
    app->now += 200;
    occupancy_tick(app, (uint16_t)(sequence[i] << (zone * 2)));
  }
}

static void test_beams(void) {
  beam_t beam = {0};
  assert(beam_step(&beam, 0, 0) == 0);
  assert(beam_step(&beam, 1, 200) == 0);
  assert(beam_step(&beam, 1, 220) == 0);
  assert(beam_step(&beam, 3, 400) == 0);
  assert(beam_step(&beam, 2, 600) == 0);
  assert(beam_step(&beam, 0, 800) == 1);
  assert(beam_step(&beam, 2, 1000) == 0);
  assert(beam_step(&beam, 3, 1200) == 0);
  assert(beam_step(&beam, 1, 1400) == 0);
  assert(beam_step(&beam, 0, 1600) == -1);
  assert(beam_step(&beam, 3, 1800) == 2);
  assert(beam_step(&beam, 0, 2000) == 0);
  assert(beam_step(&beam, 1, 2200) == 0);
  assert(beam_step(&beam, 0, 2400) == 2);
  assert(beam_step(&beam, 1, 2600) == 0);
  assert(beam_step(&beam, 1, 5300) == 2);
}

static void test_hours(void) {
  app_t app = ready();
  assert(!access_hours(&app.saved, 419));
  assert(access_hours(&app.saved, 420));
  assert(access_hours(&app.saved, 1319));
  assert(!access_hours(&app.saved, 1320));
  app.saved.open_min = 22 * 60;
  app.saved.close_min = 6 * 60;
  assert(access_hours(&app.saved, 23 * 60));
  assert(access_hours(&app.saved, 5 * 60));
  assert(!access_hours(&app.saved, 6 * 60));
  assert(!access_hours(&app.saved, -1));
  assert(!access_hours(&app.saved, 1440));
}

static void test_journey(void) {
  app_t app = ready();
  access_scan(&app, 0, 0);
  assert(gates[0] && app.saved.building == 0);
  crossing(&app, 0, true);
  assert(app.saved.building == 1 && app.saved.inside == 1);
  assert(app.saved.entries == 1 && app.pass_until[0] > app.now);
  access_scan(&app, 1, 0);
  assert(gates[1]);
  crossing(&app, 1, true);
  assert(app.saved.building == 1 && app.pass_until[0] == 0);
  crossing(&app, 3, true);
  assert(app.saved.rooms[0] == 1 && lights[0]);
  crossing(&app, 3, false);
  assert(app.saved.rooms[0] == 0 && lights[0]);
  app.now += SCMS_VACANCY_MS;
  occupancy_tick(&app, 0);
  assert(!lights[0]);
  access_scan(&app, 2, 0);
  crossing(&app, 2, true);
  assert(app.saved.building == 0 && app.saved.inside == 0);
  assert(app.saved.entries == 1);
}

static void test_denials(void) {
  app_t app = ready();
  access_scan(&app, 0, -1);
  assert(!gates[0] && app.saved.building == 0);
  app.time_valid = false;
  access_scan(&app, 0, 0);
  assert(!gates[0]);
  app.time_valid = true;
  app.last_minute = 0;
  access_scan(&app, 0, 0);
  assert(!gates[0]);
  app.last_minute = 12 * 60;
  app.saved.building_uncertain = true;
  access_scan(&app, 0, 0);
  assert(!gates[0]);
  app.saved.building_uncertain = false;
  app.pending = 28;
  access_scan(&app, 0, 0);
  assert(!gates[0]);
  app.pending = 0;
  app.saved.disabled = 1;
  access_scan(&app, 0, 0);
  assert(!gates[0]);
  app.saved.disabled = 0;
  app.saved.inside = 1;
  access_scan(&app, 0, 0);
  assert(!gates[0]);
  app.saved.inside = 0;
  access_scan(&app, 1, 0);
  assert(!gates[1]);
}

static void test_timeout_and_unknown(void) {
  app_t app = ready();
  access_scan(&app, 0, 0);
  app.now += SCMS_DOOR_MS + 1;
  access_tick(&app, 0);
  assert(!gates[0] && app.saved.building == 0 && app.pass_until[0] == 0);
  crossing(&app, 0, true);
  assert(app.saved.building == 1 && app.saved.building_uncertain);
  crossing(&app, 2, true);
  assert(app.saved.building == 0 && app.saved.building_uncertain);
  crossing(&app, 2, true);
  assert(app.saved.building == 0);
  app = ready();
  app.saved.inside = 1;
  app.pass_until[0] = app.now - 1;
  access_scan(&app, 1, 0);
  assert(!gates[1]);
  app = ready();
  app.saved.building = 1;
  app.saved.inside = 1;
  app.saved.rooms[0] = 1;
  app.saved.disabled = 1;
  app.time_valid = false;
  access_scan(&app, 2, 0);
  assert(gates[2]);
  crossing(&app, 2, true);
  assert(app.saved.building == 0 && app.saved.inside == 0);
  assert(app.saved.room_uncertain[0]);
  app = ready();
  app.saved.building = 1;
  access_scan(&app, 2, -1);
  assert(gates[2]);
  crossing(&app, 2, true);
  assert(app.saved.building == 0 && app.saved.building_uncertain);
}

static void test_emergency(void) {
  app_t app = ready();
  app.emergency_input = true;
  safety_tick(&app);
  assert(app.saved.emergency && app.saved.incident == 1);
  assert(gates[0] && gates[1] && gates[2] && lights[0] && lights[1]);
  assert(app.saved.building_uncertain && app.saved.room_uncertain[0]);
  assert(!safety_clear(&app));
  access_scan(&app, 0, 2);
  assert(access_admin(&app));
  assert(!safety_clear(&app));
  app.emergency_input = false;
  save_ok = false;
  assert(!safety_clear(&app) && app.saved.emergency);
  save_ok = true;
  assert(safety_clear(&app) && !app.saved.emergency);
  assert(app.saved.building_uncertain);
  safety_restart(&app);
  assert(restarts == 1);
}

static void test_reconciliation(void) {
  app_t app = ready();
  assert(!occupancy_correct(&app, 0, 0));
  access_scan(&app, 0, 2);
  app.saved.building_uncertain = true;
  presence[0] = true;
  assert(!occupancy_correct(&app, 0, 0));
  presence[0] = false;
  assert(!occupancy_correct(&app, 0, 3));
  assert(occupancy_correct(&app, 0, 0));
  assert(!app.saved.building_uncertain && !app.saved.room_uncertain[0]);
  assert(!occupancy_correct(&app, 1, 1));
  assert(!access_schedule(&app, 10, 10));
  assert(access_schedule(&app, 0, 1439));
  app.now += SCMS_ADMIN_MS + 1;
  assert(!access_admin(&app));
}

static void test_uncertainty(void) {
  app_t app = ready();
  presence[0] = true;
  occupancy_tick(&app, 0);
  assert(app.saved.room_uncertain[0] && lights[0]);
  presence[0] = false;
  app.now += 20000;
  occupancy_tick(&app, 0);
  assert(lights[0]);
  access_scan(&app, 0, 2);
  assert(occupancy_correct(&app, 1, 0));
  app.now++;
  occupancy_tick(&app, 0);
  app.now += SCMS_VACANCY_MS;
  occupancy_tick(&app, 0);
  assert(!lights[0]);
}

int main(void) {
  test_beams();
  test_hours();
  test_journey();
  test_denials();
  test_timeout_and_unknown();
  test_emergency();
  test_reconciliation();
  test_uncertainty();
  printf("PASS: passage sequences, timeouts, overnight schedules, complete journey,\n");
  printf("      denied entry, fast-pass expiry, unknown exits, vacancy delay,\n");
  printf("      emergency latch/clear, admin expiry, reconciliation (%u events).\n", events);
  return 0;
}
