#pragma once

#ifndef SWITCHBRIDGE_ENABLE_BT_DEBUG_TOOLS
#define SWITCHBRIDGE_ENABLE_BT_DEBUG_TOOLS 0
#endif

namespace switchbridge {

inline constexpr bool kEnableBtDebugTools =
    SWITCHBRIDGE_ENABLE_BT_DEBUG_TOOLS != 0;

} // namespace switchbridge
