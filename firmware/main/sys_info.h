/* sys_info.h — 系统信息查询（芯片型号、内存、电量等实时数据）*/
#ifndef SYS_INFO_H
#define SYS_INFO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** ESP32 芯片型号名称（如 "ESP32-S3"）*/
const char *sys_info_chip_model(void);

/** 芯片版本（如 "v0.2"）*/
const char *sys_info_chip_rev(void);

/** PSRAM 总量，字节 */
uint32_t sys_info_psram_total(void);

/** PSRAM 空闲量，字节 */
uint32_t sys_info_psram_free(void);

/** 内部 DRAM 空闲量，字节 */
uint32_t sys_info_dram_free(void);

/** 格式化的内存信息串（如 "8.0M / 4.5M free"）*/
const char *sys_info_psram_str(void);

/** 电池电量百分比，-1 表示未检测到电池 */
int8_t sys_info_battery_pct(void);

/** 是否正在充电 */
bool sys_info_battery_charging(void);

/** WiFi SSID，未连接则返回 "--" */
const char *sys_info_wifi_ssid(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_INFO_H */
