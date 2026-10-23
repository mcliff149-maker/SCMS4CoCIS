#include "storage.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"
#include "scms_config.h"

#define LOG_PATH "/sdcard/scms.log"
#define FALLBACK_LINES 32
#define LOG_LINE_SIZE 192

static bool sd_ready;
static bool nvs_ready;
static nvs_handle_t schedule_handle;
static char fallback[FALLBACK_LINES][LOG_LINE_SIZE];
static unsigned fallback_next;
static unsigned fallback_count;

typedef struct {
  uint32_t version;
  uint32_t open;
  uint32_t close;
} saved_schedule_t;

bool storage_init(void) {
  esp_err_t result = nvs_flash_init();
  if (result == ESP_OK) {
    result = nvs_open("scms", NVS_READWRITE, &schedule_handle);
    nvs_ready = result == ESP_OK;
  }
  if (!nvs_ready)
    printf("WARN schedule NVS unavailable (%s); flash not erased\n",
           esp_err_to_name(result));

  spi_bus_config_t bus = {
    .mosi_io_num = PIN_SD_MOSI, .miso_io_num = PIN_SD_MISO,
    .sclk_io_num = PIN_SD_SCK, .quadwp_io_num = -1, .quadhd_io_num = -1,
    .max_transfer_sz = 4096
  };
  result = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (result != ESP_OK) {
    printf("WARN SD SPI unavailable (%s); logs volatile\n", esp_err_to_name(result));
    return false;
  }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  host.command_timeout_ms = 1000;
  sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
  device.host_id = SPI2_HOST;
  device.gpio_cs = PIN_SD_CS;
  esp_vfs_fat_sdmmc_mount_config_t mount = {
    .format_if_mount_failed = false, .max_files = 2,
    .allocation_unit_size = 16 * 1024
  };
  sdmmc_card_t *card = NULL;
  result = esp_vfs_fat_sdspi_mount("/sdcard", &host, &device, &mount, &card);
  sd_ready = result == ESP_OK;
  if (!sd_ready) {
    printf("WARN SD mount failed (%s); logs volatile\n", esp_err_to_name(result));
    spi_bus_free(SPI2_HOST);
  }
  return sd_ready;
}

bool storage_available(void) {
  return sd_ready;
}

bool storage_nvs_available(void) {
  return nvs_ready;
}

bool storage_append(const char *line) {
  if (sd_ready) {
    FILE *file = fopen(LOG_PATH, "a");
    bool written = false;
    if (file) {
      written = fprintf(file, "%.*s\n", LOG_LINE_SIZE - 1, line) >= 0;
      if (fclose(file) != 0)
        written = false;
    }
    if (written)
      return true;
    sd_ready = false;
    printf("WARN SD write failed; subsequent logs volatile\n");
  }
  snprintf(fallback[fallback_next], LOG_LINE_SIZE, "%s", line);
  fallback_next = (fallback_next + 1) % FALLBACK_LINES;
  if (fallback_count < FALLBACK_LINES)
    ++fallback_count;
  return false;
}

void storage_print_logs(unsigned limit) {
  if (limit == 0 || limit > 50)
    limit = 50;
  if (sd_ready) {
    FILE *file = fopen(LOG_PATH, "r");
    if (file) {
      char line[LOG_LINE_SIZE];
      unsigned count = 0;
      while (count < limit && fgets(line, sizeof(line), file)) {
        printf("%s", line);
        ++count;
      }
      bool failed = ferror(file) != 0;
      if (fclose(file) != 0)
        failed = true;
      if (!failed) {
        printf("\nSD logs: first %u lines/chunks (limit %u)\n", count, limit);
        return;
      }
    }
    sd_ready = false;
    printf("WARN SD log read failed; storage now volatile\n");
  }
  printf("VOLATILE RAM logs: %u retained; no SD persistence\n", fallback_count);
  unsigned count = fallback_count < limit ? fallback_count : limit;
  unsigned start = (fallback_next + FALLBACK_LINES - count) % FALLBACK_LINES;
  for (unsigned i = 0; i < count; ++i)
    printf("%s\n", fallback[(start + i) % FALLBACK_LINES]);
}

bool storage_schedule_load(unsigned *open, unsigned *close) {
  if (!nvs_ready)
    return false;
  saved_schedule_t saved;
  size_t size = sizeof(saved);
  esp_err_t result = nvs_get_blob(schedule_handle, "schedule", &saved, &size);
  if (result == ESP_ERR_NVS_NOT_FOUND)
    return false;
  if (result != ESP_OK && result != ESP_ERR_NVS_INVALID_LENGTH) {
    nvs_ready = false;
    printf("WARN schedule NVS read failed (%s)\n", esp_err_to_name(result));
    return false;
  }
  if (result != ESP_OK || size != sizeof(saved) || saved.version != 1 ||
      saved.open >= saved.close || saved.close >= 1440) {
    printf("WARN saved schedule unavailable or invalid; using defaults\n");
    return false;
  }
  *open = saved.open;
  *close = saved.close;
  return true;
}

bool storage_schedule_save(unsigned open, unsigned close) {
  if (!nvs_ready || open >= close || close >= 1440)
    return false;
  saved_schedule_t saved = {.version = 1, .open = open, .close = close};
  esp_err_t result = nvs_set_blob(schedule_handle, "schedule", &saved, sizeof(saved));
  if (result == ESP_OK)
    result = nvs_commit(schedule_handle);
  if (result != ESP_OK) {
    nvs_ready = false;
    printf("WARN schedule NVS save failed (%s)\n", esp_err_to_name(result));
  }
  return result == ESP_OK;
}
