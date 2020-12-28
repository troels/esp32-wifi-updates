#include "mqtt_client.h"
#include "esp_err.h"
#include "esp_log.h"


static const char *TAG = "MQTT";

extern const uint8_t mqtt_client_cert_pem_start[] asm("_binary_temperature_sensor_pem_start");
extern const uint8_t mqtt_client_cert_pem_end[] asm("_binary_temperature_sensor_pem_end");

extern const uint8_t mqtt_client_key_start[] asm("_binary_temperature_sensor_key_start");
extern const uint8_t mqtt_client_key_end[] asm("_binary_temperature_sensor_key_end");

esp_err_t create_mqtt_client(char *broker_url, esp_mqtt_client_handle_t *client)
{
  esp_err_t err;
  esp_mqtt_client_config_t mqtt_cfg = {
    .uri = broker_url,
    .use_global_ca_store = true,
    .client_cert_pem = (char*)mqtt_client_cert_pem_start,
    .client_cert_len = mqtt_client_cert_pem_end - mqtt_client_cert_pem_start,
    .client_key_pem = (char*)mqtt_client_key_start,
    .client_key_len = mqtt_client_key_end - mqtt_client_key_start,
    .keepalive = 30000,
    .disable_auto_reconnect = 0
  };
  
  *client = esp_mqtt_client_init(&mqtt_cfg);
  err = esp_mqtt_client_start(*client);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start MQTT Client: %s", esp_err_to_name(err));
    return err;
  }
  return ESP_OK;
}
  
