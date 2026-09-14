#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lvgl_port.h"

static const char *TAG = "lv_port";
static SemaphoreHandle_t lvgl_mux;
static TaskHandle_t lvgl_task_handle = NULL;

static esp_lcd_touch_handle_t touch_handle = NULL;
static TaskHandle_t touch_task_handle = NULL;
static StackType_t *touch_task_stack = NULL;
static StaticTask_t touch_task_tcb;
static portMUX_TYPE touch_cache_lock = portMUX_INITIALIZER_UNLOCKED;
static lv_indev_data_t touch_cache = {
    .state = LV_INDEV_STATE_RELEASED,
};

static _Atomic uint32_t s_vsync_count;
static _Atomic uint32_t s_vsync_timeout_count;
static esp_timer_handle_t s_vsync_health_timer = NULL;

static void request_rgb_panel_restart(esp_lcd_panel_handle_t panel_handle)
{
    const esp_err_t err = esp_lcd_rgb_panel_restart(panel_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_rgb_panel_restart failed: %s", esp_err_to_name(err));
    }
}

static void log_vsync_timeout(uint32_t total)
{
    if (total == 1 || (total % 10) == 0) {
        ESP_LOGW(TAG, "VSYNC wait timeout (total=%lu)", (unsigned long)total);
    }
}

static void vsync_health_timer_cb(void *arg)
{
    (void)arg;

    static uint32_t last_vsync_count;
    static uint32_t last_timeout_count;

    const uint32_t vsync = atomic_load(&s_vsync_count);
    const uint32_t timeouts = atomic_load(&s_vsync_timeout_count);
    const uint32_t vsync_delta = vsync - last_vsync_count;
    const uint32_t timeout_delta = timeouts - last_timeout_count;

    last_vsync_count = vsync;
    last_timeout_count = timeouts;

    if (vsync_delta < PROJECT_LVGL_PORT_VSYNC_HEALTH_MIN) {
        ESP_LOGW(TAG, "VSYNC health: only %lu events in %dms (timeouts +%lu, total vsync=%lu)",
                 (unsigned long)vsync_delta, PROJECT_LVGL_PORT_VSYNC_HEALTH_MS,
                 (unsigned long)timeout_delta, (unsigned long)vsync);
    } else {
        ESP_LOGD(TAG, "VSYNC health: %lu events in %dms (timeouts +%lu)",
                 (unsigned long)vsync_delta, PROJECT_LVGL_PORT_VSYNC_HEALTH_MS,
                 (unsigned long)timeout_delta);
    }
}

static esp_err_t vsync_health_timer_start(void)
{
    const esp_timer_create_args_t args = {
        .callback = &vsync_health_timer_cb,
        .name = "vsync_health",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_vsync_health_timer));
    return esp_timer_start_periodic(s_vsync_health_timer, PROJECT_LVGL_PORT_VSYNC_HEALTH_MS * 1000);
}

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    (void)area;

    if (lv_display_flush_is_last(disp)) {
        esp_cache_msync(px_map, LVGL_PORT_FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

        if (lvgl_task_handle) {
            ulTaskNotifyValueClear(lvgl_task_handle, ULONG_MAX);
        }
        /* 全屏切换帧缓冲，避免 DIRECT 局部刷新与扫描输出重叠（重影/条纹） */
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LVGL_PORT_H_RES, LVGL_PORT_V_RES, px_map);
        if (lvgl_task_handle) {
            const TickType_t wait_ticks = pdMS_TO_TICKS(PROJECT_LVGL_PORT_VSYNC_WAIT_MS);
            if (ulTaskNotifyTake(pdTRUE, wait_ticks) == 0) {
                const uint32_t total = atomic_fetch_add(&s_vsync_timeout_count, 1) + 1;
                log_vsync_timeout(total);
                request_rgb_panel_restart(panel_handle);
            }
        }
    }

    lv_display_flush_ready(disp);
}

static lv_display_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    assert(panel_handle);

    void *buf1 = NULL;
    void *buf2 = NULL;
    const int buffer_size = LVGL_PORT_H_RES * LVGL_PORT_V_RES;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));

    lv_display_t *disp = lv_display_create(LVGL_PORT_H_RES, LVGL_PORT_V_RES);
    if (!disp) {
        ESP_LOGE(TAG, "创建显示设备失败");
        return NULL;
    }

    lv_display_set_buffers(disp, buf1, buf2, buffer_size * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_callback);
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_FULL);

    return disp;
}

static void touch_poll_task(void *arg)
{
    (void)arg;
    esp_lcd_touch_point_data_t point = {0};
    uint8_t cnt = 0;
    bool last_pressed = false;

    while (1) {
        if (touch_handle) {
            if (esp_lcd_touch_read_data(touch_handle) == ESP_OK) {
                cnt = 0;
                const esp_err_t err = esp_lcd_touch_get_data(touch_handle, &point, &cnt, 1);
                const bool pressed = (err == ESP_OK && cnt > 0);

                portENTER_CRITICAL(&touch_cache_lock);
                if (pressed) {
                    touch_cache.point.x = point.x;
                    touch_cache.point.y = point.y;
                    touch_cache.state = LV_INDEV_STATE_PRESSED;
                } else {
                    touch_cache.state = LV_INDEV_STATE_RELEASED;
                }
                portEXIT_CRITICAL(&touch_cache_lock);

                if (pressed) {
                    ESP_LOGI(TAG, "touch raw: x=%u y=%u", (unsigned)point.x, (unsigned)point.y);
                    last_pressed = true;
                } else if (last_pressed) {
                    ESP_LOGI(TAG, "touch released");
                    last_pressed = false;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(PROJECT_LVGL_PORT_TOUCH_POLL_MS));
    }
}

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    portENTER_CRITICAL(&touch_cache_lock);
    *data = touch_cache;
    portEXIT_CRITICAL(&touch_cache_lock);
}

static esp_err_t touch_task_start(esp_lcd_touch_handle_t tp)
{
    touch_handle = tp;
    touch_task_stack = heap_caps_malloc(LVGL_PORT_TOUCH_TASK_STACK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!touch_task_stack) {
        ESP_LOGE(TAG, "触摸任务栈分配失败");
        return ESP_ERR_NO_MEM;
    }
    touch_task_handle = xTaskCreateStatic(touch_poll_task, "touch", LVGL_PORT_TOUCH_TASK_STACK_SIZE, NULL,
                                          LVGL_PORT_TASK_PRIORITY - 1, touch_task_stack, &touch_task_tcb);
    if (!touch_task_handle) {
        ESP_LOGE(TAG, "创建触摸任务失败");
        heap_caps_free(touch_task_stack);
        touch_task_stack = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp, lv_display_t *disp)
{
    assert(tp);
    assert(disp);

    lv_indev_t *indev = lv_indev_create();
    if (!indev) {
        ESP_LOGE(TAG, "创建输入设备失败");
        return NULL;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_indev_set_user_data(indev, tp);
    lv_indev_set_display(indev, disp);

    return indev;
}

static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static esp_err_t tick_init(void)
{
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &tick_increment,
        .name = "LVGL tick",
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    return esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000);
}

static void lvgl_port_task(void *arg)
{
    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(0)) {
            task_delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle)
{
    lv_init();
    ESP_ERROR_CHECK(tick_init());

    lv_display_t *disp = display_init(lcd_handle);
    assert(disp);

    if (tp_handle) {
        lv_indev_t *indev = indev_init(tp_handle, disp);
        assert(indev);
        ESP_ERROR_CHECK(touch_task_start(tp_handle));
    }

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);

    ESP_LOGI(TAG, "创建 LVGL 任务 (vsync_wait=%dms, health=%dms)",
             PROJECT_LVGL_PORT_VSYNC_WAIT_MS, PROJECT_LVGL_PORT_VSYNC_HEALTH_MS);
    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                                             LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建 LVGL 任务失败");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_task_wdt_add(lvgl_task_handle));
    ESP_ERROR_CHECK(vsync_health_timer_start());

    return ESP_OK;
}

bool lvgl_port_lock(int timeout_ms)
{
    assert(lvgl_mux && "必须先调用 lvgl_port_init");

    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(lvgl_mux && "必须先调用 lvgl_port_init");
    xSemaphoreGiveRecursive(lvgl_mux);
}

bool IRAM_ATTR lvgl_port_notify_rgb_vsync(void)
{
    atomic_fetch_add(&s_vsync_count, 1);

    if (!lvgl_task_handle) {
        return false;
    }
    BaseType_t need_yield = pdFALSE;
    vTaskNotifyGiveFromISR(lvgl_task_handle, &need_yield);
    return (need_yield == pdTRUE);
}
