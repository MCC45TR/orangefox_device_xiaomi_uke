// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "touch-policy.hpp"
namespace uke::touch {
// Hooks must be called on the GUI thread. The holder survives all page/theme
// transitions. Status is collected here; OEM workers never touch DataManager.
void graphics_ready(bool graphics_ok, bool input_scan_done) noexcept;
void resources_ready(bool loaded) noexcept;
void start_once_after_frame(bool panel_awake) noexcept;
void poll_status() noexcept;
Status collected_status() noexcept;
bool take_initial_panel_sync(bool panel_awake) noexcept;
void stop() noexcept;
} // namespace uke::touch
