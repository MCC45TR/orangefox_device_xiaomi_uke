// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lifecycle_policy.hpp"
#include "gui/gui.hpp"
#include <cstdio>

inline bool ure_legacy_lifecycle_guard(const ure::LegacyLifecycleGuard& token) {
    if(token.active())return true;
    std::fprintf(stderr,"URE_LIFECYCLE_BLOCKED active-or-unresolved-operation\n");
    gui_err("ure_lifecycle_blocked=An active or interrupted operation must finish or be recovered before unmounting or restarting.");
    return false;
}
