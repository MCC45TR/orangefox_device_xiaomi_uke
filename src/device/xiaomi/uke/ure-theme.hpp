// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ure-telemetry.hpp"
#include <cstdlib>

namespace ure::theme {
struct Overlay { std::string style, accent; bool active = false; };
inline std::mutex lock;
inline Overlay current, previous;
inline bool style_name(std::string_view name) {
    return name == "Black" || name == "Cream" || name == "Dark" || name == "Gray" || name == "Light";
}
inline bool color(std::string_view value) {
    return value.size() == 7 && value.front() == '#' &&
        std::all_of(value.begin() + 1, value.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
        });
}
inline bool accent_name(std::string_view name) {
    return !name.empty() && name.size() <= 32 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
    });
}
inline bool packaged(const char* root, const std::string& path, std::string& output) {
    telemetry::detail::Fd directory(::open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct open_how how{};
    how.flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    telemetry::detail::Fd file(static_cast<int>(::syscall(SYS_openat2, directory.get(), path.c_str(), &how, sizeof(how))));
    struct stat info{};
    constexpr std::size_t limit = 1024 * 1024;
    if (file.get() < 0 || ::fstat(file.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 1 || info.st_size > static_cast<off_t>(limit)) return false;
    std::string bytes;
    std::array<char, 8192> buffer{};
    for (unsigned attempts = 0; attempts < 256; ++attempts) {
        const auto count = ::read(file.get(), buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return false;
        if (!count) { output = std::move(bytes); return !output.empty(); }
        if (bytes.size() + static_cast<std::size_t>(count) > limit) return false;
        bytes.append(buffer.data(), static_cast<std::size_t>(count));
    }
    return false;
}
inline bool replace(std::string& xml, std::string_view token, std::string_view value) {
    std::size_t position = 0, count = 0;
    while ((position = xml.find(token, position)) != std::string::npos) {
        if (++count > 256) return false;
        xml.replace(position, token.size(), value);
        position += value.size();
    }
    return count > 0;
}
inline bool prepare(const std::string& style, const std::string& name, const std::string& light,
                    const std::string& dark, bool dark_accent, Overlay& result, const char* root = "/twres") {
    if (!style_name(style) || !accent_name(name) || !color(light) || !color(dark)) return false;
    Overlay candidate;
    if (!packaged(root, "themes/styles/" + style + ".xml", candidate.style) ||
        !packaged(root, "themes/sed/accent.xml", candidate.accent)) return false;
    if (!replace(candidate.accent, "#ACCENT_NAME#", name) ||
        !replace(candidate.accent, "#COLOR#", dark_accent ? dark : light) ||
        !replace(candidate.accent, "#COLOR_LIGHT#", light) ||
        !replace(candidate.accent, "#COLOR_DARK#", dark)) return false;
    candidate.active = true;
    result = std::move(candidate);
    return true;
}
inline void commit(Overlay candidate) {
    const std::lock_guard<std::mutex> guard(lock);
    previous = std::move(current);
    current = std::move(candidate);
}
inline void rollback() {
    const std::lock_guard<std::mutex> guard(lock);
    current = std::move(previous);
}
// The loader owns/frees the returned buffer, exactly like its file reader.
inline bool buffer(const std::string& filename, const std::string& theme_path, char** output) {
    const std::lock_guard<std::mutex> guard(lock);
    if (!current.active || theme_path.empty()) return false;
    const std::string* xml = nullptr;
    if (filename == theme_path + "/style.xml") xml = &current.style;
    else if (filename == theme_path + "/accent.xml") xml = &current.accent;
    if (!xml) return false;
    *output = static_cast<char*>(::malloc(xml->size() + 1));
    if (*output) std::memcpy(*output, xml->c_str(), xml->size() + 1);
    return true;
}
} // namespace ure::theme
