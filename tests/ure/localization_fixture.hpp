// SPDX-License-Identifier: Apache-2.0
#pragma once
// Let the owner's separate localization draft coexist with platform fixtures.
// The actual draft helper is compiled when present, with English platform
// lookups. This fixture is not translation or script-shaping acceptance.
#if __has_include("../../src/device/xiaomi/uke/ure-localization.hpp")
#include "../../src/device/xiaomi/uke/ure-localization.hpp"
inline std::string gui_lookup(const std::string&,const std::string& fallback) { return fallback; }
#endif
