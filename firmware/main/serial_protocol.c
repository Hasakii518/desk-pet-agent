/* serial_protocol.c — USB-CDC 串口物理层
 *
 * 行重组 / 帧解析 / 断连检测 / 上行扇出已抽到 frame_parse.c（串口/BLE 共用）。
 * 本文件只剩：
 *   - lv_timer 轮询 USB Serial/JTAG FIFO，字节喂给 frame_parse
 *   - 注册串口上行槽（frame_parse 扇出时写到 USB-CDC）
 * 对外 API 保持不变（serial_protocol_send_cmd* 转发到 frame_parse）。
 */
#include "serial_protocol.h"
#include "frame_parse.h"

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

#include <string.h>
#include <time.h>

static const char *TAG = "serial";

#define POLL_INTERVAL_MS  10   /* 10ms→100Hz，避免 USB FIFO(64B) 溢出 */

/* ---- 上行槽：写一行 JSON 到 USB-CDC（补 \n）---- */
static void serial_uplink(const char *line)
{
    size_t len = strlen(line);
    usb_serial_jtag_write_bytes((const uint8_t *)line, len,
                                pdMS_TO_TICKS(100));
    usb_serial_jtag_write_bytes((const uint8_t *)"\n", 1,
                                pdMS_TO_TICKS(100));
}

/* ---- 下行轮询 ---- */
static void serial_poll_cb(lv_timer_t *t)
{
    (void)t;
    uint8_t buf[512];
    int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0);
    if (n > 0) {
        frame_parse_feed_bytes(buf, n);
    }
}

void serial_protocol_init(void)
{
    /* 默认时区：设备无 RTC，会话时间用 bridge 下发的墙钟 ms 经 localtime_r
     * 格式化；不设 TZ 则显示 UTC。这里设为 CST-8（本项目默认地域）。*/
    setenv("TZ", "CST-8", 1);
    tzset();

    /* 安装 USB Serial/JTAG 驱动，否则 usb_serial_jtag_read_bytes() 会访问
     * 未初始化的驱动上下文（NULL ring buffer）导致 LoadProhibited panic。
     * ESP_ERR_INVALID_STATE 表示已被控制台安装——可复用，read_bytes 照常工作。*/
    usb_serial_jtag_driver_config_t usj_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&usj_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s — serial disabled",
                 esp_err_to_name(err));
        return;
    }

    frame_parse_init();
    frame_parse_register_uplink(serial_uplink);
    lv_timer_create(serial_poll_cb, POLL_INTERVAL_MS, NULL);

    ESP_LOGI(TAG, "Serial transport initialized (poll %dms)", POLL_INTERVAL_MS);
}

/* ---- 上行指令（转发到 frame_parse 扇出：串口 + BLE）---- */

void serial_protocol_send_cmd(const char *cmd_name)
{
    frame_parse_send_cmd(cmd_name);
}

void serial_protocol_send_cmd_kv(const char *cmd_name,
                                 const char *key, const char *value)
{
    frame_parse_send_cmd_kv(cmd_name, key, value);
}

void serial_protocol_send_cmd_bool(const char *cmd_name,
                                   const char *key, bool value)
{
    frame_parse_send_cmd_bool(cmd_name, key, value);
}
