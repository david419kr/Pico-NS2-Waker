#pragma once

#include "switch_bridge/ble_controller_hub.h"

#include "btstack_hid.h"
#include "hci.h"

#include <cstddef>
#include <cstdint>

namespace switchbridge {

BleControllerKind ble_controller_hub_active_kind();
bool ble_controller_hub_state_is_connected();
bool ble_controller_hub_can_use_active_steam_link();
hci_con_handle_t ble_controller_hub_connection_handle();
bool ble_controller_hub_enqueue_hid_report(const uint8_t *report,
                                           std::size_t report_len,
                                           hid_report_type_t report_type);
void ble_controller_hub_try_tx();
void ble_controller_hub_clear_tx_queue();
uint8_t ble_controller_hub_tx_count();
bool ble_controller_hub_write_ready();

} // namespace switchbridge
