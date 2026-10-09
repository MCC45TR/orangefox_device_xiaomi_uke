// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace uke::touch {
// Preserve inherited lookup directories in this kernel RAM parameter.
inline std::optional<std::string> firmware_lookup(std::string_view inherited) {
    if (inherited.empty() || inherited.size() > 255)
        return {};
    std::size_t begin = 0;
    bool odm = false;
    while (begin < inherited.size()) {
        const auto end = inherited.find(',', begin);
        const auto part = inherited.substr(begin, end == std::string_view::npos ? end : end - begin);
        if (part.empty() || part.front() != '/' || part.find("..") != std::string_view::npos)
            return {};
        for (unsigned char c : part)
            if (c <= 32 || c >= 127)
                return {};
        if (part == "/odm/firmware") {
            if (odm)
                return {};
            odm = true;
        }
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    if (odm)
        return std::string(inherited);
    std::string next = "/odm/firmware," + std::string(inherited);
    return next.size() <= 255 ? std::optional<std::string>(std::move(next)) : std::nullopt;
}
} // namespace uke::touch
