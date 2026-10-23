#pragma once

#define SERIAL_MENU_LINE_CAPACITY 128

/* Input-task-only framing; submit must copy the borrowed line before returning. */
void serial_menu_init(void);
void serial_menu_feed(char c, void (*submit)(const char *));
void serial_menu_print_help(void);
