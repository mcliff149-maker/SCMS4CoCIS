#include "scms.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

typedef struct {
  uint16_t bits;
  int64_t time;
} input_sample_t;

static QueueHandle_t input_queue;
static EventGroupHandle_t input_faults;

/* Preserve beam transitions while the owner task is writing SD/NVS or the LCD. */
static void sample_inputs(void *argument) {
  (void)argument;
  uint16_t previous = 0xffff;
  while (true) {
    input_sample_t sample;
    hw_inputs(&sample.bits);
    sample.time = hw_millis();
    if (sample.bits != previous) {
      if (xQueueSend(input_queue, &sample, 0) != pdTRUE) {
        xEventGroupSetBits(input_faults, 1);
      }
      previous = sample.bits;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void app_main(void) {
  static app_t app;
  setenv("TZ", "UTC0", 1);
  tzset();
  app.last_minute = -1;
  app.vacancy_since[0] = -1;
  app.vacancy_since[1] = -1;
  for (int i = 0; i < 3; i++) {
    app.gates[i].user = -1;
  }
  if (uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0) != ESP_OK) {
    puts("CRITICAL: serial input driver unavailable");
  } else {
    uart_vfs_dev_use_driver(UART_NUM_0);
  }
  puts("\nSCMS4CoCIS: single ESP32, two input registers, ESP-IDF C");
  app.input_ok = hw_init();
  storage_init(&app);
  app.now = hw_millis();
  app.emergency_input = hw_emergency();
  uint16_t inputs = 0;
  hw_inputs(&inputs);
  app.fire_input = (inputs & (1u << 13)) != 0;
  if (app.saved.emergency) {
    for (int i = 0; i < 3; i++) {
      hw_gate(i, true);
      app.gates[i].door_open = true;
    }
  }
  safety_tick(&app);
  input_queue = xQueueCreate(64, sizeof(input_sample_t));
  input_faults = xEventGroupCreate();
  bool sampling = input_queue && input_faults &&
      xTaskCreate(sample_inputs, "input_sampler", 2048, NULL, 6, NULL) == pdPASS;
  if (!sampling) {
    app.input_ok = false;
    record_event(&app, "INPUT_TASK_FAULT", "Input capture unavailable; restart after service");
  }
  record_event(&app, "BOOT", "Last-known counts need physical verification; saved logs will replay");
  puts("Demo cards: Blue/Green users; Yellow Admin; Red unregistered.");
  puts("FIRST: scan Yellow at entry; physically check empty; then verify-empty CONFIRM");
  puts("Type help for serial commands. Default hours: 07:00-22:00 EAT.");
  bool watchdog = esp_task_wdt_add(NULL) == ESP_OK;
  if (!watchdog) {
    record_event(&app, "WATCHDOG_FAULT", "Task watchdog subscription failed");
  }
  int64_t next_clock = 0;
  int64_t next_reader = 0;
  int reader = 0;
  bool reader_fault[3] = {false};
  bool previously_open = false;
  bool first_clock = true;
  while (true) {
    if (watchdog) {
      esp_task_wdt_reset();
    }
    app.now = hw_millis();
    if (!sampling) {
      hw_inputs(&inputs);
    }
    if (input_queue) {
      input_sample_t sample;
      int captured = 0;
      while (captured < 32 && xQueueReceive(input_queue, &sample, 0) == pdTRUE) {
        captured++;
        inputs = sample.bits;
        app.now = sample.time;
        app.emergency_input = hw_emergency();
        app.fire_input = (inputs & (1u << 13)) != 0;
        safety_tick(&app);
        occupancy_tick(&app, inputs);
        access_tick(&app, inputs);
      }
    }
    if (input_queue && uxQueueMessagesWaiting(input_queue) > 0) {
      vTaskDelay(1);
      continue;
    }
    app.now = hw_millis();
    if (input_faults && xEventGroupGetBits(input_faults) && app.input_ok) {
      app.input_ok = false;
      app.saved.building_uncertain = true;
      app.saved.room_uncertain[0] = true;
      app.saved.room_uncertain[1] = true;
      record_event(&app, "INPUT_GAP", "Input queue overflow; verify counts and restart");
    }
    app.emergency_input = hw_emergency();
    app.fire_input = (inputs & (1u << 13)) != 0;
    safety_tick(&app);
    if (app.now >= next_clock) {
      bool valid = hw_time(&app.utc);
      if (valid != app.time_valid || first_clock) {
        app.time_valid = valid;
        record_event(&app, valid ? "TIME_VALID" : "TIME_FAULT",
                     valid ? "RTC time available" : "Schedule entry disabled");
      }
      app.time_valid = valid;
      statistics_tick(&app);
      bool open = valid && access_hours(&app.saved, app.last_minute);
      if (open != previously_open || first_clock) {
        record_event(&app, open ? "SCHEDULE_OPEN" : "SCHEDULE_CLOSED",
                     open ? "Entry permitted by time" : "No new ordinary entry; exit independent");
        previously_open = open;
      }
      first_clock = false;
      next_clock = app.now + 1000;
    }
    occupancy_tick(&app, inputs);
    access_tick(&app, inputs);
    if (app.now >= next_reader) {
      scms_uid_t uid;
      int result = hw_card(reader, &uid);
      app.readers_ok[reader] = result >= 0;
      if (result > 0) {
        access_scan(&app, reader, access_user(&uid));
      }
      if ((result < 0) != reader_fault[reader]) {
        reader_fault[reader] = result < 0;
        char detail[50];
        snprintf(detail, sizeof(detail), "Reader%d", reader + 1);
        record_event(&app, result < 0 ? "RFID_FAULT" : "RFID_RESTORED", detail);
      }
      reader = (reader + 1) % 3;
      next_reader = app.now + 40;
    }
    dashboard_serial(&app);
    dashboard_tick(&app, inputs);
    storage_tick(&app);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
