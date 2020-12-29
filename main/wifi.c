#include <string.h>
#include "wifi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "esp_wifi_types.h"


static const char *TAG = "Wifi";
static const char *SERVICE_NAME = "temperature_humidifier_1";
static const char *SERVICE_KEY  = "secret_secret_ever_secret";
static const char *POP_SECRET = "POP_SECRET";

static const int CONNECTED_BIT = BIT0;
static const int PROVISIONING_ACTIVE = BIT1;
static const int GOT_IP_BIT = BIT2;
static const int RERUN_PROVISIONING_BIT = BIT4;

static esp_err_t
reset_ssid() {
  wifi_config_t wifi_cfg;
  esp_err_t err;
  
  err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set wifi storage: %s", esp_err_to_name(err));
    return err;
  }

  
  err = esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get wifi config: %s", esp_err_to_name(err));
    return err;
  }

  wifi_cfg.sta.ssid[0] = '\0';

  err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get wifi config: %s", esp_err_to_name(err));
    return err;
  }
  
  return ESP_OK;
}

static void
wifi_provisioning(void *param)
{
  WifiInfo *wifi_info = (WifiInfo*)param;
  esp_err_t  err;
  wifi_prov_mgr_config_t config = {
    .scheme = wifi_prov_scheme_ble,
    .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    .app_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
  };
  EventBits_t uxBits;
 restart:
  uxBits = xEventGroupGetBits(wifi_info->event_group);
  if (uxBits & RERUN_PROVISIONING_BIT) {
    reset_ssid();
    xEventGroupClearBits(wifi_info->event_group, RERUN_PROVISIONING_BIT);
  }
  
  err = wifi_prov_mgr_init(config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize wifi provisioning: %s", esp_err_to_name(err));
    goto end;
  }
  
  
  err = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1,
                                         POP_SECRET,
                                         SERVICE_NAME,
                                         SERVICE_KEY);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize wifi provisioning: %s", esp_err_to_name(err));
    goto end;
  }
  
  while (1) {
    uxBits = xEventGroupWaitBits(wifi_info->event_group,
                                             CONNECTED_BIT | RERUN_PROVISIONING_BIT,
                                             pdFALSE, false, portMAX_DELAY);
    if (uxBits & RERUN_PROVISIONING_BIT) {
      reset_ssid();
      esp_restart();
      goto restart;
    } else if(uxBits & CONNECTED_BIT) {
      goto end;
    }
  }
  
 end:
    xEventGroupClearBits(wifi_info->event_group, PROVISIONING_ACTIVE);
    vTaskDelete(NULL);
}

static void
start_wifi_provisioning(WifiInfo *wifi_info) {
  xSemaphoreTake(wifi_info->wifi_semaphore, portMAX_DELAY);
  EventBits_t uxBits = xEventGroupGetBits(wifi_info->event_group);
  if (uxBits & PROVISIONING_ACTIVE) {
    xSemaphoreGive(wifi_info->wifi_semaphore);
    return;
  }

  ESP_LOGI(TAG, "Starting provisioning");
  xEventGroupSetBits(wifi_info->event_group, PROVISIONING_ACTIVE);
  BaseType_t err = xTaskCreate(wifi_provisioning, "wifi_provisioning",
                               4096, wifi_info, 2, NULL);

  if (err != pdPASS) {
    ESP_LOGE(TAG, "Failed to start wifi_provisioning");
  }
  xSemaphoreGive(wifi_info->wifi_semaphore);
}

static void
try_to_initialize_wifi_unless_connected(void *param)
{
  WifiInfo *wifi_info = (WifiInfo *) param;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(60000));
    EventBits_t uxBits = xEventGroupGetBits(wifi_info->event_group);
    if (uxBits & CONNECTED_BIT) {
      continue;
    }

    esp_wifi_connect();
  } 
}

esp_err_t wait_for_connection(WifiInfo *wifi_info, TickType_t wait_time)
{
  EventBits_t uxBits = xEventGroupWaitBits(wifi_info->event_group,
                                           GOT_IP_BIT,
                                           pdFALSE, false, wait_time);
  if(uxBits & GOT_IP_BIT) {
    return ESP_OK;
  }
  return ESP_FAIL;
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
  WifiInfo *wifi_info = (WifiInfo*) arg;
  
  if (event_base == WIFI_PROV_EVENT && event_id == WIFI_PROV_CRED_FAIL) {
    xEventGroupClearBits(wifi_info->event_group, CONNECTED_BIT | GOT_IP_BIT);
    wifi_err_reason_t *reason = (void*)event_data;
    if (*reason == WIFI_REASON_AUTH_FAIL) {
      xEventGroupSetBits(wifi_info->event_group, RERUN_PROVISIONING_BIT);
      start_wifi_provisioning(wifi_info);
    } else {
      esp_wifi_connect();
    }
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(wifi_info->event_group, CONNECTED_BIT | GOT_IP_BIT);
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    xEventGroupSetBits(wifi_info->event_group, CONNECTED_BIT);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(wifi_info->event_group, GOT_IP_BIT);
  }
}


esp_err_t initialize_wifi_with_provisioning(WifiInfo *wifi_info)
{
  esp_err_t err = ESP_OK; 
  
  wifi_info->event_group = xEventGroupCreate();
  if (wifi_info->event_group == NULL) {
    ESP_LOGE(TAG,"Error creating eventgroup");
    goto cleanup; 
  }

  wifi_info->wifi_semaphore = xSemaphoreCreateBinary();
  if (wifi_info->wifi_semaphore == NULL) {
    ESP_LOGE(TAG,"Error creating wifi semaphore");
    goto cleanup;
  }
  xSemaphoreGive(wifi_info->wifi_semaphore);

  err = nvs_flash_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize nvs flash: %s", esp_err_to_name(err));
    return err;
  }

  err = mdns_init();
  if (err) {
    ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
    goto cleanup;
  }

  err = mdns_hostname_set("temperature_sensor");
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
    goto cleanup;
  }
  err = mdns_instance_name_set("Temperature Sensor");
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
    goto cleanup;
  }

  err = esp_netif_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(err));
    goto cleanup;
  }

  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  if (sta_netif == NULL) {
    ESP_LOGE(TAG, "Failed to create default wifi sta");
    err = ESP_FAIL;
    goto cleanup;
  }
  
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize wifi: %s", esp_err_to_name(err));
    goto cleanup;
  }

  err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   &wifi_event_handler, wifi_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register wifi handler: %s", esp_err_to_name(err));
    goto cleanup;
  }

  err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   &wifi_event_handler, wifi_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register ip handler: %s", esp_err_to_name(err));
    goto cleanup;
  }

  err = esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                   &wifi_event_handler, wifi_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register provisioning handler: %s", esp_err_to_name(err));
    goto cleanup;
  }

  ESP_LOGI(TAG, "Starting wifi provisioning");
  start_wifi_provisioning(wifi_info);
  BaseType_t rtos_err = xTaskCreate(try_to_initialize_wifi_unless_connected, "maintain_wifi",
                                    4096, wifi_info, 1, NULL);
  if (rtos_err != pdPASS) {
    ESP_LOGE(TAG, "Failed to start wifi maintenance task");
  }


  return ESP_OK;
  
 cleanup:

  ESP_LOGI(TAG, "Went to cleanup");
  if (wifi_info->event_group != NULL) {
    ESP_LOGI(TAG, "Deleting event group");
    vEventGroupDelete(wifi_info->event_group);
  }

  if (wifi_info->wifi_semaphore != NULL) {
    ESP_LOGI(TAG, "Deleting wifi semaphore");
    vSemaphoreDelete(wifi_info->wifi_semaphore);
  }

  esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
  esp_event_handler_unregister(SC_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
  esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
  
  return err;
}
