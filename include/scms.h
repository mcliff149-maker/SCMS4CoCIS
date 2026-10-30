#ifndef SCMS_H
#define SCMS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#define SCMS_USERS 3
#define SCMS_ROOMS 2
#define SCMS_SNAPSHOT_VERSION 1
#define SCMS_ADMIN_MS 60000
#define SCMS_PASS_MS 60000
#define SCMS_DOOR_MS 5000
#define SCMS_VACANCY_MS 10000

typedef enum {
  EVENT_NONE, EVENT_ENTRY, EVENT_CP2, EVENT_EXIT, EVENT_ROOM1, EVENT_ROOM2
} zone_t;

typedef struct {
  uint32_t version;
  uint32_t boot;
  uint32_t incident;
  int building;
  int rooms[2];
  uint32_t inside;
  uint32_t disabled;
  bool building_uncertain;
  bool room_uncertain[2];
  bool emergency;
  int open_min;
  int close_min;
  int day;
  unsigned int entries;
  unsigned int unique;
  unsigned int peak;
  unsigned int hours[24];
} saved_t;

typedef struct {
  unsigned int state;
  int direction;
  int64_t since;
} beam_t;

typedef struct {
  int user;
  int64_t until;
  bool door_open;
  bool feedback_open;
  bool door_fault;
  int64_t opened_at;
} gate_t;

typedef struct {
  saved_t saved;
  beam_t beams[5];
  gate_t gates[3];
  int64_t pass_until[SCMS_USERS];
  int64_t admin_until;
  int64_t vacancy_since[2];
  int64_t now;
  time_t utc;
  bool time_valid;
  bool clock_fault;
  bool input_ok;
  bool display_ok;
  bool readers_ok[3];
  bool lights[2];
  bool emergency_input;
  bool fire_input;
  bool dirty;
  bool storage_ok;
  bool nvs_ok;
  unsigned int pending;
  unsigned int lost;
  int last_minute;
  char message[100];
} app_t;

typedef struct {
  uint8_t bytes[10];
  unsigned int size;
} scms_uid_t;

int64_t hw_millis(void);
bool hw_init(void);
bool hw_inputs(uint16_t *value);
bool hw_pir(int room);
bool hw_emergency(void);
void hw_gate(int gate, bool open);
void hw_light(int room, bool on);
void hw_buzzer(bool on);
void hw_status(unsigned int r, unsigned int g, unsigned int b);
bool hw_display(const char lines[4][21]);
bool hw_time(time_t *utc);
int hw_card(int reader, scms_uid_t *uid);
bool hw_sd_mount(void);
bool hw_spi_ready(void);
void hw_restart(void);

void storage_init(app_t *app);
bool storage_save(app_t *app);
void storage_tick(app_t *app);
void record_event(app_t *app, const char *type, const char *detail);
void storage_print_logs(app_t *app);
bool storage_recent(unsigned int index, char *text, size_t capacity);

int access_user(const scms_uid_t *uid);
const char *access_name(int user);
bool access_hours(const saved_t *saved, int minute);
void access_scan(app_t *app, int reader, int user);
void access_passage(app_t *app, int zone, int direction);
void access_tick(app_t *app, uint16_t inputs);
void access_cancel(app_t *app);
bool access_admin(const app_t *app);
void access_feedback(app_t *app, bool success);
bool access_schedule(app_t *app, int opening, int closing);

int beam_step(beam_t *beam, unsigned int value, int64_t now);
void occupancy_tick(app_t *app, uint16_t inputs);
bool occupancy_correct(app_t *app, int scope, int count);

void statistics_entry(app_t *app, int user);
void statistics_tick(app_t *app);
void dashboard_tick(app_t *app, uint16_t inputs);
void dashboard_serial(app_t *app);
void safety_tick(app_t *app);
void safety_enter(app_t *app, const char *reason);
bool safety_clear(app_t *app);
void safety_restart(app_t *app);

#endif
