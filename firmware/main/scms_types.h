#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SCMS_MAX_USERS 64
#define SCMS_MAX_LOGS 256

typedef enum {
  SCMS_DIR_ENTRY = 0,
  SCMS_DIR_EXIT  = 1
} scms_direction_t;

typedef enum {
  SCMS_ACCESS_GRANTED = 0,
  SCMS_DENY_UNKNOWN_ID,
  SCMS_DENY_ALREADY_INSIDE,
  SCMS_DENY_NOT_INSIDE,
  SCMS_DENY_SCHEDULE_CLOSED
} scms_access_result_t;

typedef struct {
  uint32_t user_id;
  bool is_authorized;
  bool is_inside;
} scms_user_t;

typedef struct {
  int building_occupancy;
  int room1_occupancy;
  int room2_occupancy;
  bool room1_light_on;
  bool room2_light_on;
} scms_state_t;
