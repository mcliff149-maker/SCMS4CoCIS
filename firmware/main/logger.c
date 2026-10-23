#include "logger.h"
#include "scms_types.h"

#include <stdio.h>
#include <string.h>

static char g_logs[SCMS_MAX_LOGS][192];
static size_t g_count;
static size_t g_next;
static void (*g_sink)(const char *);

void logger_init(void) {
  memset(g_logs, 0, sizeof(g_logs));
  g_count = 0;
  g_next = 0;
  g_sink = NULL;
}

void log_event(const char* msg) {
  if (!msg) return;
  size_t idx = g_next;
  strncpy(g_logs[idx], msg, sizeof(g_logs[idx]) - 1);
  g_logs[idx][sizeof(g_logs[idx]) - 1] = '\0';
  g_next = (g_next + 1) % SCMS_MAX_LOGS;
  if (g_count < SCMS_MAX_LOGS) g_count++;
  if (g_sink) g_sink(g_logs[idx]);
}

size_t logger_count(void) { return g_count; }
const char *logger_get(size_t index) {
  if (index >= g_count) return NULL;
  return g_logs[(g_next + SCMS_MAX_LOGS - g_count + index) % SCMS_MAX_LOGS];
}
void logger_set_sink(void (*sink)(const char *)) { g_sink = sink; }

void log_print_recent(int max_items) {
  if (max_items <= 0) return;
  int total = (int)g_count;
  if (max_items > total) max_items = total;

  printf("--- Recent Logs (%d) ---\n", max_items);
  for (int i = total - max_items; i < total; i++) {
    printf("[%d] %s\n", i, logger_get((size_t)i));
  }
}
