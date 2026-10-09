// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone, single-threaded recovery helper. No RTC or persistent write.
#include "ure-clock.hpp"
#include "clock-mount-policy.hpp"
#include <linux/fs.h>
#include <linux/magic.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/statfs.h>
#include <sys/sysmacros.h>
#include <cstdio>

namespace {
using Fd = ure::telemetry::detail::Fd;
bool table(std::vector<ure::clock_mount::Mount>& rows, bool require_private) {
    Fd descriptor(::open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    struct statfs type{};
    if (descriptor.get() < 0 || ::fstatfs(descriptor.get(), &type) || type.f_type != PROC_SUPER_MAGIC) return false;
    std::string contents;
    std::array<char, 4096> bytes{};
    for (unsigned reads = 0; reads < 40; ++reads) {
        const auto count = ::read(descriptor.get(), bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return false;
        if (!count) return ure::clock_mount::mounts(contents, rows, require_private);
        contents.append(bytes.data(), static_cast<std::size_t>(count));
        if (contents.size() > 131072) return false;
    }
    return false;
}
bool identity(int descriptor, dev_t& identity) {
    struct stat node{};
    std::uint64_t bytes = 0;
    int logical_sector = 0;
    if (::fstat(descriptor, &node) || !S_ISBLK(node.st_mode) ||
        ::ioctl(descriptor, BLKGETSIZE64, &bytes) || ::ioctl(descriptor, BLKSSZGET, &logical_sector)) return false;
    ure::telemetry::detail::Reader sys("/sys");
    Fd entry(sys.directory("class/block/sdf7"));
    if (entry.get() < 0) return false;
    const auto device = sys.attribute(entry.get(), "dev");
    const auto partition = sys.attribute(entry.get(), "partition");
    const auto start = sys.attribute(entry.get(), "start");
    const auto size = sys.attribute(entry.get(), "size");
    Fd event(ure::telemetry::detail::beneath(entry.get(), "uevent", O_RDONLY | O_NONBLOCK | O_NOFOLLOW));
    std::array<char, 513> event_bytes{};
    ssize_t event_size = -1;
    for (unsigned attempts = 0; attempts < 4; ++attempts) {
        event_size = ::pread(event.get(), event_bytes.data(), event_bytes.size(), 0);
        if (event_size >= 0 || errno != EINTR) break;
    }
    if (event_size <= 0 || event_size > 512 ||
        !ure::clock_mount::persist_event(std::string_view(event_bytes.data(), static_cast<std::size_t>(event_size)), "sdf7")) return false;
    identity = node.st_rdev;
    const std::string expected = std::to_string(major(identity)) + ":" + std::to_string(minor(identity));
    for (const auto* value : {&device, &partition, &start, &size})
        if (value->state != ure::telemetry::State::observed) return false;
    std::vector<std::string> holders, slaves;
    if (sys.entries("class/block/sdf7/holders", holders) != ure::telemetry::State::observed || !holders.empty() ||
        sys.entries("class/block/sdf/slaves", slaves) != ure::telemetry::State::observed || !slaves.empty()) return false;
    return device.text == expected && ure::clock_mount::persist_contract(partition.text, start.text, size.text, bytes, logical_sector);
}
int run() {
    // The direct node avoids accepting a mutable by-name alias to another LUN.
    Fd source(::open("/dev/block/sdf7", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    if (source.get() < 0)
        return errno == ENOENT || errno == ENODEV || errno == ENXIO ? 2 : 22;
    dev_t device = 0;
    if (!identity(source.get(), device)) return 3;
    const std::string number = std::to_string(major(device)) + ":" + std::to_string(minor(device));
    std::vector<ure::clock_mount::Mount> before;
    if (!table(before, false)) return 4;
    for (const auto& row : before) if (row.device == number) return 5;
    std::array<unsigned char, 2> magic{};
    if (::pread(source.get(), magic.data(), magic.size(), 1080) != 2 || magic[0] != 0x53 || magic[1] != 0xef) return 6;
    // This process has no other threads or inherited helper subprocesses.
    if (::unshare(CLONE_NEWNS) || ::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr)) return 7;
    std::vector<ure::clock_mount::Mount> private_rows;
    if (!table(private_rows, true)) return 8;
    if (::mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=0700,size=65536") ||
        ::mkdir("/tmp/uke-clock-persist", 0700)) return 9;
    struct statfs memory{};
    if (::statfs("/tmp", &memory) || memory.f_type != TMPFS_MAGIC) return 10;
    const std::string held_source = "/proc/self/fd/" + std::to_string(source.get());
    if (::mount(held_source.c_str(), "/tmp/uke-clock-persist", "ext4",
        MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, "noload")) return 11;
    Fd mounted(::open("/tmp/uke-clock-persist", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat mounted_info{};
    struct statvfs flags{};
    if (mounted.get() < 0 || ::fstat(mounted.get(), &mounted_info) || mounted_info.st_dev != device ||
        ::fstatvfs(mounted.get(), &flags) || !(flags.f_flag & ST_RDONLY)) return 12;
    std::vector<ure::clock_mount::Mount> final_rows;
    if (!table(final_rows, true)) return 13;
    unsigned matched = 0;
    for (const auto& row : final_rows) if (row.device == number) {
        if (!ure::clock_mount::readonly_mount(row, number)) return 14;
        ++matched;
    }
    if (matched != 1) return 15;
    std::array<unsigned char, 8> offset{};
    if (!ure::clock::read_offset(mounted.get(), offset)) return 16;
    ure::telemetry::detail::Reader sys("/sys");
    Fd rtc(sys.directory("class/rtc/rtc0"));
    if (rtc.get() < 0) return 17;
    const auto counter = sys.attribute(rtc.get(), "since_epoch");
    if (counter.state != ure::telemetry::State::observed) return 17;
    const auto observed = ure::clock::compose(counter.text, offset);
    dev_t checked = 0;
    if (observed.state != ure::clock::State::observed || !identity(source.get(), checked) || checked != device) return 18;
    timespec now{};
    const auto seconds = static_cast<time_t>(observed.milliseconds / 1000);
    if (::clock_gettime(CLOCK_REALTIME, &now)) return 19;
    if (now.tv_sec >= seconds - 2 && now.tv_sec <= seconds + 2) return 0;
    const timespec corrected{seconds, static_cast<long>((observed.milliseconds % 1000) * 1000000)};
    return ::clock_settime(CLOCK_REALTIME, &corrected) ? 20 : 0;
}
} // namespace
int main(int argc, char**) {
    if (argc != 1 || ::geteuid() != 0) return 21;
    int status = 2;
    // First-stage block discovery can finish after the service starts. Retry
    // only a missing source, before any namespace or filesystem is mounted.
    for (unsigned attempts = 0; attempts < 20; ++attempts) {
        status = run();
        if (status != 2) break;
        if (attempts != 19) ::usleep(250000);
    }
    // Fixed status only: no calibration, identifiers or ATS bytes in logs.
    if (status) std::fprintf(stderr, "uke-clock-sync: unavailable (%d)\n", status);
    return status;
}
