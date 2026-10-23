#pragma once

#include "scms_types.h"

void access_init(void);
scms_access_result_t access_attempt(uint32_t user_id, scms_direction_t dir, int minute_of_day);
const char* access_result_str(scms_access_result_t r);
