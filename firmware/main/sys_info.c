/* sys_info.c — 实时系统信息查询（芯片、内存、电池 ADC、WiFi）*/
#include "sys_info.h"
#include "wifi_prov.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <string.h>

/* ---- 芯片 ---- */

const char *sys_info_chip_model(void)
{
    static char buf[32];
    esp_chip_info_t info;
    esp_chip_info(&info);
    switch (info.model) {
    case CHIP_ESP32S3:  snprintf(buf, sizeof(buf), "ESP32-S3");    break;
    case CHIP_ESP32S2:  snprintf(buf, sizeof(buf), "ESP32-S2");    break;
    case CHIP_ESP32C3:  snprintf(buf, sizeof(buf), "ESP32-C3");    break;
    case CHIP_ESP32C6:  snprintf(buf, sizeof(buf), "ESP32-C6");    break;
    case CHIP_ESP32:    snprintf(buf, sizeof(buf), "ESP32");       break;
    default:            snprintf(buf, sizeof(buf), "ESP32-?");     break;
    }
    return buf;
}

const char *sys_info_chip_rev(void)
{
    static char buf[8];
    esp_chip_info_t info;
    esp_chip_info(&info);
    snprintf(buf, sizeof(buf), "v%d.%d",
             (int)info.revision / 10, (int)info.revision % 10);
    return buf;
}

/* ---- 内存 ---- */

uint32_t sys_info_psram_total(void)
{
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

uint32_t sys_info_psram_free(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

uint32_t sys_info_dram_free(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

const char *sys_info_psram_str(void)
{
    static char buf[24];
    uint32_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    uint32_t free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (total > 0) {
        snprintf(buf, sizeof(buf), "%.1f MB free", (double)free / (1024 * 1024));
    } else {
        snprintf(buf, sizeof(buf), "%.1f KB free",
                 (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    }
    return buf;
}

/* ---- 电池 ADC（GPIO4 → ADC1_CH3，分压 2:1）---- */

static adc_oneshot_unit_handle_t s_adc1;
static adc_cali_handle_t         s_adc_cali;
static bool                      s_batt_init_done;

static void batt_init(void)
{
    if (s_batt_init_done) return;

    /* 校准 */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali);

    /* ADC1 */
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&unit_cfg, &s_adc1);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(s_adc1, ADC_CHANNEL_3, &chan_cfg);

    /* 充电状态引脚 GPIO7（ETA6098 STAT：L=充电中，H=未充电）*/
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = 1ULL << GPIO_NUM_7,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&gpio_cfg);

    s_batt_init_done = true;
}

int8_t sys_info_battery_pct(void)
{
    batt_init();
    int raw;
    if (adc_oneshot_read(s_adc1, ADC_CHANNEL_3, &raw) != ESP_OK)
        return -1;

    int mv;
    if (adc_cali_raw_to_voltage(s_adc_cali, raw, &mv) != ESP_OK)
        return -1;

    /* 分压 2:1 → 实际电压 = 采样 × 2 */
    int batt_mv = mv * 2;

    /* LiPo: 4.2V=100%, 3.0V=0% */
    if (batt_mv >= 4200) return 100;
    if (batt_mv <= 3000) return 0;
    return (int8_t)((batt_mv - 3000) * 100 / 1200);
}

bool sys_info_battery_charging(void)
{
    batt_init();
    /* GPIO7: L(0)=充电中 */
    return gpio_get_level(GPIO_NUM_7) == 0;
}

/* ---- WiFi ---- */

const char *sys_info_wifi_ssid(void)
{
    return wifi_prov_ssid();
}
