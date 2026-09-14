#pragma once

/* ESP32-P4 片内 LDO 输出（VDDO_FLASH/PSRAM/3/4，与 sdkconfig 闪存/PSRAM 域一致） */
#define PROJECT_LDO_VO1_VOLTAGE_MV           3300 /* VDDO_FLASH */
#define PROJECT_LDO_VO2_VOLTAGE_MV           1800 /* VDDO_PSRAM，须与 sdkconfig CONFIG_ESP_LDO_VOLTAGE_PSRAM_DOMAIN 一致 */
#define PROJECT_LDO_VO3_VOLTAGE_MV           3300 /* VDDO_3 */
#define PROJECT_LDO_VO4_VOLTAGE_MV           3300 /* VDDO_4 → VDD_IO_5 */

/* LCD RGB（与 S3 reference 一致：init 表 + LVGL widgets + 触摸） */
#define PROJECT_LCD_RGB_BOUNCE_BUFFER_HEIGHT 0
#define PROJECT_LCD_BK_LIGHT_ON_LEVEL        1
#define PROJECT_LCD_PANEL_BPP                18 /* RGB666 COLMOD */
#define PROJECT_LCD_PCLK_HZ                  (12 * 1000 * 1000)

/* LVGL 任务 */
#define PROJECT_LVGL_PORT_TICK               2
#define PROJECT_LVGL_PORT_TASK_MAX_DELAY_MS  500
#define PROJECT_LVGL_PORT_TASK_MIN_DELAY_MS  1
#define PROJECT_LVGL_PORT_TASK_PRIORITY      5
#define PROJECT_LVGL_PORT_TASK_STACK_SIZE_KB 16
#define PROJECT_LVGL_PORT_TOUCH_POLL_MS      20
#define PROJECT_LVGL_PORT_TOUCH_TASK_STACK_KB 4
#define PROJECT_LVGL_PORT_TASK_CORE          (-1)
#define PROJECT_LVGL_PORT_VSYNC_WAIT_MS      200  /* flush 等 VSYNC 超时，避免永久阻塞 */
#define PROJECT_LVGL_PORT_VSYNC_HEALTH_MS     3000 /* 周期性检查 VSYNC 计数 */
#define PROJECT_LVGL_PORT_VSYNC_HEALTH_MIN    10   /* 健康窗口内最少 VSYNC 次数（约 3fps） */
