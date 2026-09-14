#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include "project_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define LVGL_PORT_H_RES (480)
#define LVGL_PORT_V_RES (480)
#define LVGL_PORT_TICK_PERIOD_MS (PROJECT_LVGL_PORT_TICK)

#define LVGL_PORT_TASK_MAX_DELAY_MS (PROJECT_LVGL_PORT_TASK_MAX_DELAY_MS)
#define LVGL_PORT_TASK_MIN_DELAY_MS (PROJECT_LVGL_PORT_TASK_MIN_DELAY_MS)
#define LVGL_PORT_TASK_STACK_SIZE (PROJECT_LVGL_PORT_TASK_STACK_SIZE_KB * 1024)
#define LVGL_PORT_TOUCH_TASK_STACK_SIZE (PROJECT_LVGL_PORT_TOUCH_TASK_STACK_KB * 1024)
#define LVGL_PORT_TASK_PRIORITY (PROJECT_LVGL_PORT_TASK_PRIORITY)
#define LVGL_PORT_TASK_CORE (PROJECT_LVGL_PORT_TASK_CORE)

#define LVGL_PORT_LCD_RGB_BUFFER_NUMS (2)
#define LVGL_PORT_FB_BYTES            (LVGL_PORT_H_RES * LVGL_PORT_V_RES * sizeof(lv_color_t))

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle);

bool lvgl_port_lock(int timeout_ms);

void lvgl_port_unlock(void);

bool lvgl_port_notify_rgb_vsync(void);

#ifdef __cplusplus
}
#endif
