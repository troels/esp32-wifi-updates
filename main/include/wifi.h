#ifndef WIFI_H
#define WIFI_H

#include "freertos/event_groups.h"
#include "esp_event.h"

typedef struct {
  EventGroupHandle_t event_group;
  esp_event_loop_handle_t event_loop;
} WifiInfo;

esp_err_t initialize_wifi_with_smartconfig(WifiInfo *wifi_info)
  
#endif
