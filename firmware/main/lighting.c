#include "lighting.h"
#include "occupancy.h"

static bool g_r1_on = false;
static bool g_r2_on = false;

void lighting_init(void) {
  g_r1_on = false;
  g_r2_on = false;
}

void lighting_update(void) {
  bool next_r1 = occupancy_room1() > 0;
  bool next_r2 = occupancy_room2() > 0;

  g_r1_on = next_r1;
  g_r2_on = next_r2;

}

bool lighting_room1_on(void) { return g_r1_on; }
bool lighting_room2_on(void) { return g_r2_on; }
