/*
 * SPDX-FileCopyrightText: 2025 CFSoft Systems (Chengdu) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Local bringup copy: FT5x06-style point read + G_MODE polling.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

static const char *TAG = "FT6x36";

#define FT62XX_REG_MODE        0x00
#define FT62XX_REG_NUMTOUCHES  0x02
#define FT62XX_REG_P1_XH       0x03
#define FT62XX_REG_WORKMODE    0x00
#define FT62XX_REG_THRESHHOLD  0x80
#define FT62XX_REG_POINTRATE   0x88
#define FT62XX_REG_FIRMVERS    0xA6
#define FT62XX_REG_CHIPID      0xA3
#define FT62XX_REG_VENDID      0xA8
#define FT62XX_REG_GMODE       0xA4

#define FT62XX_VENDID    0x11
#define FT6336_VENDID    0x88
#define FT6206_CHIPID    0x06
#define FT3236_CHIPID    0x33
#define FT6236_CHIPID    0x36
#define FT6236U_CHIPID   0x64
#define FT6336U_CHIPID   0x64

#define FT62XX_DEFAULT_THRESHOLD 70
#define FT62XX_DEFAULT_POINTRATE 12
#define FT62XX_GMODE_POLLING     0x00

static esp_err_t esp_lcd_touch_ft6x36_read_data(esp_lcd_touch_handle_t tp);
static bool esp_lcd_touch_ft6x36_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
static esp_err_t esp_lcd_touch_ft6x36_del(esp_lcd_touch_handle_t tp);
static esp_err_t touch_ft6x36_i2c_write(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t data);
static esp_err_t touch_ft6x36_i2c_read(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t *data, uint8_t len);
static esp_err_t touch_ft6x36_init(esp_lcd_touch_handle_t tp);
static esp_err_t touch_ft6x36_reset(esp_lcd_touch_handle_t tp);

esp_err_t esp_lcd_touch_new_i2c_ft6x36(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch)
{
    esp_err_t ret = ESP_OK;

    assert(config != NULL);
    assert(out_touch != NULL);

    esp_lcd_touch_handle_t esp_lcd_touch_ft6x36 = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(esp_lcd_touch_ft6x36, ESP_ERR_NO_MEM, err, TAG, "no mem for FT6x36 controller");

    esp_lcd_touch_ft6x36->io = io;
    esp_lcd_touch_ft6x36->read_data = esp_lcd_touch_ft6x36_read_data;
    esp_lcd_touch_ft6x36->get_xy = esp_lcd_touch_ft6x36_get_xy;
    esp_lcd_touch_ft6x36->del = esp_lcd_touch_ft6x36_del;
    esp_lcd_touch_ft6x36->data.lock.owner = portMUX_FREE_VAL;
    memcpy(&esp_lcd_touch_ft6x36->config, config, sizeof(esp_lcd_touch_config_t));

    if (esp_lcd_touch_ft6x36->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .intr_type = (esp_lcd_touch_ft6x36->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(esp_lcd_touch_ft6x36->config.int_gpio_num),
        };
        ret = gpio_config(&int_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");
        if (esp_lcd_touch_ft6x36->config.interrupt_callback) {
            esp_lcd_touch_register_interrupt_callback(esp_lcd_touch_ft6x36, esp_lcd_touch_ft6x36->config.interrupt_callback);
        }
    }

    if (esp_lcd_touch_ft6x36->config.rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(esp_lcd_touch_ft6x36->config.rst_gpio_num),
        };
        ret = gpio_config(&rst_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");
    }

    ret = touch_ft6x36_reset(esp_lcd_touch_ft6x36);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "FT6x36 reset failed");

    ret = touch_ft6x36_init(esp_lcd_touch_ft6x36);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "FT6x36 init failed");

    *out_touch = esp_lcd_touch_ft6x36;

err:
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (0x%x)! Touch controller FT6x36 initialization failed!", ret);
        if (esp_lcd_touch_ft6x36) {
            esp_lcd_touch_ft6x36_del(esp_lcd_touch_ft6x36);
        }
    }

    return ret;
}

static esp_err_t esp_lcd_touch_ft6x36_read_data(esp_lcd_touch_handle_t tp)
{
    esp_err_t err;
    uint8_t points = 0;
    uint8_t data[12];
    size_t i = 0;

    assert(tp != NULL);

    /* 与官方 FT5x06 相同：先读点数，再从 P1_XH 连续读坐标，避免 0x00 起读整块丢点 */
    err = touch_ft6x36_i2c_read(tp, FT62XX_REG_NUMTOUCHES, &points, 1);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

    points &= 0x0F;
    if (points == 0 || points > 2) {
        portENTER_CRITICAL(&tp->data.lock);
        tp->data.points = 0;
        portEXIT_CRITICAL(&tp->data.lock);
        return ESP_OK;
    }

    if (points > CONFIG_ESP_LCD_TOUCH_MAX_POINTS) {
        points = CONFIG_ESP_LCD_TOUCH_MAX_POINTS;
    }

    err = touch_ft6x36_i2c_read(tp, FT62XX_REG_P1_XH, data, 6 * points);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = points;
    for (i = 0; i < points; i++) {
        tp->data.coords[i].x = ((uint16_t)(data[(i * 6) + 0] & 0x0F) << 8) | data[(i * 6) + 1];
        tp->data.coords[i].y = ((uint16_t)(data[(i * 6) + 2] & 0x0F) << 8) | data[(i * 6) + 3];
        tp->data.coords[i].strength = 0;
    }
    portEXIT_CRITICAL(&tp->data.lock);

    return ESP_OK;
}

static bool esp_lcd_touch_ft6x36_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    assert(tp != NULL);
    assert(x != NULL);
    assert(y != NULL);
    assert(point_num != NULL);
    assert(max_point_num > 0);

    portENTER_CRITICAL(&tp->data.lock);

    *point_num = (tp->data.points > max_point_num ? max_point_num : tp->data.points);
    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }
    tp->data.points = 0;

    portEXIT_CRITICAL(&tp->data.lock);

    return (*point_num > 0);
}

static esp_err_t esp_lcd_touch_ft6x36_del(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback) {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
    }
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }

    free(tp);
    return ESP_OK;
}

static esp_err_t touch_ft6x36_init(esp_lcd_touch_handle_t tp)
{
    esp_err_t ret = ESP_OK;
    uint8_t vend_id, chip_id, firm_vers, point_rate, thresh;

    assert(tp != NULL);

    ESP_RETURN_ON_ERROR(touch_ft6x36_i2c_read(tp, FT62XX_REG_VENDID, &vend_id, 1), TAG, "Read vendor ID error");
    ESP_RETURN_ON_ERROR(touch_ft6x36_i2c_read(tp, FT62XX_REG_CHIPID, &chip_id, 1), TAG, "Read chip ID error");
    ESP_RETURN_ON_ERROR(touch_ft6x36_i2c_read(tp, FT62XX_REG_FIRMVERS, &firm_vers, 1), TAG, "Read firmware version error");
    ESP_RETURN_ON_ERROR(touch_ft6x36_i2c_read(tp, FT62XX_REG_POINTRATE, &point_rate, 1), TAG, "Read point rate error");
    ESP_RETURN_ON_ERROR(touch_ft6x36_i2c_read(tp, FT62XX_REG_THRESHHOLD, &thresh, 1), TAG, "Read threshold error");

    ESP_LOGI(TAG, "Vend ID: 0x%02X", vend_id);
    ESP_LOGI(TAG, "Chip ID: 0x%02X", chip_id);
    ESP_LOGI(TAG, "Firm V: %d", firm_vers);
    ESP_LOGI(TAG, "Point Rate: %d", point_rate);
    ESP_LOGI(TAG, "Thresh: %d", thresh);

    if (vend_id != FT62XX_VENDID && vend_id != FT6336_VENDID) {
        ESP_LOGE(TAG, "Invalid vendor ID: 0x%02X", vend_id);
        return ESP_FAIL;
    }
    if (chip_id != FT6206_CHIPID && chip_id != FT6236_CHIPID && chip_id != FT6236U_CHIPID &&
        chip_id != FT6336U_CHIPID && chip_id != FT3236_CHIPID) {
        ESP_LOGE(TAG, "Unsupported chip ID: 0x%02X", chip_id);
        return ESP_FAIL;
    }

    /* 工作模式 + 轮询模式（不用 INT）+ 合理阈值/阈值 */
    ret |= touch_ft6x36_i2c_write(tp, FT62XX_REG_MODE, FT62XX_REG_WORKMODE);
    ret |= touch_ft6x36_i2c_write(tp, FT62XX_REG_GMODE, FT62XX_GMODE_POLLING);
    ret |= touch_ft6x36_i2c_write(tp, FT62XX_REG_THRESHHOLD, FT62XX_DEFAULT_THRESHOLD);
    ret |= touch_ft6x36_i2c_write(tp, FT62XX_REG_POINTRATE, FT62XX_DEFAULT_POINTRATE);

    return ret;
}

static esp_err_t touch_ft6x36_reset(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    return ESP_OK;
}

static esp_err_t touch_ft6x36_i2c_write(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t data)
{
    assert(tp != NULL);
    return esp_lcd_panel_io_tx_param(tp->io, reg, (uint8_t[]){data}, 1);
}

static esp_err_t touch_ft6x36_i2c_read(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t *data, uint8_t len)
{
    assert(tp != NULL);
    assert(data != NULL);
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}
