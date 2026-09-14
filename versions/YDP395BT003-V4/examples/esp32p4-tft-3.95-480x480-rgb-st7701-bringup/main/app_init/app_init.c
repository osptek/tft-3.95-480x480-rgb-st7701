#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_touch_ft6x36.h"
#include "esp_lcd_st7701.h"
#include "lvgl_port.h"
#include "project_config.h"
#include "app_init.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// P4mini 点屏转接板引脚（3.95" 480x480 RGB ST7701）///////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_H_RES (480)
#define EXAMPLE_LCD_V_RES (480)
#define EXAMPLE_LCD_PCLK_HZ PROJECT_LCD_PCLK_HZ
#define EXAMPLE_LCD_BIT_PER_PIXEL (PROJECT_LCD_PANEL_BPP)
#define EXAMPLE_RGB_BIT_PER_PIXEL (16)
#define EXAMPLE_RGB_DATA_WIDTH (16)
#define EXAMPLE_RGB_BOUNCE_BUFFER_SIZE (EXAMPLE_LCD_H_RES * PROJECT_LCD_RGB_BOUNCE_BUFFER_HEIGHT)

/* 3-wire SPI 初始化 ST7701（转接板丝印）
 * MOSI=IO25, SCLK=IO26；无 CS 引出（板上硬接地，屏一直选中）
 * S3 参考板用 PCA9557 P7 做 CS，本板没有
 * esp_lcd 3wire 驱动 API 强制要求 cs_gpio_num，下面用未接到转接板的 GPIO 占位，不参与实际通信 */
#define EXAMPLE_LCD_IO_SPI_SDA (GPIO_NUM_25)
#define EXAMPLE_LCD_IO_SPI_SCL (GPIO_NUM_26)
#define EXAMPLE_LCD_IO_SPI_CS_STUB (GPIO_NUM_6)

#define EXAMPLE_LCD_IO_RGB_DISP (-1)
#define EXAMPLE_LCD_IO_RGB_VSYNC (GPIO_NUM_29)
#define EXAMPLE_LCD_IO_RGB_HSYNC (GPIO_NUM_30)
#define EXAMPLE_LCD_IO_RGB_DE    (GPIO_NUM_28)
#define EXAMPLE_LCD_IO_RGB_PCLK  (GPIO_NUM_27)

/* 转接板 GPIO→屏 DB 丝印 */
#define EXAMPLE_LCD_IO_RGB_DATA0  (GPIO_NUM_31)  /* DB0 */
#define EXAMPLE_LCD_IO_RGB_DATA1  (GPIO_NUM_33)  /* DB1 */
#define EXAMPLE_LCD_IO_RGB_DATA2  (GPIO_NUM_34)  /* DB2 */
#define EXAMPLE_LCD_IO_RGB_DATA3  (GPIO_NUM_36)  /* DB3 */
#define EXAMPLE_LCD_IO_RGB_DATA4  (GPIO_NUM_45)  /* DB4 */
#define EXAMPLE_LCD_IO_RGB_DATA5  (GPIO_NUM_46)  /* DB5 */
#define EXAMPLE_LCD_IO_RGB_DATA6  (GPIO_NUM_47)  /* DB6 */
#define EXAMPLE_LCD_IO_RGB_DATA7  (GPIO_NUM_48)  /* DB7 */
#define EXAMPLE_LCD_IO_RGB_DATA8  (GPIO_NUM_49)  /* DB8 */
#define EXAMPLE_LCD_IO_RGB_DATA9  (GPIO_NUM_50)  /* DB9 */
#define EXAMPLE_LCD_IO_RGB_DATA10 (GPIO_NUM_51)  /* DB10 */
#define EXAMPLE_LCD_IO_RGB_DATA11 (GPIO_NUM_52)  /* DB11 */
#define EXAMPLE_LCD_IO_RGB_DATA12 (GPIO_NUM_0)   /* DB12 */
#define EXAMPLE_LCD_IO_RGB_DATA13 (GPIO_NUM_1)   /* DB13 */
#define EXAMPLE_LCD_IO_RGB_DATA14 (GPIO_NUM_2)   /* DB14 */
#define EXAMPLE_LCD_IO_RGB_DATA15 (GPIO_NUM_3)   /* DB15 */
#define EXAMPLE_LCD_IO_RGB_DATA16 (GPIO_NUM_4)   /* DB16 */
#define EXAMPLE_LCD_IO_RGB_DATA17 (GPIO_NUM_5)   /* DB17 */

#define EXAMPLE_LCD_IO_RST (GPIO_NUM_24)
#define EXAMPLE_PIN_NUM_BK_LIGHT (GPIO_NUM_32)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL PROJECT_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL (!EXAMPLE_LCD_BK_LIGHT_ON_LEVEL)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// FT6336U 触摸：SDA=IO21, SCL=IO22, INT=IO20, RST=IO23 /////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_I2C_PORT (I2C_NUM_0)
#define EXAMPLE_PIN_NUM_I2C_SCL (GPIO_NUM_22)
#define EXAMPLE_PIN_NUM_I2C_SDA (GPIO_NUM_21)
/* 与 S3 reference 一致：驱动侧不接管 RST/INT。
 * 硬件 RST 接 IO23 时，在 init 前做一次加长复位（FT6336 上电需 >100ms）。 */
#define EXAMPLE_PIN_NUM_TOUCH_RST (GPIO_NUM_23)
#define EXAMPLE_PIN_NUM_TOUCH_INT (-1)

static const char *TAG = "app_init";
static i2c_master_bus_handle_t s_i2c_bus = NULL;

static void app_lcd_bk_light_set(bool on)
{
    const int level = on ? EXAMPLE_LCD_BK_LIGHT_ON_LEVEL : EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL;
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, level));
}

typedef struct {
    int chan_id;
    int voltage_mv;
    const char *name;
} app_ldo_chan_desc_t;

/* 使能 VO1~VO4；VO1/VO2 启动阶段可能已由 IDF 申请，同电压再次 acquire 仅增加引用计数 */
static void app_enable_all_ldo_channels(void)
{
    static const app_ldo_chan_desc_t channels[] = {
        {1, PROJECT_LDO_VO1_VOLTAGE_MV, "VO1/VDDO_FLASH"},
        {2, PROJECT_LDO_VO2_VOLTAGE_MV, "VO2/VDDO_PSRAM"},
        {3, PROJECT_LDO_VO3_VOLTAGE_MV, "VO3/VDDO_3"},
        {4, PROJECT_LDO_VO4_VOLTAGE_MV, "VO4/VDDO_4"},
    };

    for (int i = 0; i < (int)(sizeof(channels) / sizeof(channels[0])); i++) {
        esp_ldo_channel_handle_t handle = NULL;
        const esp_ldo_channel_config_t cfg = {
            .chan_id = channels[i].chan_id,
            .voltage_mv = channels[i].voltage_mv,
        };
        ESP_ERROR_CHECK(esp_ldo_acquire_channel(&cfg, &handle));
        ESP_LOGI(TAG, "LDO %s enabled %dmV (chan_id=%d)", channels[i].name, channels[i].voltage_mv,
                 channels[i].chan_id);
    }
}

static void init_touch_i2c_bus(void)
{
    ESP_LOGI(TAG, "I2C for FT6336U (SCL=IO22, SDA=IO21)");
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = EXAMPLE_I2C_PORT,
        .sda_io_num = EXAMPLE_PIN_NUM_I2C_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_i2c_bus));
    vTaskDelay(pdMS_TO_TICKS(10));
}

/* FT6336 复位释放后需要较长时间才出有效 ID；驱动内部仅延时 10ms 不够 */
static void touch_hw_reset_release(void)
{
#if EXAMPLE_PIN_NUM_TOUCH_RST >= 0
    const gpio_config_t rst_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_TOUCH_RST,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_TOUCH_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_TOUCH_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(200));
#endif
}

IRAM_ATTR static bool rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    return lvgl_port_notify_rgb_vsync();
}

static const st7701_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0B, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x37, 0x02}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xB0, (uint8_t[]){0x00, 0x0F, 0x16, 0x0E, 0x11, 0x07, 0x09, 0x09, 0x08, 0x23, 0x05, 0x11, 0x0F, 0x28, 0x2D, 0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x0F, 0x16, 0x0E, 0x11, 0x07, 0x09, 0x08, 0x09, 0x23, 0x05, 0x11, 0x0F, 0x28, 0x2D, 0x18}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x4D}, 1, 0},
    {0xB1, (uint8_t[]){0x33}, 1, 0},
    {0xB2, (uint8_t[]){0x87}, 1, 0},
    {0xB5, (uint8_t[]){0x4B}, 1, 0},
    {0xB7, (uint8_t[]){0x8C}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x02, 0xF0, 0x00, 0x00, 0x03, 0xF0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t[]){0x10, 0x10, 0x40, 0x40, 0xF2, 0xF0, 0x00, 0x00, 0xF2, 0xF0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x07, 0xEF, 0xF0, 0xF0, 0x09, 0xF1, 0xF0, 0xF0, 0x03, 0xF3, 0xF0, 0xF0, 0x05, 0xED, 0xF0, 0xF0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x08, 0xF0, 0xF0, 0xF0, 0x0A, 0xF2, 0xF0, 0xF0, 0x04, 0xF4, 0xF0, 0xF0, 0x06, 0xEE, 0xF0, 0xF0}, 16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0xE4, 0xE4, 0x44, 0x88, 0x40}, 7, 0},
    {0xEC, (uint8_t[]){0x78, 0x00}, 2, 0},
    {0xED, (uint8_t[]){0x20, 0xF9, 0x87, 0x76, 0x65, 0x54, 0x4F, 0xFF, 0xFF, 0xF4, 0x45, 0x56, 0x67, 0x78, 0x9F, 0x02}, 16, 0},
    {0xEF, (uint8_t[]){0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},
    /* COLMOD=0x66：覆盖驱动按 bpp 下发的值，适配本板 DB1-11+13-17 跳线映射（此前加此项后亮度恢复） */
    {0x3A, (uint8_t[]){0x66}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};
static void hold_unused_rgb_db_pins(void)
{
    static bool configured = false;
    if (!configured) {
        const gpio_config_t cfg = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << EXAMPLE_LCD_IO_RGB_DATA0) | (1ULL << EXAMPLE_LCD_IO_RGB_DATA12),
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        configured = true;
    }
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_LCD_IO_RGB_DATA0, 0));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_LCD_IO_RGB_DATA12, 0));
    ESP_LOGI(TAG, "Hold unused RGB DB pins low (DB0=IO%d, DB12=IO%d)",
             EXAMPLE_LCD_IO_RGB_DATA0, EXAMPLE_LCD_IO_RGB_DATA12);
}

void app_init(void)
{
    if (EXAMPLE_PIN_NUM_BK_LIGHT >= 0) {
        const gpio_config_t bk_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT,
        };
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
        ESP_ERROR_CHECK(gpio_set_drive_capability(EXAMPLE_PIN_NUM_BK_LIGHT, GPIO_DRIVE_CAP_3));
        app_lcd_bk_light_set(false);
    }

    ESP_LOGI(TAG, "Install 3-wire SPI panel IO (MOSI=IO25, SCLK=IO26, CS hardwired on adapter)");
    spi_line_config_t line_config = {
        .cs_io_type = IO_TYPE_GPIO,
        .cs_gpio_num = EXAMPLE_LCD_IO_SPI_CS_STUB,
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = EXAMPLE_LCD_IO_SPI_SCL,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = EXAMPLE_LCD_IO_SPI_SDA,
        .io_expander = NULL,
    };
    esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_config, &io_handle));

    hold_unused_rgb_db_pins();
    app_enable_all_ldo_channels();

    ESP_LOGI(TAG, "Install ST7701 panel driver");
    esp_lcd_panel_handle_t lcd_handle = NULL;
    esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dma_burst_size = 64,
        .data_width = EXAMPLE_RGB_DATA_WIDTH,
        .bits_per_pixel = EXAMPLE_RGB_BIT_PER_PIXEL,
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE,
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK,
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC,
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC,
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP,
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA1,   /* sw bit0  -> DB1  */
            EXAMPLE_LCD_IO_RGB_DATA2,   /* sw bit1  -> DB2  */
            EXAMPLE_LCD_IO_RGB_DATA3,   /* sw bit2  -> DB3  */
            EXAMPLE_LCD_IO_RGB_DATA4,   /* sw bit3  -> DB4  */
            EXAMPLE_LCD_IO_RGB_DATA5,   /* sw bit4  -> DB5  */
            EXAMPLE_LCD_IO_RGB_DATA6,
            EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8,
            EXAMPLE_LCD_IO_RGB_DATA9,
            EXAMPLE_LCD_IO_RGB_DATA10,  /* sw bit10 -> DB10 */
            EXAMPLE_LCD_IO_RGB_DATA11,  /* sw bit11 -> DB11 */
            EXAMPLE_LCD_IO_RGB_DATA13,  /* sw bit12 -> DB13 */
            EXAMPLE_LCD_IO_RGB_DATA14,  /* sw bit13 -> DB14 */
            EXAMPLE_LCD_IO_RGB_DATA15,  /* sw bit14 -> DB15 */
            EXAMPLE_LCD_IO_RGB_DATA16,  /* sw bit15 -> DB16 */
            EXAMPLE_LCD_IO_RGB_DATA17,  /* extra    -> DB17 */
        },
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PCLK_HZ,
            .h_res = 480,
            .v_res = 480,
            .hsync_pulse_width = 10,
            .hsync_back_porch = 10,
            .hsync_front_porch = 20,
            .vsync_pulse_width = 10,
            .vsync_back_porch = 10,
            .vsync_front_porch = 10,
            .flags.pclk_active_neg = false,
        },
        .flags.fb_in_psram = true,
        .num_fbs = LVGL_PORT_LCD_RGB_BUFFER_NUMS,
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE,
    };
    rgb_config.timings.h_res = EXAMPLE_LCD_H_RES;
    rgb_config.timings.v_res = EXAMPLE_LCD_V_RES;
    st7701_vendor_config_t vendor_config = {
        .rgb_config = &rgb_config,
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .auto_del_panel_io = 0,
            .mirror_by_cmd = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_LCD_IO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = EXAMPLE_LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_LOGI(TAG, "RGB cfg: pclk=%dMHz bounce_lines=%d colmod_bpp=%d",
             EXAMPLE_LCD_PCLK_HZ / 1000000, PROJECT_LCD_RGB_BOUNCE_BUFFER_HEIGHT, EXAMPLE_LCD_BIT_PER_PIXEL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(io_handle, &panel_config, &lcd_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_handle));
    esp_lcd_panel_disp_on_off(lcd_handle, true);

    ESP_LOGI(TAG, "Turn on LCD backlight (IO32=%d)", EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
    app_lcd_bk_light_set(true);

    init_touch_i2c_bus();
    touch_hw_reset_release();

    esp_lcd_touch_handle_t tp_handle = NULL;
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_FT6x36_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {
            .disable_control_phase = 1,
        },
        /* 飞线 I2C 用 100kHz 更稳；400kHz 偶发读点全 0 */
        .scl_speed_hz = 100 * 1000,
    };

    ESP_LOGI(TAG, "Initialize I2C panel IO (FT6336U)");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(s_i2c_bus, &tp_io_config, &tp_io_handle));

    ESP_LOGI(TAG, "Initialize FT6336U touch controller");
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        /* 复位已在 touch_hw_reset_release() 完成，避免驱动内再短复位 */
        .rst_gpio_num = -1,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_err_t err = esp_lcd_touch_new_i2c_ft6x36(tp_io_handle, &tp_cfg, &tp_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FT6336U touch unavailable (%s), continue without touch", esp_err_to_name(err));
        tp_handle = NULL;
    }

    esp_lcd_rgb_panel_event_callbacks_t cbs = {
#if EXAMPLE_RGB_BOUNCE_BUFFER_SIZE > 0
        .on_frame_buf_complete = rgb_lcd_on_vsync_event,
#else
        .on_vsync = rgb_lcd_on_vsync_event,
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(lcd_handle, &cbs, NULL));
    ESP_ERROR_CHECK(lvgl_port_init(lcd_handle, tp_handle));
}
