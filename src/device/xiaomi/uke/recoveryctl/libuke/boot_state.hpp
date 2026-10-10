// SPDX-License-Identifier: Apache-2.0
// Missing optional boot properties require matching kernel orange evidence.
#pragma once
#include <string_view>

namespace ure {
class Root;
bool bootloader_unlocked(const Root& system);
inline bool measured_uke_unlocked(std::string_view device, std::string_view state,
        std::string_view locked, std::string_view verified, std::string_view bootconfig) {
    if (device != "uke" || (!state.empty() && state != "unlocked") ||
        (!locked.empty() && locked != "0") || (!verified.empty() && verified != "orange") ||
        bootconfig.size() > 256 * 1024 || bootconfig.find('\0') != bootconfig.npos) return false;
    auto trim = [](std::string_view value) {
        const auto first = value.find_first_not_of(" \t\r");
        if (first == value.npos) return std::string_view{};
        return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
    };
    const std::string_view keys[] = {"androidboot.verifiedbootstate", "androidboot.vbmeta.device_state", "androidboot.flash.locked"};
    const std::string_view values[] = {"\"orange\"", "\"unlocked\"", "\"0\""};
    bool seen[] = {false, false, false};
    while (!bootconfig.empty()) {
        const auto end = bootconfig.find('\n');
        const auto line = trim(bootconfig.substr(0, end));
        if (line.size() > 4096) return false;
        const auto equal = line.find('=');
        const auto key = trim(line.substr(0, equal));
        for (int i = 0; i < 3; ++i) if (key == keys[i]) {
            if (seen[i] || equal == line.npos || trim(line.substr(equal + 1)) != values[i]) return false;
            seen[i] = true;
        }
        if (end == bootconfig.npos) break;
        bootconfig.remove_prefix(end + 1);
    }
    if (state == "unlocked" && locked == "0") return true;
    return verified == "orange" && seen[0];
}
}
