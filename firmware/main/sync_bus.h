#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"

#define SYNC_LINE_SIZE 192
#define SYNC_TERMINALS 3

typedef struct {
  char line[SYNC_LINE_SIZE];
  unsigned terminal;
  int button;
} sync_request_t;

bool sync_bus_init(void);
bool sync_bus_submit(const char *line);
bool sync_bus_button(unsigned button);
bool sync_bus_receive(sync_request_t *request, TickType_t wait);
void sync_bus_complete(void);
bool sync_bus_online(unsigned terminal);
void sync_bus_set_online(unsigned terminal, bool online);
