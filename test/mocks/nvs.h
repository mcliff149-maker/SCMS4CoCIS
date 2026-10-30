#ifndef TEST_NVS_H
#define TEST_NVS_H
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
typedef unsigned int nvs_handle_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
const char *esp_err_to_name(esp_err_t error);
#endif
