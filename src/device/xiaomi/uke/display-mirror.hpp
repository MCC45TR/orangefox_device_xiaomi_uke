// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace uke_display {
struct Frame {
    const uint8_t* pixels;
    int width, height, stride, bytes_per_pixel;
    unsigned rotation;
};
struct Viewport { int x, y, width, height; };
// Native framebuffer coordinates are rotated into the same logical canvas as
// the GUI. The external monitor has no independent input coordinate system.
bool fit(int width, int height, int output_width, int output_height, Viewport& result, int scale_percent = 100);
bool render(const Frame& source, uint8_t* destination, std::size_t capacity,
            int width, int height, int stride, int scale_percent = 100);
int pointer_axis(int position, int delta, float speed, int extent);
bool parse_selection(const std::string& resolution, const std::string& refresh,
                     int& width, int& height, int& millihertz);
}

// Only the render thread owns DRM resources. Action threads change one atomic
// preference; no fd, framebuffer or CRTC is destroyed on an action thread.
void ure_mirror_attach(int fd, uint32_t primary_connector, uint32_t primary_crtc,
                       const uint32_t* primary_planes, std::size_t count);
void ure_mirror_detach();
void ure_mirror_enable(bool enabled);
// Zero dimensions/refresh request automatic selection. Refresh is millihertz
// (59940 preserves 59.94 Hz); exact EDID timings are always used.
bool ure_mirror_select(int width, int height, int millihertz, int scale_percent = 100);
std::string ure_mirror_modes();
std::string ure_mirror_mode();
void ure_mirror_blank(bool blank);
bool ure_mirror_update(const uke_display::Frame& frame, bool changed = true);
const char* ure_mirror_status();
