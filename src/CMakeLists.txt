idf_component_register(
    SRCS "main.c" "hardware.c" "access_control.c" "occupancy_lighting.c"
         "dashboard.c" "storage.c" "statistics.c" "safety_recovery.c"
    INCLUDE_DIRS "../include"
    REQUIRES driver fatfs sdmmc nvs_flash esp_timer
)
