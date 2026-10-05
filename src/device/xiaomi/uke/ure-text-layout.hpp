// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <hb.h>

namespace ure_text {
constexpr std::size_t max_characters = 8192;
constexpr std::size_t max_glyphs = 32768;
constexpr std::size_t memory_budget = 16 * 1024 * 1024;
constexpr std::int64_t max_width_26 = 1024 * 1024 * 64LL;

struct FontAccess {
    void* context;
    // The renderer owns font lifetime and its FreeType lock. A null font is a
    // visible refusal; a missing scalar may use the primary .notdef glyph.
    hb_font_t* (*select)(void*, std::uint32_t, hb_script_t, unsigned int*, unsigned int);
    int (*decode)(const char*, const char*, unsigned int*);
};
struct Glyph {
    std::uint32_t index, cluster;
    unsigned int slot;
    std::int64_t x_26, y_26;
    std::int32_t advance_26;
};
struct Cluster {
    std::uint32_t begin, end;
    std::int64_t advance_26;
};
struct Layout {
    bool valid = false;
    bool truncated = false;
    std::size_t bytes = 0;
    std::int64_t width_26 = 0;
    std::vector<Glyph> glyphs;
    std::vector<Cluster> clusters;
};
Layout shape(const char* text, std::size_t bytes, FontAccess fonts, hb_language_t language) noexcept;
// Returns a logical UTF-8 prefix ending at a shaped grapheme cluster. Re-shape
// that prefix before drawing; contextual forms can change its final width.
std::size_t prefix(const Layout& layout, int width) noexcept;
std::size_t allocated_bytes() noexcept;
std::size_t refused_allocations() noexcept;
}
