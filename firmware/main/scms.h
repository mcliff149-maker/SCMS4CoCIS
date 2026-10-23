#pragma once

#include <stdbool.h>
#include <stdint.h>

/* All model calls belong to one state-owner task; sinks run synchronously there. */
void scms_init(void);
void scms_execute(const char *line);
void scms_button(unsigned terminal, uint32_t selected_id);
uint32_t scms_selected_id(void);
bool scms_light(unsigned room);
unsigned scms_building(void);
unsigned scms_minute(void);
void scms_set_storage(bool available);
void scms_set_log_sink(void (*sink)(const char *));
/* A failed persistence callback rejects the schedule change. */
void scms_set_schedule_sink(bool (*sink)(unsigned open, unsigned close));
bool scms_restore_schedule(unsigned open, unsigned close);
void scms_set_access_sink(void (*sink)(int direction, bool granted));

/* Read-only diagnostics, also used by deterministic host tests. */
unsigned scms_room(unsigned room);
unsigned scms_user_room(uint32_t id);
bool scms_user_inside(uint32_t id);
bool scms_authenticated(void);
bool scms_storage_available(void);
uint64_t scms_day(void);
uint64_t scms_energy_watt_minutes(unsigned room);
uint64_t scms_elapsed_minutes(void);
uint64_t scms_light_on_minutes(unsigned room);
uint64_t scms_saved_watt_minutes(unsigned room);
