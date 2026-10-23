#include "serial_menu.h"
#include "occupancy.h"
#include "lighting.h"
#include "scheduler.h"
#include "logger.h"

#include <stdio.h>

void serial_menu_print_help(void) {
  printf("\nCommands:\n");
  printf("  help\n");
  printf("  status\n");
  printf("  logs\n");
  printf("  set_schedule <open> <close>\n");
  printf("\n");
}

void serial_menu_poll(void) {
  // Placeholder: in Wokwi, stdin command parsing can be added incrementally.
  // Keep non-blocking skeleton for now.
}
