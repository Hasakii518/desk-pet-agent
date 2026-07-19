/* wifi_prov.h — WiFi 配网（STA 连接，经 BLE 配网 Service 下发凭据）
 *
 * 状态机：idle → connecting → ok（拿到 IP）/ fail（断开重试 3 次仍失败）。
 * 连接成功后凭据写入 NVS，下次上电自动重连（WiFi 本轮仅连通性验证用）。
 * 状态变化时调 bt_stack_prov_status_request() 请求 BLE 上报。
 */
#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 NVS + netif + event loop + WiFi STA；NVS 有凭据则自动连接。 */
void wifi_prov_init(void);

/* BLE 配网特征写入回调（bt_stack GATT access 调用）。 */
void wifi_prov_set_ssid(const char *ssid);
void wifi_prov_set_password(const char *pass);
void wifi_prov_commit(void);

/* 状态查询（UI / BLE Status 特征用）。 */
bool wifi_prov_connected(void);
const char *wifi_prov_state_str(void);   /* "idle" | "connecting" | "ok" | "fail" */
const char *wifi_prov_ssid(void);        /* 当前/目标 SSID，无则 "--" */
int  wifi_prov_rssi(void);               /* 已连接时的信号 dBm，否则 0 */
void wifi_prov_ip_str(char *buf, size_t len);
int  wifi_prov_last_err(void);           /* fail 时的 disconnect reason */

/* 序列化当前状态为 JSON：{"s":"ok","ip":"…","rssi":-55} 或 {"s":"fail","err":8} */
void wifi_prov_status_json(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_PROV_H */
