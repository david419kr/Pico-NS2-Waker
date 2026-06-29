#pragma once

#include <cstddef>
#include <cstdint>

namespace switchbridge {

enum class BtDebugEvent : uint8_t {
    Boot = 1,
    StackReady,
    InquiryResult,
    CreateConnection,
    ConnectionRequest,
    ConnectionComplete,
    AuthComplete,
    EncryptionChange,
    Disconnect,
    SdpComplete,
    ControllerKind,
    L2capOpen,
    L2capIncoming,
    L2capClosed,
    HidData,
};

void bt_debug_log_init();
void bt_debug_log_event(BtDebugEvent event, uint8_t kind = 0,
                        uint16_t value16 = 0, uint32_t value32 = 0);
std::size_t bt_debug_log_read(uint32_t offset, uint8_t *out, std::size_t len);
void bt_debug_log_clear();
uint32_t bt_debug_log_flash_offset();
uint32_t bt_debug_log_size();

} // namespace switchbridge
