#include "esp_err.h"
#include "esp_log.h"
#include "wifi.h"
#include "update.h"

static const char *TAG = "main";

void app_main(void)
{
  WifiInfo wifi_info;
  esp_err_t err;
  
  err = esp_event_loop_create_default();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
    return;
  }
  
  err = initialize_wifi_with_smartconfig(&wifi_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize wifi with smartconfig: %s", esp_err_to_name(err));
    return;
  }    

  while (1) {
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    err = update_firmware("https://192.168.1.19:8700/image.bin");
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to update firmware: %s", esp_err_to_name(err));
    }
  }
}
