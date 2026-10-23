#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "scms.h"
#include "scms_config.h"
#include "serial_menu.h"
#include "storage.h"
#include "sync_bus.h"

static const gpio_num_t buttons[] = {
  PIN_BTN_ENTRY, PIN_BTN_EXIT, PIN_BTN_R1, PIN_BTN_R2
};
static int64_t entry_until;
static int64_t exit_until;
static int64_t denied_until;
static char pending_logs[32][SYNC_LINE_SIZE];
static unsigned pending_read;
static unsigned pending_count;

static void io_init(void) {
  gpio_config_t inputs = {
    .pin_bit_mask = (1ULL << PIN_BTN_ENTRY) | (1ULL << PIN_BTN_EXIT) |
                    (1ULL << PIN_BTN_R1) | (1ULL << PIN_BTN_R2),
    .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE
  };
  ESP_ERROR_CHECK(gpio_config(&inputs));
  gpio_config_t outputs = {
    .pin_bit_mask = (1ULL << PIN_LED_ENTRY) | (1ULL << PIN_LED_EXIT) |
                    (1ULL << PIN_LED_DENIED) | (1ULL << PIN_LED_ROOM1) |
                    (1ULL << PIN_LED_ROOM2),
    .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE
  };
  ESP_ERROR_CHECK(gpio_config(&outputs));
  gpio_set_level(PIN_LED_ENTRY, 0);
  gpio_set_level(PIN_LED_EXIT, 0);
  gpio_set_level(PIN_LED_DENIED, 0);
  gpio_set_level(PIN_LED_ROOM1, 0);
  gpio_set_level(PIN_LED_ROOM2, 0);
}

static void access_signal(int direction, bool granted) {
  int64_t until = esp_timer_get_time() + SCMS_DOOR_PULSE_MS * 1000;
  if (!granted)
    denied_until = until;
  else if (direction == 0)
    entry_until = until;
  else
    exit_until = until;
  gpio_set_level(!granted ? PIN_LED_DENIED :
                 direction == 0 ? PIN_LED_ENTRY : PIN_LED_EXIT, 1);
}

static void update_outputs(void) {
  int64_t now = esp_timer_get_time();
  gpio_set_level(PIN_LED_ENTRY, now < entry_until);
  gpio_set_level(PIN_LED_EXIT, now < exit_until);
  gpio_set_level(PIN_LED_DENIED, now < denied_until);
  gpio_set_level(PIN_LED_ROOM1, scms_light(1));
  gpio_set_level(PIN_LED_ROOM2, scms_light(2));
}

static void log_sink(const char *line) {
  /* File I/O is deferred until access pulses finish, so slow SD writes cannot
     hold a door indicator on beyond its monotonic deadline. */
  if (pending_count == 32) {
    printf("WARN pending log buffer full; oldest event discarded\n");
    pending_read = (pending_read + 1) % 32;
    --pending_count;
  }
  snprintf(pending_logs[(pending_read + pending_count) % 32],
           SYNC_LINE_SIZE, "%s", line);
  ++pending_count;
}

static void flush_log(void) {
  if (!pending_count)
    return;
  if (!storage_append(pending_logs[pending_read]))
    scms_set_storage(false);
  pending_read = (pending_read + 1) % 32;
  --pending_count;
}

static void submit_line(const char *line) {
  (void)sync_bus_submit(line);
}

static void input_task(void *argument) {
  (void)argument;
  bool raw[4] = {true, true, true, true};
  bool stable[4] = {true, true, true, true};
  int64_t changed[4] = {0};
  serial_menu_init();
  for (;;) {
    uint8_t bytes[64];
    int count = uart_read_bytes(UART_NUM_0, bytes, sizeof(bytes), 0);
    for (int i = 0; i < count; ++i)
      serial_menu_feed((char)bytes[i], submit_line);
    int64_t now = esp_timer_get_time();
    for (unsigned i = 0; i < 4; ++i) {
      bool level = gpio_get_level(buttons[i]) != 0;
      if (level != raw[i]) {
        raw[i] = level;
        changed[i] = now;
      }
      if (stable[i] != raw[i] &&
          now - changed[i] >= SCMS_BUTTON_DEBOUNCE_MS * 1000) {
        stable[i] = raw[i];
        if (!stable[i])
          (void)sync_bus_button(i);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static bool runtime_command(const char *line) {
  char command[24] = {0};
  if (sscanf(line, "%23s", command) != 1)
    return false;
  if (strcmp(command, "terminal") == 0) {
    char id[8], state[8], extra;
    if (sscanf(line, "%*s %7s %7s %c", id, state, &extra) != 2 ||
        id[0] < '0' || id[0] > '2' || id[1] != '\0' ||
        (strcmp(state, "on") != 0 && strcmp(state, "off") != 0)) {
      printf("ERROR usage: terminal <0|1|2> <on|off>\n");
    } else {
      unsigned terminal = (unsigned)(id[0] - '0');
      sync_bus_set_online(terminal, strcmp(state, "on") == 0);
      char event[64];
      snprintf(event, sizeof(event), "TERMINAL terminal=%u state=%s", terminal, state);
      log_event(event);
      printf("terminal %u %s (logical FreeRTOS worker)\n", terminal, state);
    }
    return true;
  }
  if (strcmp(command, "storage_logs") == 0) {
    unsigned limit = 50;
    char argument[24], extra;
    int count = sscanf(line, "%*s %23s %c", argument, &extra);
    bool valid = count < 1;
    if (count == 1) {
      char *end;
      unsigned long value = strtoul(argument, &end, 10);
      valid = argument[0] >= '0' && argument[0] <= '9' && *end == '\0' &&
              value >= 1 && value <= 50;
      if (valid)
        limit = (unsigned)value;
    }
    if (!valid) {
      printf("ERROR usage: storage_logs [1..50]\n");
    } else {
      while (pending_count)
        flush_log();
      storage_print_logs(limit);
      scms_set_storage(storage_available());
    }
    return true;
  }
  return false;
}

static bool terminal_event(const char *line) {
  char command[24];
  if (sscanf(line, "%23s", command) != 1)
    return false;
  return strcmp(command, "scan_entry") == 0 || strcmp(command, "scan_exit") == 0 ||
         strcmp(command, "room") == 0 || strcmp(command, "button") == 0;
}

void app_main(void) {
  /* app_main is the sole model/GPIO-output owner; workers only move messages. */
  printf("\nSCMS: one ESP32, three logical terminal tasks, one state owner\n");
  io_init();
  scms_init();
  storage_init();
  scms_set_storage(storage_available());
  unsigned open, close;
  if (storage_schedule_load(&open, &close))
    (void)scms_restore_schedule(open, close);
  scms_set_log_sink(log_sink);
  scms_set_schedule_sink(storage_schedule_save);
  scms_set_access_sink(access_signal);
  uart_config_t uart = {
    .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT
  };
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart));
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0));
  if (!sync_bus_init() ||
      xTaskCreate(input_task, "uart_buttons", 4096, NULL, 4, NULL) != pdPASS) {
    printf("FATAL task/queue allocation failed; restarting\n");
    abort();
  }
  printf("storage=%s schedule_nvs=%s; help for commands\n",
         storage_available() ? "SD" : "VOLATILE",
         storage_nvs_available() ? "ready" : "unavailable");
  printf("runtime commands: terminal <0|1|2> <on|off>, storage_logs [1..50]\n");
  for (;;) {
    sync_request_t request;
    update_outputs();
    int64_t now = esp_timer_get_time();
    if (now < entry_until || now < exit_until || now < denied_until) {
      /* Input remains nonblocking and bounded while the owner finishes a pulse.
         No blocking NVS/SD command runs with an active access indicator. */
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (pending_count) {
      flush_log();
      continue;
    }
    if (!sync_bus_receive(&request, pdMS_TO_TICKS(10)))
      continue;
    if (request.button < 0 && runtime_command(request.line)) {
      /* Recovery commands remain available even when the entrance is offline. */
    } else if ((request.button >= 0 || terminal_event(request.line)) &&
               !sync_bus_online(request.terminal)) {
      char event[64];
      snprintf(event, sizeof(event), "TERMINAL terminal=%u request=offline_rejected",
               request.terminal);
      log_event(event);
      printf("ERROR terminal %u offline; request rejected\n", request.terminal);
    } else if (request.button >= 0) {
      scms_button((unsigned)request.button, scms_selected_id());
    } else {
      scms_execute(request.line);
      char command[24] = {0};
      (void)sscanf(request.line, "%23s", command);
      if (strcmp(command, "status") == 0)
        printf("runtime terminals=0:%s 1:%s 2:%s storage=%s schedule_nvs=%s\n",
               sync_bus_online(0) ? "on" : "off",
               sync_bus_online(1) ? "on" : "off",
               sync_bus_online(2) ? "on" : "off",
               storage_available() ? "SD" : "VOLATILE",
               storage_nvs_available() ? "ready" : "unavailable");
      if (strcmp(command, "help") == 0)
        printf("terminal <0|1|2> <on|off>; storage_logs [1..50]\n");
    }
    update_outputs();
    sync_bus_complete();
  }
}
