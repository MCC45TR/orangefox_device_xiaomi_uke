// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ure-locale-keys.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ure_locale {
using Lookup = std::function<std::string(const std::string&, const std::string&)>;
inline std::string translate(std::string_view text, const Lookup& lookup) {
    const auto* end = std::end(entries);
    const auto* entry = std::lower_bound(std::begin(entries), end, text,
        [](const Entry& item, std::string_view value) { return item.source < value; });
    return entry != end && entry->source == text ? lookup(std::string(entry->key), std::string(text)) : std::string(text);
}
// Typed composition keeps paths, model IDs, numbers and hashes opaque. It also
// allows text to be rendered in the current language. Callers must invalidate
// the previous warning/journal review before a mutation after a language switch.
struct Message {
    std::vector<std::pair<bool, std::string>> parts;
    Message& prose(std::string text) { parts.emplace_back(true, std::move(text)); return *this; }
    Message& data(std::string text) { parts.emplace_back(false, std::move(text)); return *this; }
    std::string text() const { std::string out; for (const auto& item : parts) out += item.second; return out; }
    std::string render(const Lookup& lookup) const {
        std::string out;
        for (const auto& item : parts) out += item.first ? translate(item.second, lookup) : item.second;
        return out;
    }
};
inline std::mutex messages_mutex;
inline std::map<std::string, Message> messages;
inline void remember(const std::string& variable, const Message& message) {
    std::lock_guard<std::mutex> lock(messages_mutex); messages[variable] = message;
}
inline bool display_variable(const std::string& variable, const std::string& raw, const Lookup& lookup, std::string& output) {
    static const std::vector<std::string_view> prose_variables = {
        "ure_status", "ure_scale_status", "ure_scale_warning", "ure_mirror_status", "ure_manage_summary",
        "ure_boot_summary", "ure_stock_job_summary", "ure_layout_review", "ure_maintenance_state", "ure_mb_report"
    };
    static const std::map<std::string, std::map<std::string, std::string>> choices = {
        {"ure_layout_mode", {{"standard", "Standard"}, {"advanced", "Advanced"}}},
        {"ure_layout_placement", {{"after_userdata", "After userdata"}, {"before_userdata", "Before userdata"}}},
        {"ure_layout_userdata_policy", {{"preserve", "Preserve data"}, {"erase_recreate", "Erase and recreate"}}},
        {"ure_fs_action", {{"format", "Format"}, {"check", "Check"}, {"repair", "Repair"}, {"resize", "Resize"}}},
        {"ure_raw_kind", {{"image", "Image"}, {"live", "Live storage"}}}
    };
    const auto choice = choices.find(variable);
    if (choice != choices.end()) {
        const auto selected = choice->second.find(raw);
        output = selected == choice->second.end() ? raw : translate(selected->second, lookup); return true;
    }
    if (std::find(prose_variables.begin(), prose_variables.end(), variable) == prose_variables.end()) return false;
    Message remembered; bool found = false;
    {
        std::lock_guard<std::mutex> lock(messages_mutex);
        const auto item = messages.find(variable);
        if (item != messages.end() && item->second.text() == raw) { remembered = item->second; found = true; }
    }
    if (found) { output = remembered.render(lookup); return true; }
    output = translate(raw, lookup);
    if (output != raw) return true;
    // Stable error codes are retained. Translate an exact known error message
    // after the code; never search or replace text inside user-supplied paths.
    const auto colon = raw.find(": ");
    if (colon != std::string::npos && colon <= 80 &&
        raw.substr(0, colon).find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") == std::string::npos)
        output = raw.substr(0, colon + 2) + translate(std::string_view(raw).substr(colon + 2), lookup);
    return true;
}
}
