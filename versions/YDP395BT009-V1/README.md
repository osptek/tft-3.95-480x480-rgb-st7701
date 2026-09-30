<p align="left"><img alt="OSPTEK" src="./images/logo.png" width="200" /></p>

<h1 align="center">OSPTEK 3.95″ TFT 480×480（ST7701 · RGB）</h1>

<p align="center"><b>方形 TFT 模组 · RGB · ST7701</b></p>

<p align="center"><a href="./README_EN.md">English</a> | 简体中文 · <a href="../../README.md">规格族索引</a></p>

<p align="center">
  <img alt="Size: 3.95 inch" src="https://img.shields.io/badge/Size-3.95%22-3498DB?style=flat-square" />
  <img alt="Resolution: 480x480" src="https://img.shields.io/badge/Resolution-480%C3%97480-8E44AD?style=flat-square" />
  <img alt="Interface: RGB" src="https://img.shields.io/badge/Interface-RGB-27AE60?style=flat-square" />
  <img alt="Driver: ST7701" src="https://img.shields.io/badge/Driver-ST7701-E7352C?style=flat-square" />
</p>

<p align="center"><img alt="OSPTEK 3.95 寸 480×480 TFT RGB 模组（ST7701）宣传图" src="./images/product.png" width="640" /></p>

## 目录

- [产品简介](#产品简介)
- [规格参数](#规格参数)
- [示例工程](#示例工程)
- [仓库结构](#仓库结构)
- [相关资料](#相关资料)
- [购买链接](#购买链接)
- [技术支持](#技术支持)

---

## 产品简介

OSPTEK **3.95 寸 480×480 TFT** 是一款 **RGB** 接口彩色显示模组，显示驱动为 **ST7701S**（面板初始化经 3-wire SPI），触摸驱动为 **FT6336U**。适合方形 HMI、仪表与中尺寸交互面板等场景。

规格标识（仓库名）：`tft-3.95-480x480-rgb-st7701`

当前模组版本：**YDP395BT009-V1**。电气与外形细节以 [`docs/YDP395BT009-V1.pdf`](./docs/YDP395BT009-V1.pdf) 为准。

## 规格参数

| 项目 | 规格 |
| ---- | ---- |
| 尺寸 | 3.95 英寸 |
| 类型 | TFT / IPS（彩色） |
| 分辨率 | 480×480 |
| 接口 | RGB 18-bit（初始化：3-wire SPI） |
| 驱动 IC | ST7701S |
| 触摸驱动 | FT6336U |

> 完整外形尺寸、FPC 定义、供电与时序以产品规格书 / 驱动手册为准。

## 示例工程

| 说明 | 路径 |
| ---- | ---- |
| ESP32-S3 · ST7701 RGB + LVGL9（触摸 FT6336U，`esp_lcd_touch_ft6336u`） | [`examples/esp32s3-tft-3.95-480x480-rgb-st7701-bringup/`](./examples/esp32s3-tft-3.95-480x480-rgb-st7701-bringup/) |
| ESP32-P4 · ST7701 RGB + LVGL9（触摸 FT6336U，`esp_lcd_touch_ft6336u`） | [`examples/esp32p4-tft-3.95-480x480-rgb-st7701-bringup/`](./examples/esp32p4-tft-3.95-480x480-rgb-st7701-bringup/) |

## 仓库结构

```text
tft-3.95-480x480-rgb-st7701/                                # 仓库根（导航见 ../../README.md）
└── versions/
    └── YDP395BT009-V1/                                # 本料号完整资料
        ├── README.md
        ├── README_EN.md
        ├── images/
        ├── docs/
        └── examples/
```

## 相关资料

### 本产品资料

| 资料 | 链接 |
| ---- | ---- |
| 产品规格书（YDP395BT009-V1） | [`docs/YDP395BT009-V1.pdf`](./docs/YDP395BT009-V1.pdf) |
| 总成图 CAD（YDP395BT009-V1） | [`docs/YDP395BT009-V1.dwg`](./docs/YDP395BT009-V1.dwg) |
| 驱动 IC 数据手册（ST7701S） | [`docs/ST7701S_SPEC_V1.3.pdf`](./docs/ST7701S_SPEC_V1.3.pdf) |
| 触摸 IC 数据手册（FT6336U） | [`docs/FT6336U_DataSheet_V1.1.pdf`](./docs/FT6336U_DataSheet_V1.1.pdf) |
| 初始化序列（文本） | [`docs/BOE3.95_480x480_ST7701S_init.txt`](./docs/BOE3.95_480x480_ST7701S_init.txt) |

### 示例工程

- [ESP32-S3 ST7701 RGB + LVGL9](./examples/esp32s3-tft-3.95-480x480-rgb-st7701-bringup/)
- [ESP32-P4 ST7701 RGB + LVGL9](./examples/esp32p4-tft-3.95-480x480-rgb-st7701-bringup/)

## 购买链接

<p align="center">
  <a href="https://shop110742373.taobao.com/"><img alt="淘宝官方店铺" src="https://img.shields.io/badge/淘宝-官方店铺-FF6A00?style=for-the-badge" /></a>
  &nbsp;&nbsp;
  <a href="https://www.aliexpress.com/store/1105701619"><img alt="速卖通官方店铺" src="https://img.shields.io/badge/速卖通-官方店铺-FF6A00?style=for-the-badge" /></a>
</p>

**国内（淘宝）**

- 店铺：[鱼鹰光电工厂店](https://shop110742373.taobao.com/)

**海外（AliExpress）**

- 店铺：[OSPTEK Official Store](https://www.aliexpress.com/store/1105701619)

## 技术支持

- 技术支持 / 产品咨询：<luyu@osptek.com>
- QQ 技术交流群：**985881096**
- 公司官网：<https://osptek.com/>
- 有任何问题，都可以在本仓库 Issues 中提问

---

<p align="center"><sub>© 2026 OSPTEK 鱼鹰光电 · 本仓库资料采用 CC BY 4.0 许可</sub></p>
