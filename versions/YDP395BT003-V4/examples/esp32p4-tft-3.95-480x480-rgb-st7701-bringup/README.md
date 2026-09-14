# ESP32-P4 · ST7701 RGB + FT6336U 触摸 bringup

面向 **YDP395BT003-V4** 的点亮例程：ESP32-P4 + ST7701（RGB）+ 本地组件 `esp_lcd_touch_ft6336u`（FT6x36）+ LVGL9 Widgets Demo。

## 依赖

- ESP-IDF（建议 5.x）
- 组件见 `main/idf_component.yml`（`esp_lcd_st7701`、`esp_lcd_touch`、`lvgl` 等）
- 触摸驱动：`components/esp_lcd_touch_ft6336u/`

## 编译与烧录

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p <PORT> flash monitor
```

首次构建会下载 `managed_components/`（已在 `.gitignore` 中忽略）。
