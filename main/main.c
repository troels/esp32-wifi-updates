#include "esp_err.h"
#include "esp_log.h"
#include "wifi.h"

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

  vTaskSuspend(NULL);
}
