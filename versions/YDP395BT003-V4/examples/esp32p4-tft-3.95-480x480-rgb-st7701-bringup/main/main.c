/*
 * SPDX-FileCopyrightText: Copyright 2026 OSPTEK
 * SPDX-License-Identifier: CC-BY-4.0
 *
 * https://github.com/osptek
 */

#include "app_init.h"
#include "lv_demos.h"
#include "esp_log.h"
#include "lvgl.h"
#include "misc/lv_async.h"
#include "widgets/lv_demo_widgets.h"

static const char *TAG = "main";

static void run_demo_async(void *user_data)
{
    (void)user_data;
    lv_demo_widgets();
}

void app_main(void)
{
    app_init();
    ESP_LOGI(TAG, "LVGL stress demo (auto-animated)");
    lv_async_call(run_demo_async, NULL);
}
