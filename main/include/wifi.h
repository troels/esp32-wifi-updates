#ifndef WIFI_H
#define WIFI_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_smartconfig.h"

typedef struct {
  EventGroupHandle_t event_group;
  SemaphoreHandle_t wifi_semaphore;
} WifiInfo;

esp_err_t initialize_wifi_with_provisioning(WifiInfo *wifi_info);
esp_err_t wait_for_connection(WifiInfo *info, TickType_t wait_time);
  
#endif
