idf_component_register(
  SRCS
    "main.c"
    "access_control.c"
    "occupancy.c"
    "lighting.c"
    "scheduler.c"
    "logger.c"
    "serial_menu.c"
    "sync_bus.c"
    "storage.c"
  INCLUDE_DIRS "."
  REQUIRES driver esp_timer fatfs sdmmc nvs_flash
)
