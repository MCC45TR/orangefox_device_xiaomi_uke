// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "touch/touch-gui-session.hpp"
inline void ure_touch_graphics_ready(bool ok, bool scanned) noexcept {
    uke::touch::graphics_ready(ok, scanned);
}
inline void ure_touch_resources_ready(bool loaded) noexcept { uke::touch::resources_ready(loaded); }
inline void ure_touch_start_once_after_frame(bool awake) noexcept {
    uke::touch::start_once_after_frame(awake);
}
inline void ure_touch_poll_status() noexcept { uke::touch::poll_status(); }
inline uke::touch::Status ure_touch_collected_status() noexcept {
    return uke::touch::collected_status();
}
inline void ure_touch_stop() noexcept { uke::touch::stop(); }
