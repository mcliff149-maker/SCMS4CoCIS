#include "scms.h"
#include "board_pins.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/rmt.h"
#include "driver/sdspi_host.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUS SPI2_HOST
#define PIXEL_CHANNEL RMT_CHANNEL_0

static spi_device_handle_t readers[3];
static bool spi_ready;
static bool i2c_ready;
static bool pixel_ready;
static bool servo_ready[3];
static bool mounted;
static scms_uid_t held[3];
static int64_t last_seen[3];
static const int servo_pins[3] = {25, 26, 27};
static const int reader_cs[3] = {4, 13, 14};

int64_t hw_millis(void) {
  return esp_timer_get_time() / 1000;
}

static bool check(esp_err_t error, const char *what) {
  if (error == ESP_OK) {
    return true;
  }
  printf("HARDWARE FAULT: %s: %s\n", what, esp_err_to_name(error));
  return false;
}

static bool lcd_byte(uint8_t value, bool data) {
  uint8_t high = (value & 0xf0) | 0x08 | (data ? 1 : 0);
  uint8_t low = (value << 4) | 0x08 | (data ? 1 : 0);
  uint8_t bytes[4] = {high | 4, high, low | 4, low};
  return i2c_master_write_to_device(I2C_NUM_0, SCMS_LCD_I2C_ADDRESS,
                                   bytes, sizeof(bytes),
                                   pdMS_TO_TICKS(40)) == ESP_OK;
}

static bool lcd_init(void) {
  vTaskDelay(pdMS_TO_TICKS(50));
  const uint8_t nibbles[] = {0x30, 0x30, 0x30, 0x20};
  for (unsigned int i = 0; i < sizeof(nibbles); i++) {
    uint8_t bytes[2] = {nibbles[i] | 0x0c, nibbles[i] | 0x08};
    if (i2c_master_write_to_device(I2C_NUM_0, SCMS_LCD_I2C_ADDRESS,
                                   bytes, 2, pdMS_TO_TICKS(40)) != ESP_OK) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (!lcd_byte(0x28, false) || !lcd_byte(0x0c, false) ||
      !lcd_byte(0x06, false) || !lcd_byte(0x01, false)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(3));
  return true;
}

bool hw_display(const char lines[4][21]) {
  static const uint8_t rows[4] = {0x80, 0xc0, 0x94, 0xd4};
  if (!i2c_ready) {
    return false;
  }
  for (int row = 0; row < 4; row++) {
    if (!lcd_byte(rows[row], false)) {
      return false;
    }
    bool ended = false;
    for (int col = 0; col < 20; col++) {
      if (lines[row][col] == '\0') {
        ended = true;
      }
      if (!lcd_byte(ended ? ' ' : (uint8_t)lines[row][col], true)) {
        return false;
      }
    }
  }
  return true;
}

static bool reg_write(int reader, uint8_t reg, uint8_t value) {
  uint8_t tx[2] = {(uint8_t)((reg << 1) & 0x7e), value};
  spi_transaction_t transaction = {
    .length = 16,
    .tx_buffer = tx
  };
  return readers[reader] &&
         spi_device_polling_transmit(readers[reader], &transaction) == ESP_OK;
}

static bool reg_read(int reader, uint8_t reg, uint8_t *value) {
  uint8_t tx[2] = {(uint8_t)(((reg << 1) & 0x7e) | 0x80), 0};
  uint8_t rx[2] = {0};
  spi_transaction_t transaction = {
    .length = 16,
    .tx_buffer = tx,
    .rx_buffer = rx
  };
  if (!readers[reader] ||
      spi_device_polling_transmit(readers[reader], &transaction) != ESP_OK) {
    return false;
  }
  *value = rx[1];
  return true;
}

/* Return 0 for no card, -1 for hardware/protocol error, otherwise bytes read. */
static int exchange(int reader, const uint8_t *tx, size_t count,
                    int bits, uint8_t *rx, size_t capacity) {
  if (!reg_write(reader, 0x01, 0) || !reg_write(reader, 0x04, 0x7f) ||
      !reg_write(reader, 0x0a, 0x80)) {
    return -1;
  }
  for (size_t i = 0; i < count; i++) {
    if (!reg_write(reader, 0x09, tx[i])) {
      return -1;
    }
  }
  if (!reg_write(reader, 0x0d, bits) || !reg_write(reader, 0x01, 0x0c) ||
      !reg_write(reader, 0x0d, bits | 0x80)) {
    return -1;
  }
  int64_t limit = hw_millis() + 35;
  uint8_t irq = 0;
  do {
    if (!reg_read(reader, 0x04, &irq)) {
      return -1;
    }
    if (irq & 0x31) {
      break;
    }
    vTaskDelay(1);
  } while (hw_millis() < limit);
  if (!reg_write(reader, 0x0d, bits)) {
    return -1;
  }
  if (!(irq & 0x30)) {
    return 0;
  }
  uint8_t error, length;
  if (!reg_read(reader, 0x06, &error) ||
      !reg_read(reader, 0x0a, &length) || (error & 0x1b) ||
      length > capacity) {
    return -1;
  }
  for (uint8_t i = 0; i < length; i++) {
    if (!reg_read(reader, 0x09, &rx[i])) {
      return -1;
    }
  }
  return length;
}

static void crc_a(const uint8_t *bytes, size_t length, uint8_t *out) {
  uint16_t crc = 0x6363;
  for (size_t i = 0; i < length; i++) {
    uint8_t c = bytes[i] ^ (crc & 0xff);
    c ^= c << 4;
    crc = (crc >> 8) ^ ((uint16_t)c << 8) ^
          ((uint16_t)c << 3) ^ (c >> 4);
  }
  out[0] = crc & 0xff;
  out[1] = crc >> 8;
}

static int read_uid(int reader, scms_uid_t *uid) {
  uint8_t version;
  if (!reg_read(reader, 0x37, &version) || version == 0 || version == 0xff) {
    return -1;
  }
  uint8_t request = 0x52;
  uint8_t rx[16];
  int result = exchange(reader, &request, 1, 7, rx, sizeof(rx));
  if (result <= 0) {
    return result;
  }
  if (result != 2) {
    return -1;
  }
  uid->size = 0;
  for (int level = 0; level < 3; level++) {
    uint8_t command[9] = {(uint8_t)(0x93 + level * 2), 0x20};
    result = exchange(reader, command, 2, 0, rx, sizeof(rx));
    if (result != 5 || (rx[0] ^ rx[1] ^ rx[2] ^ rx[3]) != rx[4]) {
      return -1;
    }
    bool cascade = rx[0] == 0x88;
    for (int i = cascade ? 1 : 0; i < 4; i++) {
      if (uid->size >= sizeof(uid->bytes)) {
        return -1;
      }
      uid->bytes[uid->size++] = rx[i];
    }
    command[1] = 0x70;
    memcpy(command + 2, rx, 5);
    crc_a(command, 7, command + 7);
    result = exchange(reader, command, 9, 0, rx, sizeof(rx));
    if (result != 3) {
      return -1;
    }
    uint8_t crc[2];
    crc_a(rx, 1, crc);
    if (rx[1] != crc[0] || rx[2] != crc[1]) {
      return -1;
    }
    if (!(rx[0] & 4)) {
      if (cascade) {
        return -1;
      }
      uint8_t halt[4] = {0x50, 0x00};
      crc_a(halt, 2, halt + 2);
      int halted = exchange(reader, halt, sizeof(halt), 0, rx, sizeof(rx));
      return halted < 0 ? -1 : 1;
    }
    if (!cascade) {
      return -1;
    }
  }
  return -1;
}

int hw_card(int reader, scms_uid_t *uid) {
  int result = read_uid(reader, uid);
  if (result == 0) {
    if (hw_millis() - last_seen[reader] > 350) {
      held[reader].size = 0;
    }
    return 0;
  }
  if (result < 0) {
    return -1;
  }
  last_seen[reader] = hw_millis();
  if (uid->size == held[reader].size &&
      memcmp(uid->bytes, held[reader].bytes, uid->size) == 0) {
    return 0;
  }
  held[reader] = *uid;
  return 1;
}

static bool reader_init(int reader) {
  if (!readers[reader] || !reg_write(reader, 0x01, 0x0f)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  return reg_write(reader, 0x2a, 0x80) &&
         reg_write(reader, 0x2b, 0xa9) &&
         reg_write(reader, 0x2c, 0x03) &&
         reg_write(reader, 0x2d, 0xe8) &&
         reg_write(reader, 0x15, 0x40) &&
         reg_write(reader, 0x11, 0x3d) &&
         reg_write(reader, 0x14, 0x03);
}

bool hw_inputs(uint16_t *value) {
  uint16_t result = 0;
  gpio_set_level(SCMS_INPUT_CLOCK_GPIO, 0);
  gpio_set_level(SCMS_INPUT_LOAD_GPIO, 0);
  esp_rom_delay_us(2);
  gpio_set_level(SCMS_INPUT_LOAD_GPIO, 1);
  for (int byte = 0; byte < 2; byte++) {
    uint8_t data = 0;
    for (int bit = 0; bit < 8; bit++) {
      data = (data << 1) | gpio_get_level(SCMS_INPUT_DATA_GPIO);
      gpio_set_level(SCMS_INPUT_CLOCK_GPIO, 1);
      esp_rom_delay_us(1);
      gpio_set_level(SCMS_INPUT_CLOCK_GPIO, 0);
      esp_rom_delay_us(1);
    }
    result |= (uint16_t)data << (byte * 8);
  }
  *value = result;
  return true;
}

bool hw_pir(int room) {
  return gpio_get_level(room ? SCMS_ROOM2_PIR_GPIO : SCMS_ROOM1_PIR_GPIO);
}

bool hw_emergency(void) {
  return gpio_get_level(SCMS_EMERGENCY_GPIO) == 0;
}

void hw_gate(int gate, bool open) {
  if (!servo_ready[gate]) {
    return;
  }
  unsigned int pulse = open ? 1500 : 500;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, gate, pulse * 65535 / 20000);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, gate);
}

void hw_light(int room, bool on) {
  gpio_set_level(room ? SCMS_ROOM2_RELAY_GPIO : SCMS_ROOM1_RELAY_GPIO,
                 on ? SCMS_RELAY_ON_LEVEL : SCMS_RELAY_OFF_LEVEL);
}

void hw_buzzer(bool on) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, on ? 512 : 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);
}

void hw_status(unsigned int r, unsigned int g, unsigned int b) {
  if (!pixel_ready) {
    return;
  }
  uint32_t color = ((g & 255) << 16) | ((r & 255) << 8) | (b & 255);
  rmt_item32_t items[25] = {0};
  for (int i = 0; i < 24; i++) {
    bool one = (color & (1u << (23 - i))) != 0;
    items[i].level0 = 1;
    items[i].duration0 = one ? 8 : 4;
    items[i].level1 = 0;
    items[i].duration1 = one ? 4 : 8;
  }
  items[24].level0 = 0;
  items[24].duration0 = 800;
  items[24].level1 = 0;
  items[24].duration1 = 10;
  rmt_write_items(PIXEL_CHANNEL, items, 25, true);
}

static int bcd(uint8_t value) {
  if ((value & 15) > 9 || (value >> 4) > 9) {
    return -1;
  }
  return (value >> 4) * 10 + (value & 15);
}

bool hw_time(time_t *utc) {
  uint8_t reg = 0;
  uint8_t bytes[7];
  if (!i2c_ready ||
      i2c_master_write_read_device(I2C_NUM_0, SCMS_RTC_I2C_ADDRESS,
                                   &reg, 1, bytes, 7,
                                   pdMS_TO_TICKS(40)) != ESP_OK ||
      (bytes[0] & 0x80) || (bytes[2] & 0x40)) {
    return false;
  }
  int sec = bcd(bytes[0] & 0x7f), min = bcd(bytes[1]);
  int hour = bcd(bytes[2] & 0x3f), day = bcd(bytes[4]);
  int month = bcd(bytes[5]), year = bcd(bytes[6]);
  if (sec < 0 || sec > 59 || min < 0 || min > 59 ||
      hour < 0 || hour > 23 || day < 1 || day > 31 ||
      month < 1 || month > 12 || year < 24 || year > 99) {
    return false;
  }
  struct tm tm = {
    .tm_year = year + 100, .tm_mon = month - 1, .tm_mday = day,
    .tm_hour = hour, .tm_min = min, .tm_sec = sec, .tm_isdst = 0
  };
  *utc = mktime(&tm);
  struct tm verified;
  gmtime_r(utc, &verified);
  return verified.tm_mday == day && verified.tm_mon == month - 1;
}

bool hw_spi_ready(void) {
  return spi_ready;
}

bool hw_sd_mount(void) {
  if (mounted) {
    return true;
  }
  if (!spi_ready) {
    return false;
  }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = BUS;
  host.max_freq_khz = 1000;
  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.host_id = BUS;
  slot.gpio_cs = SCMS_SD_CS_GPIO;
  esp_vfs_fat_sdmmc_mount_config_t config = {
    .format_if_mount_failed = false, .max_files = 3,
    .allocation_unit_size = 4096
  };
  sdmmc_card_t *card = NULL;
  mounted = check(esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot,
                                        &config, &card), "SD mount");
  return mounted;
}

bool hw_init(void) {
  bool ok = true;
  uint64_t outputs = (1ULL << SCMS_INPUT_CLOCK_GPIO) |
    (1ULL << SCMS_INPUT_LOAD_GPIO) | (1ULL << SCMS_ROOM1_RELAY_GPIO) |
    (1ULL << SCMS_ROOM2_RELAY_GPIO) | (1ULL << SCMS_SD_CS_GPIO);
  for (int i = 0; i < 3; i++) {
    outputs |= 1ULL << reader_cs[i];
  }
  gpio_config_t out = {.pin_bit_mask = outputs, .mode = GPIO_MODE_OUTPUT};
  ok &= check(gpio_config(&out), "output pins");
  gpio_set_level(SCMS_INPUT_LOAD_GPIO, 1);
  hw_light(0, false);
  hw_light(1, false);
  gpio_set_level(SCMS_SD_CS_GPIO, 1);
  for (int i = 0; i < 3; i++) {
    gpio_set_level(reader_cs[i], 1);
  }
  gpio_config_t in = {
    .pin_bit_mask = (1ULL << SCMS_INPUT_DATA_GPIO) |
      (1ULL << SCMS_ROOM1_PIR_GPIO) | (1ULL << SCMS_ROOM2_PIR_GPIO) |
      (1ULL << SCMS_EMERGENCY_GPIO),
    .mode = GPIO_MODE_INPUT
  };
  ok &= check(gpio_config(&in), "input pins");
  spi_bus_config_t bus = {
    .mosi_io_num = SCMS_SPI_MOSI_GPIO, .miso_io_num = SCMS_SPI_MISO_GPIO,
    .sclk_io_num = SCMS_SPI_SCK_GPIO, .quadwp_io_num = -1,
    .quadhd_io_num = -1
  };
  spi_ready = check(spi_bus_initialize(BUS, &bus, SPI_DMA_CH_AUTO), "SPI");
  ok &= spi_ready;
  if (spi_ready) {
    for (int i = 0; i < 3; i++) {
      spi_device_interface_config_t device = {
        .clock_speed_hz = 1000000, .mode = 0,
        .spics_io_num = reader_cs[i], .queue_size = 1
      };
      bool added = check(spi_bus_add_device(BUS, &device, &readers[i]),
                          "RFID SPI device");
      if (!added || !reader_init(i)) {
        printf("HARDWARE FAULT: RFID reader %d initialization\n", i + 1);
        ok = false;
      }
    }
  }
  i2c_config_t i2c = {
    .mode = I2C_MODE_MASTER, .sda_io_num = SCMS_I2C_SDA_GPIO,
    .scl_io_num = SCMS_I2C_SCL_GPIO,
    .master.clk_speed = 100000
  };
  i2c_ready = check(i2c_param_config(I2C_NUM_0, &i2c), "I2C setup") &&
    check(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0), "I2C");
  if (!i2c_ready || !lcd_init()) {
    printf("HARDWARE FAULT: LCD initialization\n");
  }
  ledc_timer_config_t servo_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_16_BIT,
    .timer_num = LEDC_TIMER_0, .freq_hz = 50, .clk_cfg = LEDC_AUTO_CLK
  };
  bool timer_ok = check(ledc_timer_config(&servo_timer), "servo timer");
  ok &= timer_ok;
  for (int i = 0; i < 3; i++) {
    ledc_channel_config_t channel = {
      .gpio_num = servo_pins[i], .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = i, .timer_sel = LEDC_TIMER_0,
      .duty = 500 * 65535 / 20000
    };
    servo_ready[i] = timer_ok && check(ledc_channel_config(&channel), "servo");
    ok &= servo_ready[i];
  }
  ledc_timer_config_t buzzer_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
    .timer_num = LEDC_TIMER_1, .freq_hz = 2000, .clk_cfg = LEDC_AUTO_CLK
  };
  ok &= check(ledc_timer_config(&buzzer_timer), "buzzer timer");
  ledc_channel_config_t buzzer = {
    .gpio_num = SCMS_BUZZER_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_3, .timer_sel = LEDC_TIMER_1, .duty = 0
  };
  ok &= check(ledc_channel_config(&buzzer), "buzzer");
  rmt_config_t pixel = RMT_DEFAULT_CONFIG_TX(SCMS_STATUS_LED_GPIO, PIXEL_CHANNEL);
  pixel.clk_div = 8;
  pixel_ready = check(rmt_config(&pixel), "status LED setup") &&
    check(rmt_driver_install(PIXEL_CHANNEL, 0, 0), "status LED");
  ok &= pixel_ready;
  hw_status(0, 0, 20);
  return ok;
}

void hw_restart(void) {
  esp_restart();
}
