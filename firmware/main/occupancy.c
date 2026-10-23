#include "occupancy.h"

static int g_building = 0;
static int g_room1 = 0;
static int g_room2 = 0;

void occupancy_init(void) {
  g_building = 0;
  g_room1 = 0;
  g_room2 = 0;
}

void occupancy_on_entry(void) {
  g_building++;
}

void occupancy_on_exit(void) {
  if (g_building > 0) g_building--;
}

void occupancy_set_room1(int count) {
  g_room1 = count < 0 ? 0 : count;
}

void occupancy_set_room2(int count) {
  g_room2 = count < 0 ? 0 : count;
}

int occupancy_building(void) { return g_building; }
int occupancy_room1(void) { return g_room1; }
int occupancy_room2(void) { return g_room2; }
