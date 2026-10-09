// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Uke RTC is a UTC counter. Read only the installed Android ATS_TOD offset;
// never start time_daemon, mount storage, change the RTC, or persist an offset.
#include "ure-telemetry.hpp"
#include <sys/statvfs.h>
#include <time.h>

namespace ure::clock {
enum class State { unavailable, invalid, observed, applied, denied };
struct Snapshot {
    State state = State::unavailable;
    std::uint64_t milliseconds = 0;
};
inline bool plausible(std::uint64_t milliseconds) {
    return milliseconds >= 1577836800000ULL && milliseconds < 4102444800000ULL;
}
inline Snapshot compose(std::string_view counter, const std::array<unsigned char, 8>& bytes) {
    if (counter.empty() || counter.size() > 10) return {State::invalid};
    std::uint64_t seconds = 0;
    for (char c : counter) {
        if (c < '0' || c > '9') return {State::invalid};
        seconds = seconds * 10 + static_cast<unsigned>(c - '0');
    }
    if (seconds > 4102444800ULL) return {State::invalid};
    std::uint64_t offset = 0;
    for (unsigned i = 0; i < bytes.size(); ++i) offset |= std::uint64_t(bytes[i]) << (i * 8);
    // Bound before adding: no uint64 overflow or guessed offset fallback.
    if (offset >= 4102444800000ULL) return {State::invalid};
    const auto value = seconds * 1000 + offset;
    return plausible(value) ? Snapshot{State::observed, value} : Snapshot{State::invalid};
}
inline bool read_offset(int directory, std::array<unsigned char, 8>& bytes) {
    struct open_how how{};
    how.flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    telemetry::detail::Fd file(static_cast<int>(::syscall(SYS_openat2, directory, "time/ats_2", &how, sizeof(how))));
    struct stat info{};
    if (file.get() < 0 || ::fstat(file.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size != 8) return false;
    std::array<unsigned char, 9> record{};
    std::size_t used = 0;
    for (unsigned attempts = 0; attempts < 8; ++attempts) {
        const auto count = ::read(file.get(), record.data() + used, record.size() - used);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return false;
        if (count == 0) {
            if (used != bytes.size()) return false;
            std::copy_n(record.begin(), bytes.size(), bytes.begin());
            return true;
        }
        used += static_cast<std::size_t>(count);
        if (used > bytes.size()) return false;
    }
    return false;
}
inline Snapshot observe() {
    telemetry::detail::Reader sys("/sys");
    telemetry::detail::Fd rtc(sys.directory("class/rtc/rtc0"));
    if (rtc.get() < 0) return {};
    const auto counter = sys.attribute(rtc.get(), "since_epoch");
    if (counter.state != telemetry::State::observed) return {};
    struct stat device{};
    if (::stat("/dev/block/by-name/persist", &device) != 0 || !S_ISBLK(device.st_mode)) return {};
    // These are the two installed-daemon persist aliases. Never scan generic
    // ATS files or open /data through a write-enabled recovery mount helper.
    for (const char* path : {"/persist", "/mnt/vendor/persist"}) {
        struct open_how how{};
        how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
        how.resolve = RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
        telemetry::detail::Fd root(static_cast<int>(::syscall(SYS_openat2, AT_FDCWD, path, &how, sizeof(how))));
        struct stat mounted{};
        struct statvfs flags{};
        if (root.get() < 0 || ::fstat(root.get(), &mounted) != 0 || mounted.st_dev != device.st_rdev ||
            ::fstatvfs(root.get(), &flags) != 0 || !(flags.f_flag & ST_RDONLY)) continue;
        std::array<unsigned char, 8> bytes{};
        if (read_offset(root.get(), bytes)) return compose(counter.text, bytes);
    }
    return {};
}
inline State apply() {
    const auto snapshot = observe();
    if (snapshot.state != State::observed) return snapshot.state;
    timespec now{};
    if (::clock_gettime(CLOCK_REALTIME, &now) != 0) return State::denied;
    const auto seconds = static_cast<time_t>(snapshot.milliseconds / 1000);
    // Repeated startup/settings calls derive UTC from the counter each time;
    // they never add an offset to an already corrected CLOCK_REALTIME.
    if (now.tv_sec >= seconds - 2 && now.tv_sec <= seconds + 2) return State::observed;
    const timespec corrected{seconds, static_cast<long>((snapshot.milliseconds % 1000) * 1000000)};
    return ::clock_settime(CLOCK_REALTIME, &corrected) == 0 ? State::applied : State::denied;
}
} // namespace ure::clock
