#include "lighting.h"
#include "occupancy.h"
#include "scms_config.h"

#include "driver/gpio.h"

static bool g_r1_on = false;
static bool g_r2_on = false;

void lighting_init(void) {
  gpio_set_direction(PIN_LED_ROOM1, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_LED_ROOM2, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_LED_ROOM1, 0);
  gpio_set_level(PIN_LED_ROOM2, 0);
}

void lighting_update(void) {
  bool next_r1 = occupancy_room1() > 0;
  bool next_r2 = occupancy_room2() > 0;

  g_r1_on = next_r1;
  g_r2_on = next_r2;

  gpio_set_level(PIN_LED_ROOM1, g_r1_on ? 1 : 0);
  gpio_set_level(PIN_LED_ROOM2, g_r2_on ? 1 : 0);
}

bool lighting_room1_on(void) { return g_r1_on; }
bool lighting_room2_on(void) { return g_r2_on; }
