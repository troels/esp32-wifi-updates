#ifndef MQTT_H
#define MQTT_H

#include "mqtt_client.h"

esp_err_t create_mqtt_client(char *broker_url, esp_mqtt_client_handle_t *client);

#endif
