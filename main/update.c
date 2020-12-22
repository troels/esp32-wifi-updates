#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "ota_update";

extern const uint8_t ca_cert_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t ca_cert_pem_end[] asm("_binary_ca_pem_end");

static esp_err_t update_firmware(char *url)
{
  esp_err_t err;
  esp_http_client_config_t config = {
    .url = url,
    .cert_pem = (char *)ca_cert_pem_start,
    .event_handler = NULL,
    .skip_cert_common_name_check = true
  };

  err = esp_https_ota(&config);
  if (err == ESP_OK) {
    esp_restart();
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Firmware upgrade failed: %s", esp_err_to_name(err));
  }
  
  return err;
}

static void monitor_update_task(void *param)
{
  char *url = (char*) param;
  esp_err_t err;
  while(1) {
    vTaskDelay(pdMS_TO_TICKS(60000));
    err = update_firmware(url);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed trying to update ESP firmware: %s", esp_err_to_name(err));
    }
  }
}

esp_err_t start_monitor_update_task(char *url)
{
  BaseType_t rtos_err = xTaskCreate(monitor_update_task, "monitor_updates",
                                    4096, (void*)url, 2, NULL);

  if (rtos_err != pdPASS) {
    ESP_LOGE(TAG, "Failed starting monitor task: %d", rtos_err);
    return ESP_FAIL;
  }
  return ESP_OK;
}
