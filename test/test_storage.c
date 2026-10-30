#include "scms.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char saved_blob[16000];
static size_t saved_size;
static bool mounted;
static bool fail_commit;

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
  (void)name; (void)mode; *handle = 1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size) {
  (void)handle; (void)key;
  if (saved_size == 0) { return ESP_ERR_NVS_NOT_FOUND; }
  assert(*size >= saved_size);
  *size = saved_size;
  memcpy(value, saved_blob, saved_size);
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                       const void *value, size_t size) {
  (void)handle; (void)key;
  assert(size < sizeof(saved_blob));
  saved_size = size;
  memcpy(saved_blob, value, size);
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return fail_commit ? 2 : ESP_OK; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "TEST_FAULT"; }
bool hw_sd_mount(void) { return mounted; }

static unsigned int file_rows(void) {
  FILE *file = fopen(SCMS_LOG_PATH, "r");
  assert(file);
  unsigned int rows = 0;
  char line[256];
  while (fgets(line, sizeof(line), file)) {
    assert(strchr(line, '\n') != NULL);
    rows++;
  }
  fclose(file);
  return rows;
}

int main(void) {
  app_t app = {0};
  storage_init(&app);
  assert(app.nvs_ok && app.saved.boot == 1 && app.saved.building_uncertain);
  app.saved.building = 2;
  app.saved.emergency = true;
  record_event(&app, "TEST", "Persisted pending event");
  assert(app.pending == 1);
  storage_tick(&app);
  assert(app.pending == 1 && !app.storage_ok);

  app_t recovered = {0};
  storage_init(&recovered);
  assert(recovered.saved.boot == 2 && recovered.saved.building == 2);
  assert(recovered.saved.emergency && recovered.pending == 1);
  mounted = true;
  recovered.now = 6000;
  fail_commit = true;
  storage_tick(&recovered);
  assert(recovered.pending == 1 && !recovered.nvs_ok);
  assert(file_rows() == 1);
  fail_commit = false;
  recovered.now = 12000;
  storage_tick(&recovered);
  assert(recovered.pending == 0 && recovered.nvs_ok);
  assert(file_rows() == 1);

  FILE *file = fopen(SCMS_LOG_PATH, "a");
  assert(file);
  fputs("partial interrupted tail", file);
  fclose(file);
  record_event(&recovered, "TAIL_RECOVERY", "New event after interrupted SD write");
  recovered.now = 18000;
  storage_tick(&recovered);
  assert(recovered.pending == 0 && file_rows() == 2);

  for (int i = 0; i < 33; i++) {
    record_event(&recovered, "QUEUE_TEST", "Bounded queue");
  }
  assert(recovered.pending == 32 && recovered.lost == 1);
  assert(recovered.saved.building_uncertain);
  puts("PASS: durable queue restore, emergency/counter restore, replay deduplication,");
  puts("      NVS commit failure, incomplete SD tail repair and explicit overflow.");
  return 0;
}
