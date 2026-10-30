#include "scms.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *input;
static char last_event[80];
static unsigned int saves;
static bool presence[2];

int uart_read_bytes(int port, void *buffer, size_t size, unsigned int ticks) {
  (void)port; (void)ticks;
  size_t count = strlen(input);
  if (count > size) {
    count = size;
  }
  memcpy(buffer, input, count);
  input += count;
  return (int)count;
}
void record_event(app_t *app, const char *type, const char *detail) {
  (void)app; (void)detail;
  snprintf(last_event, sizeof(last_event), "%s", type);
}
bool storage_save(app_t *app) { (void)app; saves++; return true; }
void storage_tick(app_t *app) { (void)app; }
void storage_print_logs(app_t *app) { (void)app; }
bool storage_recent(unsigned int index, char *text, size_t capacity) {
  (void)index; (void)text; (void)capacity; return false;
}
bool hw_display(const char lines[4][21]) { (void)lines; return true; }
bool hw_pir(int room) { return presence[room]; }
void hw_gate(int gate, bool open) { (void)gate; (void)open; }
void hw_light(int room, bool on) { (void)room; (void)on; }
void hw_buzzer(bool on) { (void)on; }
void hw_status(unsigned int r, unsigned int g, unsigned int b) {
  (void)r; (void)g; (void)b;
}
void hw_restart(void) {}

static void run(app_t *app, const char *line) {
  input = line;
  while (*input) {
    dashboard_serial(app);
  }
}

int main(void) {
  app_t app = {0};
  app.now = 100;
  app.saved.open_min = 420;
  app.saved.close_min = 1320;
  run(&app, "schedule 08:00 20:00\n");
  assert(strcmp(last_event, "ADMIN_REJECTED") == 0 && saves == 0);
  app.admin_until = 60000;
  run(&app, "schedule 08:00 20:00\n");
  assert(app.saved.open_min == 480 && app.saved.close_min == 1200);
  run(&app, "schedule 99999999999999999999:00 20:00\n");
  assert(app.saved.open_min == 480 && strcmp(last_event, "COMMAND_REJECTED") == 0);
  run(&app, "schedule 08:00 20:00 extra\n");
  assert(strcmp(last_event, "COMMAND_REJECTED") == 0);
  app.saved.building = 2;
  app.saved.rooms[0] = 1;
  run(&app, "room 1 0\n");
  assert(app.saved.rooms[0] == 1);
  run(&app, "room 1 999999999999999999999999 CONFIRM\n");
  assert(app.saved.rooms[0] == 1);
  presence[0] = true;
  run(&app, "room 1 0 CONFIRM\n");
  assert(app.saved.rooms[0] == 1);
  presence[0] = false;
  run(&app, "room 1 0 CONFIRM\n");
  assert(app.saved.rooms[0] == 0);
  app.saved.emergency = true;
  run(&app, "clear-emergency\n");
  assert(app.saved.emergency);
  run(&app, "clear-emergency CONFIRM\n");
  assert(!app.saved.emergency);
  app.now = 60001;
  run(&app, "disable blue\n");
  assert(app.saved.disabled == 0);
  puts("PASS: serial admin authentication/expiry, confirmations, bounded parsing.");
  return 0;
}
