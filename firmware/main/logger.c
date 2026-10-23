#include "logger.h"
#include "scms_types.h"

#include <stdio.h>
#include <string.h>

static char g_logs[SCMS_MAX_LOGS][96];
static int g_log_count = 0;

void logger_init(void) {
  g_log_count = 0;
}

void log_event(const char* msg) {
  if (!msg) return;
  int idx = g_log_count % SCMS_MAX_LOGS;
  strncpy(g_logs[idx], msg, sizeof(g_logs[idx]) - 1);
  g_logs[idx][sizeof(g_logs[idx]) - 1] = '\0';
  g_log_count++;
}

void log_print_recent(int max_items) {
  if (max_items <= 0) return;
  int total = g_log_count < SCMS_MAX_LOGS ? g_log_count : SCMS_MAX_LOGS;
  if (max_items > total) max_items = total;

  printf("--- Recent Logs (%d) ---\n", max_items);
  for (int i = total - max_items; i < total; i++) {
    printf("[%d] %s\n", i, g_logs[i]);
  }
}
