#include "esp_tls.h"
#include "esp_err.h"
#include "esp_log.h"

const char *TAG = "CAStore";

extern const uint8_t ca_cert_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t ca_cert_pem_end[] asm("_binary_ca_pem_end");

esp_err_t setup_global_ca_store() {
  esp_err_t err;
  err = esp_tls_init_global_ca_store();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize global ca store: %s", esp_err_to_name(err));
    return err;
  }
  
  err = esp_tls_set_global_ca_store(ca_cert_pem_start, ca_cert_pem_end - ca_cert_pem_start);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set global cert pem: %s", esp_err_to_name(err));
    return err;
  }
  return ESP_OK;
}
