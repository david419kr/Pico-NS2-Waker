#include "switch_bridge/bt_debug_log.h"

#include "switch_bridge/debug_flags.h"

#include "hardware/flash.h"
#include "pico/btstack_flash_bank.h"
#include "pico/flash.h"
#include "pico/platform.h"
#include "pico/time.h"

#include <cstring>

extern "C" char __flash_binary_end;

namespace switchbridge {
namespace {

constexpr uint32_t kLogMagic = 0x354c4447u; // "GDL5"
constexpr uint32_t kLogFlashOffset =
    PICO_FLASH_BANK_STORAGE_OFFSET - FLASH_SECTOR_SIZE;
constexpr std::size_t kLogSize = FLASH_SECTOR_SIZE;
constexpr std::size_t kEntrySize = 16;
constexpr std::size_t kEntryCount = kLogSize / kEntrySize;

struct Entry {
    uint32_t magic;
    uint32_t ms;
    uint8_t event;
    uint8_t kind;
    uint16_t value16;
    uint32_t value32;
};
static_assert(sizeof(Entry) == kEntrySize);
static_assert(kLogFlashOffset + kLogSize <= PICO_FLASH_BANK_STORAGE_OFFSET);

struct FlashOp {
    bool erase;
    uint32_t offset;
    const uint8_t *page;
};

bool g_enabled = false;
std::size_t g_next_entry = 0;
uint8_t g_page_cache[FLASH_PAGE_SIZE];

const uint8_t *flash_log_base()
{
    return reinterpret_cast<const uint8_t *>(XIP_BASE + kLogFlashOffset);
}

bool erased_entry(const Entry &entry)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&entry);
    for (std::size_t i = 0; i < sizeof(entry); ++i) {
        if (bytes[i] != 0xffu) {
            return false;
        }
    }
    return true;
}

void __no_inline_not_in_flash_func(run_flash_op)(void *param)
{
    const auto *op = static_cast<const FlashOp *>(param);
    if (op->erase) {
        flash_range_erase(op->offset, FLASH_SECTOR_SIZE);
    } else {
        flash_range_program(op->offset, op->page, FLASH_PAGE_SIZE);
    }
}

void erase_log_sector()
{
    FlashOp op{true, kLogFlashOffset, nullptr};
    (void)flash_safe_execute(run_flash_op, &op, UINT32_MAX);
    g_next_entry = 0;
}

void load_page_cache(std::size_t page_offset)
{
    std::memcpy(g_page_cache, flash_log_base() + page_offset,
                sizeof(g_page_cache));
}

void program_page(std::size_t page_offset)
{
    FlashOp op{false, kLogFlashOffset + static_cast<uint32_t>(page_offset),
               g_page_cache};
    (void)flash_safe_execute(run_flash_op, &op, UINT32_MAX);
}

bool log_region_safe()
{
    const auto binary_end =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&__flash_binary_end) -
                              XIP_BASE);
    return binary_end <= kLogFlashOffset;
}

void scan_log()
{
    const auto *entries = reinterpret_cast<const Entry *>(flash_log_base());
    for (std::size_t i = 0; i < kEntryCount; ++i) {
        if (erased_entry(entries[i])) {
            g_next_entry = i;
            return;
        }
        if (entries[i].magic != kLogMagic) {
            erase_log_sector();
            return;
        }
    }
    g_next_entry = kEntryCount;
}

} // namespace

void bt_debug_log_init()
{
    if (!kEnableBtDebugTools) {
        return;
    }
    g_enabled = log_region_safe();
    if (!g_enabled) {
        return;
    }
    scan_log();
    bt_debug_log_event(BtDebugEvent::Boot);
}

void bt_debug_log_event(BtDebugEvent event, uint8_t kind, uint16_t value16,
                        uint32_t value32)
{
    if (!kEnableBtDebugTools || !g_enabled) {
        return;
    }
    if (g_next_entry >= kEntryCount) {
        erase_log_sector();
    }

    const std::size_t entry_offset = g_next_entry * kEntrySize;
    const std::size_t page_offset =
        (entry_offset / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
    load_page_cache(page_offset);

    Entry entry{
        kLogMagic,
        to_ms_since_boot(get_absolute_time()),
        static_cast<uint8_t>(event),
        kind,
        value16,
        value32,
    };
    std::memcpy(g_page_cache + (entry_offset - page_offset), &entry,
                sizeof(entry));
    program_page(page_offset);
    ++g_next_entry;
}

std::size_t bt_debug_log_read(uint32_t offset, uint8_t *out, std::size_t len)
{
    if (!kEnableBtDebugTools || out == nullptr || offset >= kLogSize) {
        return 0;
    }
    const std::size_t available = kLogSize - offset;
    const std::size_t copy_len = len < available ? len : available;
    std::memcpy(out, flash_log_base() + offset, copy_len);
    return copy_len;
}

void bt_debug_log_clear()
{
    if (!kEnableBtDebugTools || !log_region_safe()) {
        return;
    }
    g_enabled = true;
    erase_log_sector();
    bt_debug_log_event(BtDebugEvent::Boot);
}

uint32_t bt_debug_log_flash_offset()
{
    return kLogFlashOffset;
}

uint32_t bt_debug_log_size()
{
    return static_cast<uint32_t>(kLogSize);
}

} // namespace switchbridge
