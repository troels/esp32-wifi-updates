#ifndef AM2320_H
#define AM2320_H

#include "esp_err.h"

typedef struct {
  float temperature;
  float relative_humidity;
} am2320_measurement;

esp_err_t am2320_measure(int i2c_num, int sda, int scl, am2320_measurement *out);

#endif
