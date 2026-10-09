// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "touch-policy.hpp"
#include <cstring>
#include <linux/dm-ioctl.h>
#include <span>
#include <sys/sysmacros.h>
namespace uke::touch {
struct Backing {
    unsigned major = 0, minor = 0;
    std::uint64_t start = 0, length = 0;
};
inline std::optional<std::vector<Backing>> linear_reply(std::span<const unsigned char> bytes,
                                                        unsigned expected_major, unsigned expected_minor,
                                                        std::string_view name) {
    if (bytes.size() < sizeof(dm_ioctl) || bytes.size() > 65536)
        return {};
    dm_ioctl io{};
    std::memcpy(&io, bytes.data(), sizeof(io));
    if (io.version[0] != DM_VERSION_MAJOR || !(io.flags & DM_ACTIVE_PRESENT_FLAG) ||
        (io.flags & (DM_BUFFER_FULL_FLAG | DM_SUSPEND_FLAG | DM_INACTIVE_PRESENT_FLAG)) ||
        io.target_count == 0 || io.target_count > 64 || io.data_size > bytes.size() ||
        io.data_start < sizeof(io) || io.data_start % alignof(dm_target_spec) != 0 ||
        io.data_start >= io.data_size || expected_major != major(static_cast<dev_t>(io.dev)) ||
        expected_minor != minor(static_cast<dev_t>(io.dev)) || !std::memchr(io.name, 0, sizeof(io.name)) ||
        std::string_view(io.name) != name)
        return {};
    std::vector<Backing> result;
    std::size_t offset = io.data_start;
    std::uint64_t next_sector = 0;
    for (unsigned i = 0; i < io.target_count; ++i) {
        if (offset % alignof(dm_target_spec) != 0 || offset > io.data_size ||
            sizeof(dm_target_spec) > io.data_size - offset)
            return {};
        dm_target_spec target{};
        std::memcpy(&target, bytes.data() + offset, sizeof(target));
        if (!std::memchr(target.target_type, 0, sizeof(target.target_type)) ||
            std::string_view(target.target_type) != "linear" ||
            target.sector_start != next_sector || target.length == 0 ||
            target.length > UINT64_MAX - next_sector)
            return {};
        next_sector += target.length;
        // DM_TABLE_STATUS next is relative to the FIRST target, unlike LOAD.
        const std::size_t end = i + 1 == io.target_count
                                    ? io.data_size
                                    : static_cast<std::size_t>(io.data_start) + target.next;
        if (end <= offset + sizeof(target) || end > io.data_size)
            return {};
        const char *params = reinterpret_cast<const char *>(bytes.data() + offset + sizeof(target));
        const auto *nul =
            static_cast<const char *>(std::memchr(params, 0, end - offset - sizeof(target)));
        if (!nul)
            return {};
        std::istringstream words{std::string(params, static_cast<std::size_t>(nul - params))};
        std::string dev, start, extra;
        Backing b;
        b.length = target.length;
        if (!(words >> dev >> start) || (words >> extra) || !device(dev, b.major, b.minor))
            return {};
        const auto parsed = std::from_chars(start.data(), start.data() + start.size(), b.start);
        if (parsed.ec != std::errc{} || parsed.ptr != start.data() + start.size() ||
            b.start > UINT64_MAX - b.length)
            return {};
        result.push_back(b);
        offset = end;
    }
    return result;
}
} // namespace uke::touch
