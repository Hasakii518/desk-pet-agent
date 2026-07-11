/* bsp.c — 显示 + 触摸 + LVGL 初始化
 *
 * 面板：SH8601 QSPI，466x466，RGB565，bits_per_pixel=16。
 * 触摸：FT5x06（I2C）。移植层：esp_lvgl_port。
 * 引脚参照 Waveshare ESP32-S3-Touch-AMOLED-1.43C 的 config.h。
 */
#include "bsp.h"
#include "ui_common.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"

static const char *TAG = "bsp";

/* -------- 引脚（Waveshare 1.43C）-------- */
#define PIN_LCD_D0    9
#define PIN_LCD_D1    10
#define PIN_LCD_D2    11
#define PIN_LCD_D3    12
#define PIN_LCD_SCK   14
#define PIN_LCD_CS    15
#define PIN_LCD_RST   13
#define PIN_LCD_TE    8

#define PIN_TP_RST    16
#define PIN_TP_INT    17

#define PIN_I2C_SDA   47
#define PIN_I2C_SCL   48

#define LCD_H_RES     466
#define LCD_V_RES     466
#define LCD_BITS      16
#define LCD_PCLK_HZ   (40 * 1000 * 1000)
#define LCD_SPI_HOST  SPI2_HOST
#define TP_I2C_PORT   I2C_NUM_0

/* SH8601 初始化命令（同参考板）*/
static const sh8601_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x53, (uint8_t []){0x20}, 1, 0},
    {0x51, (uint8_t []){0xFF}, 1, 0},
    {0x36, (uint8_t []){0xC0}, 1, 0},
    {0x63, (uint8_t []){0xFF}, 1, 0},
    {0x2A, (uint8_t []){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, (uint8_t []){0x00}, 0, 100},
    {0x29, (uint8_t []){0x00}, 0, 0},
};

static i2c_master_bus_handle_t s_i2c_bus;

static void init_i2c(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = TP_I2C_PORT,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_i2c_bus));
}

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t    s_panel;

static void init_lcd(void)
{
    ESP_LOGI(TAG, "Init QSPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCK,
        .data0_io_num = PIN_LCD_D0,
        .data1_io_num = PIN_LCD_D1,
        .data2_io_num = PIN_LCD_D2,
        .data3_io_num = PIN_LCD_D3,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Init panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 8,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = { .quad_mode = true },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &s_io));

    ESP_LOGI(TAG, "Init SH8601 panel");
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(s_io, &panel_config, &s_panel));
    esp_lcd_panel_set_gap(s_panel, 0x08, 0x00);
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
}

static esp_lcd_touch_handle_t s_tp;

static void init_touch(void)
{
    ESP_LOGI(TAG, "Init CST816/CST820 touch");
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 0,
        .flags = { .disable_control_phase = 1 },
        .scl_speed_hz = 400 * 1000,
    };

    esp_err_t err = esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch panel_io init failed (%s), continue without touch", esp_err_to_name(err));
        return;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = PIN_TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        /* 本板实测：显示 180° 旋转后，触摸只需镜像 Y（上下），
         * X（左右）保持不镜像，否则左右滑方向相反。 */
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 1 },
    };
    /* 触摸初始化失败不致命：显示仍要能亮，仅告警并降级为无触摸 */
    err = esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_tp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CST816/CST820 init failed (%s), continue without touch", esp_err_to_name(err));
        s_tp = NULL;
    }
}

static lv_display_t *s_disp;
static lv_indev_t   *s_touch_indev;

/* SH8601 要求每次刷新区域按 2 像素对齐（起点取偶、终点取奇），
 * 否则局部重绘（翻页 / 滚动）会撕裂、错位。见参考板 my_draw_event_cb。 */
static void align_area_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

/* 直接给面板发 MADCTL(0x36) 设置扫描方向 / 镜像。
 * lvgl_port_add_disp 会把 MADCTL 复位为 0，需在其后重新下发 0xC0（旋转 180°），
 * 否则文字 / 图像左右上下反向。 */
static void panel_set_madctl(uint8_t val)
{
    uint32_t cmd = (0x02u << 24) | (0x36u << 8);  /* QSPI 命令帧：0x02 前缀 + 0x36 */
    esp_lcd_panel_io_tx_param(s_io, cmd, &val, 1);
}

static void init_lvgl(void)
{
    ESP_LOGI(TAG, "Init LVGL port");
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_io,
        .panel_handle = s_panel,
        .buffer_size = LCD_H_RES * 40,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);

    /* 2 像素对齐刷新区，消除翻页撕裂 */
    lv_display_add_event_cb(s_disp, align_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    /* 复位镜像方向为 0xC0（旋转 180°），修正反向显示 */
    panel_set_madctl(0xC0);

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = s_tp,
    };
    if (s_tp)
        s_touch_indev = lvgl_port_add_touch(&touch_cfg);
    else
        ESP_LOGW(TAG, "no touch controller; gestures disabled");
}

void bsp_init(void)
{
    init_i2c();
    init_lcd();
    init_touch();
    init_lvgl();
    ESP_LOGI(TAG, "BSP ready");
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }

lv_indev_t *bsp_touch_indev(void)  { return s_touch_indev; }
