#include "scms.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include "nvs.h"
#include "nvs_flash.h"

#define QUEUE_SIZE 32
#ifndef SCMS_LOG_PATH
#define SCMS_LOG_PATH "/sdcard/events.csv"
#endif

typedef struct {
  char text[240];
  long offset;
} log_item_t;

static nvs_handle_t handle;
static bool nvs_opened;
static log_item_t queue[QUEUE_SIZE];
static unsigned int head;
static unsigned int size;
static uint32_t sequence;
static int64_t next_try;
static int64_t next_save;

typedef struct {
  saved_t state;
  uint32_t sequence;
  unsigned int lost;
  unsigned int count;
  log_item_t items[QUEUE_SIZE];
} checkpoint_t;

static checkpoint_t checkpoint;
static char recent[8][100];
static unsigned int recent_count;
static unsigned int recent_head;

bool storage_recent(unsigned int index, char *text, size_t capacity) {
  if (index >= recent_count) {
    return false;
  }
  unsigned int slot = (recent_head + 7 - index) % 8;
  snprintf(text, capacity, "%s", recent[slot]);
  return true;
}

void storage_print_logs(app_t *app) {
  if (!app->storage_ok) {
    puts("SD unavailable. Latest in-memory events:");
    char text[100];
    for (unsigned int i = 0; storage_recent(i, text, sizeof(text)); i++) {
      puts(text);
    }
    return;
  }
  FILE *file = fopen(SCMS_LOG_PATH, "r");
  if (!file) {
    record_event(app, "LOG_READ_FAULT", "Could not open SD event log");
    return;
  }
  /* Bound serial output; start at a whole record near the end. */
  bool ok = fseek(file, 0, SEEK_END) == 0;
  long length = ok ? ftell(file) : -1;
  char line[240];
  if (length < 0 || fseek(file, length > 4000 ? length - 4000 : 0, SEEK_SET) != 0) {
    ok = false;
  } else {
    if (length > 4000) {
      ok = fgets(line, sizeof(line), file) != NULL;
    }
    while (ok && fgets(line, sizeof(line), file)) {
      printf("%s", line);
    }
    ok = ok && !ferror(file);
  }
  if (fclose(file) != 0) {
    ok = false;
  }
  if (!ok) {
    record_event(app, "LOG_READ_FAULT", "SD audit read failed");
  }
}

static bool valid_saved(const saved_t *saved) {
  return saved->version == SCMS_SNAPSHOT_VERSION &&
    saved->building >= 0 && saved->building <= 1000 &&
    saved->rooms[0] >= 0 && saved->rooms[0] <= 1000 &&
    saved->rooms[1] >= 0 && saved->rooms[1] <= 1000 &&
    saved->open_min >= 0 && saved->open_min < 1440 &&
    saved->close_min >= 0 && saved->close_min < 1440 &&
    saved->open_min != saved->close_min &&
    (saved->inside & ~7u) == 0 && (saved->disabled & ~7u) == 0;
}

void record_event(app_t *app, const char *type, const char *detail) {
  char text[240];
  sequence++;
  snprintf(text, sizeof(text), "%" PRIu32 ":%" PRIu32 ",%lld,%s,%s\n",
           app->saved.boot, sequence,
           app->time_valid ? (long long)app->utc : 0LL, type, detail);
  printf("%s", text);
  snprintf(app->message, sizeof(app->message), "%.25s %.70s", type, detail);
  snprintf(recent[recent_head], sizeof(recent[recent_head]), "%s", app->message);
  recent_head = (recent_head + 1) % 8;
  if (recent_count < 8) {
    recent_count++;
  }
  if (size == QUEUE_SIZE) {
    app->lost++;
    app->storage_ok = false;
    app->saved.building_uncertain = true;
    app->dirty = true;
    printf("CRITICAL LOG GAP: queue full; event %" PRIu32 " not retained\n", sequence);
    return;
  }
  unsigned int slot = (head + size) % QUEUE_SIZE;
  snprintf(queue[slot].text, sizeof(queue[slot].text), "%s", text);
  queue[slot].offset = -1;
  size++;
  app->pending = size;
  app->dirty = true;
  /* Counts, latch and pending audit events commit as one NVS blob. */
  storage_save(app);
}

bool storage_save(app_t *app) {
  if (!nvs_opened) {
    app->nvs_ok = false;
    return false;
  }
  checkpoint.state = app->saved;
  checkpoint.sequence = sequence;
  checkpoint.lost = app->lost;
  checkpoint.count = size;
  for (unsigned int i = 0; i < size; i++) {
    checkpoint.items[i] = queue[(head + i) % QUEUE_SIZE];
  }
  esp_err_t error = nvs_set_blob(handle, "checkpoint",
                                &checkpoint, sizeof(checkpoint));
  if (error == ESP_OK) {
    error = nvs_commit(handle);
  }
  if (error != ESP_OK) {
    if (app->nvs_ok) {
      printf("NVS FAULT: %s; ordinary entry disabled\n", esp_err_to_name(error));
    }
    app->nvs_ok = false;
    return false;
  }
  app->nvs_ok = true;
  app->dirty = false;
  return true;
}

void storage_init(app_t *app) {
  app->saved.version = SCMS_SNAPSHOT_VERSION;
  app->saved.open_min = 7 * 60;
  app->saved.close_min = 22 * 60;
  app->saved.building_uncertain = true;
  app->saved.room_uncertain[0] = true;
  app->saved.room_uncertain[1] = true;
  esp_err_t error = nvs_flash_init();
  if (error == ESP_OK) {
    error = nvs_open("scms", NVS_READWRITE, &handle);
  }
  if (error != ESP_OK) {
    printf("NVS FAULT: %s. Not erasing existing data automatically.\n",
           esp_err_to_name(error));
    return;
  }
  nvs_opened = true;
  size_t bytes = sizeof(checkpoint);
  error = nvs_get_blob(handle, "checkpoint", &checkpoint, &bytes);
  if (error == ESP_OK && bytes == sizeof(checkpoint) &&
      valid_saved(&checkpoint.state) && checkpoint.count <= QUEUE_SIZE) {
    app->saved = checkpoint.state;
    app->lost = checkpoint.lost;
    size = checkpoint.count;
    for (unsigned int i = 0; i < size; i++) {
      queue[i] = checkpoint.items[i];
      queue[i].text[sizeof(queue[i].text) - 1] = '\0';
      queue[i].offset = -1;
    }
    app->pending = size;
    printf("Recovered last-known state and %u pending logs; verify occupancy.\n", size);
  } else if (error != ESP_ERR_NVS_NOT_FOUND) {
    printf("NVS FAULT: invalid/unreadable snapshot; entry disabled pending service\n");
    nvs_opened = false;
    return;
  }
  /* Activity during any outage is unknowable, even with a valid saved count. */
  app->saved.building_uncertain = true;
  app->saved.room_uncertain[0] = true;
  app->saved.room_uncertain[1] = true;
  app->saved.boot++;
  app->dirty = true;
  storage_save(app);
}

static bool append_one(log_item_t *item) {
  FILE *file = fopen(SCMS_LOG_PATH, "r+");
  if (!file && errno == ENOENT) {
    file = fopen(SCMS_LOG_PATH, "w+");
  }
  if (!file) {
    return false;
  }
  bool ok = true;
  if (item->offset < 0) {
    char line[240];
    const char *comma = strchr(item->text, ',');
    if (!comma) {
      fclose(file);
      return false;
    }
    size_t id_length = (size_t)(comma - item->text) + 1;
    while (true) {
      long start = ftell(file);
      if (start < 0) {
        ok = false;
        break;
      }
      if (!fgets(line, sizeof(line), file)) {
        ok = !ferror(file);
        item->offset = start;
        break;
      }
      size_t length = strlen(line);
      if (length == 0 || line[length - 1] != '\n') {
        if (!feof(file)) {
          ok = false;
          break;
        }
        printf("SD RECOVERY: trimming incomplete final audit record\n");
        ok = ftruncate(fileno(file), start) == 0;
        item->offset = start;
        break;
      }
      if (strncmp(line, item->text, id_length) == 0) {
        /* Replayed records never update occupancy; this prevents duplicate audit rows. */
        return fclose(file) == 0;
      }
    }
    if (ok) {
      ok = fseek(file, item->offset, SEEK_SET) == 0;
    }
  } else {
    /* A retry replaces a potentially partial write of this same queue item. */
    ok = ftruncate(fileno(file), item->offset) == 0 &&
         fseek(file, item->offset, SEEK_SET) == 0;
  }
  if (ok) {
    size_t count = strlen(item->text);
    ok = fwrite(item->text, 1, count, file) == count &&
         fflush(file) == 0 && fsync(fileno(file)) == 0;
  }
  if (fclose(file) != 0) {
    ok = false;
  }
  return ok;
}

void storage_tick(app_t *app) {
  if (app->dirty && app->now >= next_save) {
    storage_save(app);
    next_save = app->now + 1000;
  }
  if (app->now < next_try) {
    return;
  }
  if (!app->storage_ok) {
    if (!hw_sd_mount()) {
      next_try = app->now + 5000;
      return;
    }
  }
  if (size > 0 && !append_one(&queue[head])) {
    if (app->storage_ok) {
      printf("SD FAULT: write failed; records remain queued\n");
      snprintf(app->message, sizeof(app->message), "SD FAULT: records queued");
    }
    app->storage_ok = false;
    next_try = app->now + 5000;
    return;
  }
  bool recovered = !app->storage_ok;
  app->storage_ok = true;
  if (size > 0) {
    unsigned int old_head = head;
    head = (head + 1) % QUEUE_SIZE;
    size--;
    app->pending = size;
    app->dirty = true;
    if (!storage_save(app)) {
      head = old_head;
      size++;
      app->pending = size;
      app->dirty = true;
      next_try = app->now + 5000;
      return;
    }
  }
  if (recovered) {
    record_event(app, "STORAGE_READY", "SD writes available; history gaps remain explicit");
  }
}
