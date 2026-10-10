// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string_view>

namespace ure {
// Role names are hints only; old GPT names remain readable without renaming them.
constexpr std::string_view os_partition_role(std::string_view label) noexcept {
    if (label.starts_with("uke_")) label.remove_prefix(4);
    if (label == "esp" || label == "linux_boot" || label == "linux" ||
        label == "windows" || label == "home") return label;
    return {};
}
} // namespace ure
