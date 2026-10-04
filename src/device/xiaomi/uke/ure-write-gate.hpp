// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "recovery_write_policy.hpp"
#include "gui/gui.hpp"
#include <cstdio>
#include <string>

inline bool ure_legacy_write_guard(ure::LegacyWrite operation) {
    const auto decision = ure::legacy_write_decision(operation);
    if (decision.allowed) return true;
    // No supplied path, unit identity, credential or arbitrary command is logged.
    std::fprintf(stderr, "URE_STORAGE_WRITE_BLOCKED operation=%s code=%s\n",
        ure::legacy_write_name(operation), decision.code);
    const std::string resource = std::string("ure_device_write_blocked=") + decision.message;
    gui_err(resource.c_str());
    return false;
}
