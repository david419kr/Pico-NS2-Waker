#pragma once

#ifndef DS5ONSWITCH_ENABLE_BT_DEBUG_TOOLS
#define DS5ONSWITCH_ENABLE_BT_DEBUG_TOOLS 0
#endif

namespace ds5onswitch {

inline constexpr bool kEnableBtDebugTools =
    DS5ONSWITCH_ENABLE_BT_DEBUG_TOOLS != 0;

} // namespace ds5onswitch
