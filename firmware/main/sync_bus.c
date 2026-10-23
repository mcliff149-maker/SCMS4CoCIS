#include "sync_bus.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static QueueHandle_t incoming;
static QueueHandle_t owner;
static QueueHandle_t terminals[SYNC_TERMINALS];
static SemaphoreHandle_t completed;
/* These flags are read and written only by the authoritative owner. */
static bool online[SYNC_TERMINALS] = {true, true, true};

static unsigned route(const char *line) {
  char command[SYNC_LINE_SIZE], argument[SYNC_LINE_SIZE];
  if (sscanf(line, "%191s %191s", command, argument) != 2)
    return 0;
  const char *number = argument;
  if (*number == '+')
    ++number;
  /* Classify equivalent decimal spellings without numeric conversion overflow. */
  while (*number == '0' && number[1])
    ++number;
  if (strcmp(command, "room") == 0) {
    if (strcmp(number, "1") == 0)
      return 1;
    if (strcmp(number, "2") == 0)
      return 2;
  }
  if (strcmp(command, "button") == 0) {
    if (strcmp(number, "2") == 0)
      return 1;
    if (strcmp(number, "3") == 0)
      return 2;
  }
  return 0;
}

static void terminal_task(void *argument) {
  unsigned terminal = (unsigned)(uintptr_t)argument;
  sync_request_t request;
  for (;;) {
    if (xQueueReceive(terminals[terminal], &request, portMAX_DELAY) == pdTRUE) {
      /* Only one request is in flight, so the owner queue cannot be full. */
      if (xQueueSend(owner, &request, 0) != pdTRUE) {
        printf("ERROR owner queue full; request rejected\n");
        xSemaphoreGive(completed);
      }
    }
  }
}

static void dispatch_task(void *argument) {
  (void)argument;
  sync_request_t request;
  for (;;) {
    if (xQueueReceive(incoming, &request, portMAX_DELAY) != pdTRUE)
      continue;
    if (xQueueSend(terminals[request.terminal], &request, 0) != pdTRUE) {
      printf("ERROR terminal %u queue full; request rejected\n", request.terminal);
      continue;
    }
    /* Do not dispatch later commands ahead of an unfinished earlier command.
       A timeout is only a poll: it never retries or invents an acknowledgement. */
    while (xSemaphoreTake(completed, pdMS_TO_TICKS(100)) != pdTRUE) {
      vTaskDelay(1);
    }
  }
}

bool sync_bus_init(void) {
  incoming = xQueueCreate(16, sizeof(sync_request_t));
  owner = xQueueCreate(1, sizeof(sync_request_t));
  completed = xSemaphoreCreateBinary();
  for (unsigned i = 0; i < SYNC_TERMINALS; ++i)
    terminals[i] = xQueueCreate(1, sizeof(sync_request_t));
  if (!incoming || !owner || !completed)
    return false;
  for (unsigned i = 0; i < SYNC_TERMINALS; ++i) {
    if (!terminals[i] ||
        xTaskCreate(terminal_task, i == 0 ? "entrance" : i == 1 ? "room1" : "room2",
                    3072, (void *)(uintptr_t)i, 5, NULL) != pdPASS)
      return false;
  }
  return xTaskCreate(dispatch_task, "dispatcher", 3072, NULL, 5, NULL) == pdPASS;
}

bool sync_bus_submit(const char *line) {
  if (!line || strlen(line) >= SYNC_LINE_SIZE) {
    printf("ERROR command too long\n");
    return false;
  }
  sync_request_t request = {.terminal = route(line), .button = -1};
  memcpy(request.line, line, strlen(line) + 1);
  if (xQueueSend(incoming, &request, 0) != pdTRUE) {
    printf("ERROR input queue full; command rejected\n");
    return false;
  }
  return true;
}

bool sync_bus_button(unsigned button) {
  if (button > 3)
    return false;
  sync_request_t request = {
    .terminal = button < 2 ? 0 : button - 1, .button = (int)button
  };
  if (xQueueSend(incoming, &request, 0) != pdTRUE) {
    printf("ERROR input queue full; button rejected\n");
    return false;
  }
  return true;
}

bool sync_bus_receive(sync_request_t *request, TickType_t wait) {
  return xQueueReceive(owner, request, wait) == pdTRUE;
}

void sync_bus_complete(void) {
  xSemaphoreGive(completed);
}

bool sync_bus_online(unsigned terminal) {
  return terminal < SYNC_TERMINALS && online[terminal];
}

void sync_bus_set_online(unsigned terminal, bool available) {
  if (terminal < SYNC_TERMINALS)
    online[terminal] = available;
}
