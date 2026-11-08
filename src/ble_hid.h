/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLE_HID_H
#define BLE_HID_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLE_HID_VOLUME_UP,
    BLE_HID_VOLUME_DOWN,
    BLE_HID_MUTE,
    BLE_HID_PREVIOUS,
    BLE_HID_PLAY_PAUSE,
    BLE_HID_NEXT
} ble_hid_command_t;

void ble_hid_init(void);
int ble_hid_send(ble_hid_command_t command);
bool ble_hid_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_HID_H */
