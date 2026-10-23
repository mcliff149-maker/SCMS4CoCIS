#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "scms_config.h"
#include "access_control.h"
#include "occupancy.h"
#include "lighting.h"
#include "scheduler.h"
#include "logger.h"
#include "serial_menu.h"
#include "sync_bus.h"

static void io_init(void) {
  gpio_set_direction(PIN_BTN_ENTRY, GPIO_MODE_INPUT);
  gpio_set_direction(PIN_BTN_EXIT, GPIO_MODE_INPUT);
  gpio_set_direction(PIN_BTN_R1, GPIO_MODE_INPUT);
  gpio_set_direction(PIN_BTN_R2, GPIO_MODE_INPUT);

  gpio_set_pull_mode(PIN_BTN_ENTRY, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(PIN_BTN_EXIT, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(PIN_BTN_R1, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(PIN_BTN_R2, GPIO_PULLUP_ONLY);

  gpio_set_direction(PIN_LED_ACCESS, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_LED_ACCESS, 0);
}

void app_main(void) {
  printf("\n=== SCMS4CoCIS Simulation Start ===\n");

  io_init();
  logger_init();
  schedule_init();
  occupancy_init();
  access_init();
  lighting_init();
  sync_bus_init();

  serial_menu_print_help();

  int room1_count = 0;
  int room2_count = 0;
  int sim_hour = 10; // default daytime for early demos

  int prev_entry = 1, prev_exit = 1, prev_r1 = 1, prev_r2 = 1;

  while (1) {
    int entry_now = gpio_get_level(PIN_BTN_ENTRY);
    int exit_now  = gpio_get_level(PIN_BTN_EXIT);
    int r1_now    = gpio_get_level(PIN_BTN_R1);
    int r2_now    = gpio_get_level(PIN_BTN_R2);

    if (prev_entry == 1 && entry_now == 0) {
      scms_access_result_t r = access_attempt(1001, SCMS_DIR_ENTRY, sim_hour);
      gpio_set_level(PIN_LED_ACCESS, r == SCMS_ACCESS_GRANTED ? 1 : 0);
      printf("ENTRY scan user=1001 => %s | building=%d\n", access_result_str(r), occupancy_building());
    }

    if (prev_exit == 1 && exit_now == 0) {
      scms_access_result_t r = access_attempt(1001, SCMS_DIR_EXIT, sim_hour);
      gpio_set_level(PIN_LED_ACCESS, r == SCMS_ACCESS_GRANTED ? 1 : 0);
      printf("EXIT  scan user=1001 => %s | building=%d\n", access_result_str(r), occupancy_building());
    }

    if (prev_r1 == 1 && r1_now == 0) {
      room1_count = (room1_count == 0) ? 1 : 0;
      occupancy_set_room1(room1_count);
      lighting_update();
      printf("ROOM1 occupancy=%d light=%s\n", occupancy_room1(), lighting_room1_on() ? "ON" : "OFF");
    }

    if (prev_r2 == 1 && r2_now == 0) {
      room2_count = (room2_count == 0) ? 1 : 0;
      occupancy_set_room2(room2_count);
      lighting_update();
      printf("ROOM2 occupancy=%d light=%s\n", occupancy_room2(), lighting_room2_on() ? "ON" : "OFF");
    }

    prev_entry = entry_now;
    prev_exit = exit_now;
    prev_r1 = r1_now;
    prev_r2 = r2_now;

    serial_menu_poll();
    sync_bus_tick();

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
