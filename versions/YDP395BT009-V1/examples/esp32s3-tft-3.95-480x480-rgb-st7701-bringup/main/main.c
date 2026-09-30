/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Modifications Copyright 2026 OSPTEK
 * https://github.com/osptek
 */

#include "app_init.h"
#include "lvgl_port.h"
#include "lv_demos.h"

void app_main(void)
{
    app_init();

    if (lvgl_port_lock(-1))
    {
        lv_demo_widgets();
        lvgl_port_unlock();
    }
}