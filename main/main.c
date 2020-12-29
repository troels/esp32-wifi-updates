#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "wifi.h"
#include "update.h"
#include "mqtt.h"
#include "certificates.h"
#include "cJSON.h"
#include "json.h"
#include "am2320.h"
#include "sntp.h"
#include "esp_wifi.h"


static const char *TAG = "main";

extern const uint8_t ca_cert_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t ca_cert_pem_end[] asm("_binary_ca_pem_end");

void app_main(void)
{
  WifiInfo wifi_info;
  esp_err_t err;

  init_cjson();
  
  err = setup_global_ca_store();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize global ca store: %s", esp_err_to_name(err));
    return;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
    return;
  }
  
  err = initialize_wifi_with_provisioning(&wifi_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize wifi with smartconfig: %s", esp_err_to_name(err));
    return;
  }    
  
  /* err = start_monitor_update_task("https://192.168.1.19:8700/image.bin"); */
  /* if (err != ESP_OK) { */
  /*   ESP_LOGE(TAG, "Failed to start monitor update task: %s", esp_err_to_name(err)); */
  /*   return; */
  /* } */

  while (1) {
    err = wait_for_connection(&wifi_info, portMAX_DELAY);
    if (err == ESP_OK) {
      break;
    }
    ESP_LOGW(TAG, "Connection failed");
  }

  
  vTaskDelay(pdMS_TO_TICKS(5000));

  init_sntp();
  
  esp_mqtt_client_handle_t mqtt_client;
  err = create_mqtt_client("mqtts://178.128.42.0", &mqtt_client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create mqtt client: %s", esp_err_to_name(err));
    return;
  }

  esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
  
  while (1) {
    wait_for_connection(&wifi_info, portMAX_DELAY);
    am2320_measurement measurement;
    err = am2320_measure(I2C_NUM_0, GPIO_NUM_21, GPIO_NUM_22, &measurement);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Problem getting AM2320 measurement: %s", esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    } 
    ESP_LOGI(TAG, "Logging temperature");
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "temperature", measurement.temperature);
    cJSON_AddNumberToObject(resp, "relative_humidity", measurement.relative_humidity);
    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;
    
    time(&now);
    if (gmtime_r(&now, &timeinfo) == NULL) {
      ESP_LOGE(TAG, "Failed to get time: %s", strerror(errno));
      continue;
    }

    if (strftime(strftime_buf, sizeof(strftime_buf), "%FT%T", &timeinfo) == 0) {
      ESP_LOGE(TAG, "Failed to formattime: %s", strerror(errno));
      continue;
    }
      
    cJSON_AddStringToObject(resp, "time", strftime_buf);
    char *out = cJSON_Print(resp);
    cJSON_free(resp);
    esp_mqtt_client_publish(mqtt_client, "topic/temperature", out, strlen(out), 1, 0);
    cJSON_free(out);
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
  
  vTaskSuspend(NULL);
}
