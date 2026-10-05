// SPDX-License-Identifier: Apache-2.0
#include "ure-text-layout.hpp"
#include <fribidi.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <utility>

namespace {
std::atomic<std::size_t> layout_bytes{0}, refused_bytes{0};
constexpr std::size_t allocation_limit = 4 * 1024 * 1024;
struct alignas(std::max_align_t) Allocation { std::size_t bytes; };
bool reserve_bytes(std::size_t bytes) {
    auto used = layout_bytes.load(std::memory_order_relaxed);
    do {
        if (bytes > ure_text::memory_budget - used) return false;
    } while (!layout_bytes.compare_exchange_weak(used, used + bytes, std::memory_order_relaxed));
    return true;
}
}
extern "C" void* ure_layout_malloc(std::size_t bytes) {
    if (!bytes) bytes = 1;
    if (bytes > allocation_limit || !reserve_bytes(bytes)) {
        ++refused_bytes; return nullptr;
    }
    auto* block = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + bytes));
    if (!block) { layout_bytes.fetch_sub(bytes); ++refused_bytes; return nullptr; }
    block->bytes = bytes;
    return block + 1;
}
extern "C" void ure_layout_free(void* block) {
    if (!block) return;
    auto* allocation = static_cast<Allocation*>(block) - 1;
    layout_bytes.fetch_sub(allocation->bytes);
    std::free(allocation);
}
extern "C" void* hb_malloc_impl(std::size_t bytes) { return ure_layout_malloc(bytes); }
extern "C" void hb_free_impl(void* block) { ure_layout_free(block); }
extern "C" void* hb_calloc_impl(std::size_t count, std::size_t bytes) {
    if (bytes && count > std::numeric_limits<std::size_t>::max() / bytes) {
        ++refused_bytes; return nullptr;
    }
    void* block = ure_layout_malloc(count * bytes);
    if (block) std::memset(block, 0, count * bytes);
    return block;
}
extern "C" void* hb_realloc_impl(void* block, std::size_t bytes) {
    if (!bytes) { ure_layout_free(block); return nullptr; }
    void* replacement = ure_layout_malloc(bytes);
    if (!replacement) return nullptr; // The original block remains owned.
    if (block) {
        auto* allocation = static_cast<Allocation*>(block) - 1;
        std::memcpy(replacement, block, std::min(bytes, allocation->bytes));
        ure_layout_free(block);
    }
    return replacement;
}

namespace ure_text {
std::size_t allocated_bytes() noexcept { return layout_bytes.load(); }
std::size_t refused_allocations() noexcept { return refused_bytes.load(); }
namespace {
struct BufferDeleter { void operator()(hb_buffer_t* p) const { hb_buffer_destroy(p); } };
using Buffer = std::unique_ptr<hb_buffer_t, BufferDeleter>;
struct Scalar {
    FriBidiChar value;
    std::uint32_t byte;
    hb_script_t script;
    hb_font_t* font = nullptr;
    unsigned int slot = 0;
};
struct Run {
    std::size_t begin, end;
    FriBidiStrIndex visual;
};
bool inherited(hb_script_t script) {
    return script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED || script == HB_SCRIPT_UNKNOWN;
}
}
Layout shape(const char* text, std::size_t bytes, FontAccess fonts, hb_language_t language) noexcept {
    Layout result;
    if (!text || bytes > 65536 || !fonts.select || !fonts.decode) return result;
    if (!bytes) { result.valid = true; return result; }
    try {
        std::vector<Scalar> scalars;
        scalars.reserve(std::min(bytes, max_characters));
        auto* unicode = hb_unicode_funcs_get_default();
        const char* end = text + bytes;
        const char* input = text;
        while (input < end && *input && scalars.size() < max_characters) {
            unsigned int value = 0;
            const int consumed = fonts.decode(input, end, &value);
            if (consumed <= 0 || consumed > end - input) return result;
            scalars.push_back({value, static_cast<std::uint32_t>(input - text), hb_unicode_script(unicode, value)});
            input += consumed;
        }
        result.bytes = static_cast<std::size_t>(input - text);
        result.truncated = input < end;
        if (scalars.empty()) { result.valid = true; return result; }
        const auto n = static_cast<FriBidiStrIndex>(scalars.size());
        std::vector<FriBidiChar> codepoints(scalars.size());
        std::vector<FriBidiCharType> types(scalars.size());
        std::vector<FriBidiBracketType> brackets(scalars.size());
        std::vector<FriBidiLevel> levels(scalars.size());
        std::vector<FriBidiStrIndex> logical_to_visual(scalars.size());
        std::vector<FriBidiStrIndex> visual_to_logical(scalars.size());
        for (std::size_t i = 0; i < scalars.size(); ++i) codepoints[i] = scalars[i].value;
        FriBidiParType base = FRIBIDI_PAR_ON; // UAX #9 first-strong paragraph direction.
        // No Arabic presentation-form rewrite or visual UTF-8 mutation: only
        // bidi levels and ordering are requested. HarfBuzz shapes logical runs.
        const auto refusals = refused_allocations();
        fribidi_get_bidi_types(codepoints.data(), n, types.data());
        fribidi_get_bracket_types(codepoints.data(), n, types.data(), brackets.data());
        if (!fribidi_get_par_embedding_levels_ex(types.data(), brackets.data(), n, &base, levels.data()))
            return result;
        std::iota(visual_to_logical.begin(), visual_to_logical.end(), 0);
        if (!fribidi_reorder_line(FRIBIDI_FLAG_REORDER_NSM, types.data(), n, 0, base, levels.data(), nullptr,
            visual_to_logical.data()) || refusals != refused_allocations()) return result;
        for (std::size_t i = 0; i < scalars.size(); ++i) {
            if (visual_to_logical[i] < 0 || visual_to_logical[i] >= n) return result;
            logical_to_visual[visual_to_logical[i]] = static_cast<FriBidiStrIndex>(i);
        }
        hb_script_t previous_script = HB_SCRIPT_UNKNOWN;
        for (auto& scalar : scalars) {
            if (inherited(scalar.script)) scalar.script = previous_script;
            else previous_script = scalar.script;
        }
        hb_script_t next_script = HB_SCRIPT_LATIN;
        for (auto it = scalars.rbegin(); it != scalars.rend(); ++it) {
            if (inherited(it->script)) it->script = next_script;
            else next_script = it->script;
        }
        unsigned int previous_slot = 0;
        for (auto& scalar : scalars) {
            scalar.font = fonts.select(fonts.context, scalar.value, scalar.script, &scalar.slot, previous_slot);
            if (!scalar.font) return result;
            previous_slot = scalar.slot;
        }
        std::vector<Run> runs;
        runs.reserve(scalars.size());
        for (std::size_t begin = 0; begin < scalars.size();) {
            auto finish = begin + 1;
            auto visual = logical_to_visual[begin];
            while (finish < scalars.size() && levels[finish] == levels[begin] &&
                   scalars[finish].font == scalars[begin].font && scalars[finish].script == scalars[begin].script) {
                visual = std::min(visual, logical_to_visual[finish]);
                ++finish;
            }
            runs.push_back({begin, finish, visual});
            begin = finish;
        }
        std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) { return a.visual < b.visual; });
        std::int64_t x = 0;
        for (const auto& run : runs) {
            Buffer buffer(hb_buffer_create());
            if (!hb_buffer_allocation_successful(buffer.get())) return result;
            hb_buffer_set_content_type(buffer.get(), HB_BUFFER_CONTENT_TYPE_UNICODE);
            hb_buffer_set_direction(buffer.get(), levels[run.begin] & 1 ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            hb_buffer_set_script(buffer.get(), scalars[run.begin].script);
            hb_buffer_set_language(buffer.get(), language);
            hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
            hb_buffer_set_flags(buffer.get(), HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
            for (auto i = run.begin; i < run.end; ++i)
                hb_buffer_add(buffer.get(), scalars[i].value, scalars[i].byte);
            if (!hb_buffer_allocation_successful(buffer.get())) return result;
            static const char* const shapers[]{"ot", nullptr};
            if (!hb_shape_full(scalars[run.begin].font, buffer.get(), nullptr, 0, shapers) ||
                !hb_buffer_allocation_successful(buffer.get())) return result;
            unsigned int count = 0;
            const auto* infos = hb_buffer_get_glyph_infos(buffer.get(), &count);
            const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
            if (count > max_glyphs - result.glyphs.size() || (count && (!infos || !positions))) return result;
            for (unsigned int i = 0; i < count; ++i) {
                if (infos[i].cluster >= result.bytes || positions[i].y_advance ||
                    std::abs(x) > max_width_26 || std::abs(std::int64_t(positions[i].x_offset)) > max_width_26 ||
                    std::abs(std::int64_t(positions[i].y_offset)) > max_width_26) return result;
                result.glyphs.push_back({infos[i].codepoint, infos[i].cluster, scalars[run.begin].slot,
                    x + positions[i].x_offset, -std::int64_t(positions[i].y_offset), positions[i].x_advance});
                x += positions[i].x_advance;
            }
        }
        if (x < 0 || x > max_width_26 || refusals != refused_allocations()) return result;
        result.width_26 = x;
        // Cluster boundaries remain logical byte offsets even for mixed bidi.
        // Accumulating their advances permits one bounded prefix re-shape.
        auto logical = result.glyphs;
        std::sort(logical.begin(), logical.end(), [](const Glyph& a, const Glyph& b) { return a.cluster < b.cluster; });
        for (const auto& glyph : logical) {
            if (result.clusters.empty() || result.clusters.back().begin != glyph.cluster)
                result.clusters.push_back({glyph.cluster, static_cast<std::uint32_t>(result.bytes), 0});
            result.clusters.back().advance_26 += glyph.advance_26;
        }
        for (std::size_t i = 1; i < result.clusters.size(); ++i) result.clusters[i - 1].end = result.clusters[i].begin;
        result.valid = true;
    } catch (const std::bad_alloc&) {
        // Partial vectors own their storage; callers never publish this layout.
    }
    return result;
}
std::size_t prefix(const Layout& layout, int width) noexcept {
    if (!layout.valid || width < 0) return 0;
    const auto limit = std::int64_t(width) * 64;
    if (layout.width_26 <= limit) return layout.bytes;
    std::int64_t advance = 0;
    std::size_t bytes = 0;
    for (const auto& cluster : layout.clusters) {
        advance += cluster.advance_26;
        if (advance > limit) break;
        bytes = cluster.end;
    }
    return bytes;
}
}
