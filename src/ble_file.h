/* REQ-203：板端 BLE 文件传输服务（自建 128-bit GATT）。
 *
 * 协议见 docs/11-REQ-203-BLE文件传输-设计与协议.md，
 * 板端实施方案见 docs/12-REQ-203-板端实施方案.md。
 */
#ifndef BLE_FILE_H
#define BLE_FILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 cmd/data 两个特征；busy 用于录音或回放进行中时回 X 9。 */
void ble_file_init(bool (*busy)(void));

/* 当前是否有主机连着（工作线程据此在中途断开时退出）。 */
bool ble_file_connected(void);

/* 是否正在传文件（UI 侧录音入口可据此拒绝）。 */
bool ble_file_transfer_active(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_FILE_H */

