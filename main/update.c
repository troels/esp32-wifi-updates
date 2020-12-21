#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "esp_log.h"

const char *TAG = "ota_update";

extern const uint8_t server_cert_pem_start[] asm("_binary_wifiupdate_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_wifiupdate_pem_end");

esp_err_t update_firmware(char *url)
{
  esp_err_t err;
  esp_http_client_config_t config = {
    .url = url,
    .cert_pem = (char *)server_cert_pem_start,
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
