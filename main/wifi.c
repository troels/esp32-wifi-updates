#include <string.h>
#include "wifi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_wifi.h"


static const char *TAG = "Wifi";

static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const int GOT_IP_BIT = BIT2;

static void smartconfig_config(void *param)
{
  WifiInfo *wifi_info = (WifiInfo*)param;
  esp_err_t  err;
  
  err = esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set smartconfig type: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }
  
  smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
  err = esp_smartconfig_start(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure smartconfig: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    EventBits_t uxBits = xEventGroupWaitBits(wifi_info->event_group,
                                             ESPTOUCH_DONE_BIT | CONNECTED_BIT,
                                             pdFALSE, false, portMAX_DELAY);
    if((uxBits & ESPTOUCH_DONE_BIT) || (uxBits & CONNECTED_BIT)) {
      esp_smartconfig_stop();
      ESP_LOGI(TAG, "smartconfig over");
      vTaskDelete(NULL);
      return;
    }
  }
}

static void
try_to_initialize_wifi_unless_connected(void *param)
{
  WifiInfo *wifi_info = (WifiInfo *) param;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(60000));
    EventBits_t uxBits = xEventGroupGetBits(wifi_info->event_group);
    ESP_LOGI(TAG, "Connected bits: %d %d", uxBits, uxBits & CONNECTED_BIT);
    if (uxBits & CONNECTED_BIT) {
      continue;
    }

    BaseType_t sem_take = xSemaphoreTake(wifi_info->wifi_semaphore, portMAX_DELAY);
    if (sem_take == pdFALSE) {
      ESP_LOGE(TAG, "Horrible error taking semaphore");
      continue;
    }

    uxBits = xEventGroupGetBits(wifi_info->event_group);
    if (!(uxBits & CONNECTED_BIT)) {
      esp_err_t err = esp_wifi_connect();
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect wifi: %s", esp_err_to_name(err));
      }
    }
    
    BaseType_t sem_give = xSemaphoreGive(wifi_info->wifi_semaphore);
    if (sem_give == pdFALSE) {
      ESP_LOGE(TAG, "Horrible error giving semaphore");
    }
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
  esp_err_t err;
  
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    BaseType_t rtos_err = xTaskCreate(smartconfig_config, "smartconfig_config",
                                      4096, wifi_info, 3, NULL);
    if (rtos_err != pdPASS) {
      ESP_LOGE(TAG, "Failed to create smartconfig_config task");
      return;
    }
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(wifi_info->event_group, CONNECTED_BIT);
    xEventGroupClearBits(wifi_info->event_group, GOT_IP_BIT);
    xEventGroupClearBits(wifi_info->event_group, ESPTOUCH_DONE_BIT);

    // Restart smartconfig
    BaseType_t rtos_err = xTaskCreate(smartconfig_config, "smartconfig_config",
                                      4096, wifi_info, 3, NULL);
    if (rtos_err != pdPASS) {
      ESP_LOGE(TAG, "Failed to create smartconfig_config task");
      return;
    }
    
    BaseType_t got_semaphore = xSemaphoreTake(wifi_info->wifi_semaphore, portMAX_DELAY);
    if (got_semaphore == pdFALSE) {
      ESP_LOGE(TAG, "Failed to wait for semaphore");
      abort();
    }

    err = esp_wifi_connect();
    BaseType_t sem_res = xSemaphoreGive(wifi_info->wifi_semaphore);
    if (sem_res == pdFALSE) {
      ESP_LOGE(TAG, "Failed to give semaphore");
    }

    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to connect to wifi: %s", esp_err_to_name(err));
      return;
    }
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    xEventGroupSetBits(wifi_info->event_group, CONNECTED_BIT);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(wifi_info->event_group, GOT_IP_BIT);
  } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
    BaseType_t got_semaphore = xSemaphoreTake(wifi_info->wifi_semaphore, portMAX_DELAY);
    if (got_semaphore == pdFALSE) {
      ESP_LOGE(TAG, "Failed to wait for semaphore");
      abort();
    }
                                   
    smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
    wifi_config_t wifi_config = {0};

    memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
    wifi_config.sta.bssid_set = evt->bssid_set;
    if (wifi_config.sta.bssid_set) {
      memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
    }

    err = esp_wifi_disconnect();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to disconnect from wifi: %s", esp_err_to_name(err));
    }
    
    err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to configure wifi: %s", esp_err_to_name(err));
      BaseType_t sem_res = xSemaphoreGive(wifi_info->wifi_semaphore);
      if (sem_res == pdFALSE) {
        ESP_LOGE(TAG, "Failed to give semaphore");
      }
      return;
    }
    
    err = esp_wifi_connect();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to connect wifi: %s", esp_err_to_name(err));
    }
    
    BaseType_t sem_res = xSemaphoreGive(wifi_info->wifi_semaphore);
    if (sem_res == pdFALSE) {
      ESP_LOGE(TAG, "Failed to give semaphore");
    }
  } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
    xEventGroupSetBits(wifi_info->event_group, ESPTOUCH_DONE_BIT);
  }
}


esp_err_t initialize_wifi_with_smartconfig(WifiInfo *wifi_info)
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
    
  err = nvs_flash_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize nvs flash: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_netif_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(err));
    return err;
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

  err = esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID,
                                   &wifi_event_handler, wifi_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register smartconfig handler: %s", esp_err_to_name(err));
    goto cleanup;
  }

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize WIFI: %s", esp_err_to_name(err));
    goto cleanup;
  }

  ESP_LOGI(TAG, "Starting wifi");
  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start WIFI: %s", esp_err_to_name(err));
    goto cleanup;
  }

  BaseType_t rtos_err = xTaskCreate(try_to_initialize_wifi_unless_connected, "maintain_wifi",
                                    4096, wifi_info, 1, NULL);
  if (rtos_err != pdPASS) {
    ESP_LOGE(TAG, "Failed to start wifi maintenance task");
  }

  xSemaphoreGive(wifi_info->wifi_semaphore);
  return ESP_OK;
  
 cleanup:
  
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
