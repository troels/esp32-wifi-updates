#include <stdio.h>
#include "sdkconfig.h"
#include "am2320.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/task.h"


#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define ACK_CHECK_EN 0x1            /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0           /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                 /*!< I2C ack value */
#define NACK_VAL 0x1                /*!< I2C nack value */

static const uint8_t AM2320_ADDRESS = 0x5C;
static const char *TAG = "AM2320";

static unsigned short
crc16(unsigned char *buf, int len)
{
  unsigned short crc=0xFFFF;
  while (len--) {
    crc ^= *buf++;
    for(int i = 0; i < 8; i++) {
      if (crc & 0x01) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  
  return crc;
}


static esp_err_t
install_driver(int i2c_num, int sda, int scl)
{
  i2c_config_t conf = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = sda,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_io_num = scl,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = 100000U
  };
  esp_err_t err;
  err = i2c_param_config(i2c_num, &conf);
  if (err != ESP_OK) {
    return err;
  }
  err = i2c_driver_install(i2c_num,
                           I2C_MODE_MASTER,
                           I2C_MASTER_RX_BUF_DISABLE,
                           I2C_MASTER_TX_BUF_DISABLE,
                           0);
  if (err != ESP_OK) {
    return err;
  }


  return ESP_OK;
}

static esp_err_t
uninstall(int i2c_num)
{
  return i2c_driver_delete(i2c_num);
}

static esp_err_t
wakeup(int i2c_num)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (AM2320_ADDRESS << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
  i2c_master_stop(cmd);
  int ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
  i2c_cmd_link_delete(cmd);
  return ret;
}

static esp_err_t
perform_measurement(int i2c_num, am2320_measurement *out)
{
  unsigned char buf[8];
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (AM2320_ADDRESS << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
  i2c_master_write_byte(cmd, 0x03, ACK_CHECK_EN);
  i2c_master_write_byte(cmd, 0x00, ACK_CHECK_EN);
  i2c_master_write_byte(cmd, 0x04, ACK_CHECK_EN);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
  i2c_cmd_link_delete(cmd);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s(%d): Failed to write to AM2320", __FUNCTION__, __LINE__);
    return ret;
  }
  vTaskDelay(2 / portTICK_RATE_MS);
  
  cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (AM2320_ADDRESS << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
  i2c_master_read(cmd, buf, 8, I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd);
  ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
  i2c_cmd_link_delete(cmd);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s(%d): Failed to read from AM2320", __FUNCTION__, __LINE__);
    return ret;
  }

  unsigned short crc = (((int)buf[7]) << 8) + ((int)buf[6]);
  if (crc != crc16(buf, 6)) {
    return ESP_ERR_INVALID_CRC;
  }

  float humidity = (((int)buf[2]) << 8) + buf[3];
  humidity /= 10;
  float temperature = (((int)buf[4]) << 8) + buf[5];
  temperature /= 10;
  out->temperature = temperature;
  out->relative_humidity = humidity;
  return ESP_OK;
}


esp_err_t
am2320_measure(int i2c_num, int sda, int scl, am2320_measurement *out)
{
  esp_err_t err;
  int try_num_times = 5;
  if (out == NULL) {
    ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, "out argument is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if ((err = install_driver(i2c_num, sda, scl)) != ESP_OK) {
    ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, "failed to initialize i2c");
    return err;
  }
  wakeup(i2c_num);
  vTaskDelay(10 / portTICK_RATE_MS);

  while (try_num_times--) {
    err = perform_measurement(i2c_num, out);
    if (err == ESP_OK) {
      err = uninstall(i2c_num);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s(%d): Failed to uninstall I2C Driver", __FUNCTION__, __LINE__);
      }
      return ESP_OK;
    }
    ESP_LOGW(TAG, "%s(%d) Failed to read from AM2320", __FUNCTION__, __LINE__);
  }

  uninstall(i2c_num);
  return err;
}
