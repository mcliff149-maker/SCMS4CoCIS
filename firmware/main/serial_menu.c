#include "serial_menu.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

static char line[SERIAL_MENU_LINE_CAPACITY];
static size_t used;
static bool discard;
static bool previous_cr;

void serial_menu_init(void) {
  used = 0;
  discard = false;
  previous_cr = false;
}

void serial_menu_feed(char c, void (*submit)(const char *)) {
  if (c == '\n' && previous_cr) {
    previous_cr = false;
    return;
  }
  previous_cr = c == '\r';
  if (c == '\r' || c == '\n') {
    if (discard) puts("ERROR: invalid or oversized frame discarded.");
    if (!discard && used && submit) {
      line[used] = '\0';
      submit(line);
    }
    used = 0;
    discard = false;
    return;
  }
  /* Binary/control input invalidates the entire frame, never a command prefix. */
  if ((unsigned char)c < 32 && c != '\t') discard = true;
  if ((unsigned char)c == 127) discard = true;
  if (discard) return;
  if (used == sizeof(line) - 1) {
    discard = true;
    return;
  }
  line[used++] = c;
}

void serial_menu_print_help(void) {
  puts("help | status | users | who | logs [N] | storage_logs\n"
       "scan_entry <id> | scan_exit <id> | select <id> | button <0..3>\n"
       "room <1|2> <id> <in|out> | time HH:MM | advance <0..10080>\n"
       "login admin coursework | logout | set_schedule HH:MM HH:MM\n"
       "Demo credentials only; not production authentication. Same-day [open,close).");
}
