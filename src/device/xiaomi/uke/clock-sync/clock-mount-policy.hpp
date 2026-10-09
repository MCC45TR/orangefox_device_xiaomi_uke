// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ure::clock_mount {
inline bool option(std::string_view values, std::string_view expected) {
    while (!values.empty()) {
        const auto end = values.find(',');
        if (values.substr(0, end) == expected) return true;
        if (end == std::string_view::npos) break;
        values.remove_prefix(end + 1);
    }
    return false;
}
struct Mount { std::string device, target, flags, filesystem, super_flags; };
inline bool persist_event(std::string_view text, std::string_view node) {
    if (text.empty() || text.size() > 512 || text.back() != '\n') return false;
    unsigned names = 0, devices = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        if (end == std::string_view::npos) return false;
        const auto line = text.substr(0, end);
        if (line.rfind("PARTNAME=", 0) == 0) {
            if (++names != 1 || line.substr(9) != "persist") return false;
        }
        if (line.rfind("DEVNAME=", 0) == 0) {
            if (++devices != 1 || line.substr(8) != node) return false;
        }
        text.remove_prefix(end + 1);
    }
    return names == 1 && devices == 1;
}
inline bool mounts(std::string_view contents, std::vector<Mount>& result, bool require_private) {
    if (contents.empty() || contents.size() > 131072 || contents.back() != '\n') return false;
    std::istringstream input{std::string(contents)};
    std::string line;
    while (std::getline(input, line)) {
        if (result.size() >= 512 || line.size() > 4096) return false;
        std::istringstream row(line);
        std::vector<std::string> fields;
        std::string field;
        while (row >> field) { fields.push_back(field); if (fields.size() > 64) return false; }
        if (fields.size() < 10) return false;
        std::size_t separator = 6;
        for (; separator < fields.size() && fields[separator] != "-"; ++separator) {
            if (require_private && (fields[separator].rfind("shared:", 0) == 0 ||
                fields[separator].rfind("master:", 0) == 0 || fields[separator] == "propagate_from" ||
                fields[separator].rfind("propagate_from:", 0) == 0)) return false;
        }
        if (separator + 4 != fields.size() || fields[2].find(':') == std::string::npos ||
            fields[4].empty() || fields[4][0] != '/') return false;
        result.push_back({fields[2], fields[4], fields[5], fields[separator + 1], fields[separator + 3]});
    }
    return !result.empty();
}
inline bool persist_contract(std::string_view partition, std::string_view start,
                             std::string_view sectors, std::uint64_t bytes, int logical_sector) {
    // Uke LUN5, persist entry7: 6656*4096 to 14848*4096. Sysfs uses 512-byte sectors.
    return partition == "7" && start == "53248" && sectors == "65536" &&
        bytes == 33554432 && logical_sector == 4096;
}
inline bool readonly_mount(const Mount& row, std::string_view device) {
    return row.device == device && row.target == "/tmp/uke-clock-persist" && row.filesystem == "ext4" &&
        option(row.flags, "ro") && option(row.flags, "nosuid") && option(row.flags, "nodev") &&
        option(row.flags, "noexec") && option(row.super_flags, "ro") &&
        (option(row.super_flags, "noload") || option(row.super_flags, "norecovery"));
}
} // namespace ure::clock_mount
