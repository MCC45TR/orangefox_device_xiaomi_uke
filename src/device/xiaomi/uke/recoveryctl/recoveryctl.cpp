// SPDX-License-Identifier: Apache-2.0
// Fail-closed Uke recovery controls. No partition table or filesystem writes.
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace {
namespace fs = std::filesystem;

struct Partition {
    std::string device;
    std::string label;
    std::string uuid;
};

bool valid_uuid(std::string_view value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return true;
}

std::vector<Partition> inventory(const fs::path& root) {
    std::vector<Partition> result;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!fs::exists(entry.path() / "partition")) continue;
        std::ifstream stream(entry.path() / "uevent");
        if (!stream) continue;
        Partition item;
        item.device = entry.path().filename().string();
        std::string line;
        while (std::getline(stream, line)) {
            if (line.starts_with("PARTNAME=")) item.label = line.substr(9);
            if (line.starts_with("PARTUUID=")) item.uuid = line.substr(9);
        }
        if (item.label.find_first_of("\t\r\n") != std::string::npos ||
            !valid_uuid(item.uuid)) continue;
        result.push_back(std::move(item));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.device < b.device;
    });
    return result;
}

struct Target {
    const char* label;
    const char* mountpoint;
    const char* filesystem;
    const char* options;
};

Target target(std::string_view name) {
    if (name == "esp") return {"uke_esp", "/mnt/uke-esp", "vfat", "utf8=1"};
    if (name == "linux") return {"uke_linux", "/mnt/uke-linux", "ext4", "noload"};
    if (name == "linux-btrfs")
        return {"uke_linux", "/mnt/uke-linux", "btrfs", "rescue=nologreplay"};
    throw std::runtime_error("Target must be esp, linux or linux-btrfs");
}

Partition select(const std::vector<Partition>& entries, const Target& selected,
                 std::string_view uuid) {
    if (!valid_uuid(uuid)) throw std::runtime_error("Expected a full GPT PARTUUID");
    std::optional<Partition> found;
    for (const auto& item : entries) {
        if (item.label != selected.label) continue;
        if (item.uuid.size() != uuid.size() ||
            !std::equal(item.uuid.begin(), item.uuid.end(), uuid.begin(),
                        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                                      std::tolower(static_cast<unsigned char>(b)); })) continue;
        if (found) throw std::runtime_error("Ambiguous partition identity");
        found = item;
    }
    if (!found) throw std::runtime_error("No matching Uke partition label and PARTUUID");
    return *found;
}

std::optional<std::string> mounted_source(std::string_view mountpoint) {
    std::ifstream stream("/proc/self/mountinfo");
    if (!stream) throw std::runtime_error("Cannot read mount table");
    std::string line;
    while (std::getline(stream, line)) {
        const auto separator = line.find(" - ");
        if (separator == std::string::npos) continue;
        std::istringstream before(line.substr(0, separator));
        std::string id, parent, device, root, path;
        if (!(before >> id >> parent >> device >> root >> path) || path != mountpoint)
            continue;
        std::istringstream after(line.substr(separator + 3));
        std::string filesystem, source;
        if (!(after >> filesystem >> source))
            throw std::runtime_error("Incomplete mount table entry");
        return source;
    }
    return std::nullopt;
}

void mount_read_only(const Partition& item, const Target& selected) {
    const fs::path device = fs::path("/dev/block") / item.device;
    struct stat info{};
    if (stat(device.c_str(), &info) != 0 || !S_ISBLK(info.st_mode))
        throw std::runtime_error("Resolved partition is not a block device");
    if (mkdir(selected.mountpoint, 0755) != 0 && errno != EEXIST)
        throw std::runtime_error(std::string("Cannot create mount point: ") + std::strerror(errno));
    struct stat mountpoint_info{};
    if (lstat(selected.mountpoint, &mountpoint_info) != 0 || !S_ISDIR(mountpoint_info.st_mode))
        throw std::runtime_error("Mount point is not a real directory");
    if (mounted_source(selected.mountpoint))
        throw std::runtime_error("Mount point is already occupied");
    constexpr unsigned long flags = MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC;
    if (mount(device.c_str(), selected.mountpoint, selected.filesystem, flags,
              selected.options) != 0)
        throw std::runtime_error(std::string("Read-only mount failed: ") + std::strerror(errno));
}

int usage() {
    std::cerr << "Usage: uke-recoveryctl list [SYSFS_ROOT]\n"
                 "       uke-recoveryctl plan-mount esp|linux|linux-btrfs PARTUUID [SYSFS_ROOT]\n"
                 "       uke-recoveryctl mount-ro esp|linux|linux-btrfs PARTUUID\n"
                 "       uke-recoveryctl unmount esp|linux|linux-btrfs PARTUUID\n"
                 "       uke-recoveryctl rotation show|0|90|180|270\n";
    return 2;
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) return usage();
    try {
        const std::string_view command = argv[1];
        if (command == "list" && argc <= 3) {
            const auto entries = inventory(argc == 3 ? argv[2] : "/sys/class/block");
            std::cout << "device\tlabel\tpartuuid\n";
            for (const auto& item : entries)
                std::cout << item.device << '\t' << item.label << '\t' << item.uuid << '\n';
        } else if (command == "plan-mount" && (argc == 4 || argc == 5)) {
            const auto selected = target(argv[2]);
            const auto entries = inventory(argc == 5 ? argv[4] : "/sys/class/block");
            const auto item = select(entries, selected, argv[3]);
            std::cout << "read-only " << item.device << " (" << item.label << ", " << item.uuid
                      << ") -> " << selected.mountpoint << " as " << selected.filesystem << '\n';
        } else if (command == "mount-ro" && argc == 4) {
            const auto selected = target(argv[2]);
            const auto item = select(inventory("/sys/class/block"), selected, argv[3]);
            mount_read_only(item, selected);
            std::cout << selected.mountpoint << " mounted read-only\n";
        } else if (command == "unmount" && argc == 4) {
            const auto selected = target(argv[2]);
            const auto item = select(inventory("/sys/class/block"), selected, argv[3]);
            struct stat mountpoint_info{};
            if (lstat(selected.mountpoint, &mountpoint_info) != 0 || !S_ISDIR(mountpoint_info.st_mode))
                throw std::runtime_error("Mount point is not a real directory");
            const auto source = mounted_source(selected.mountpoint);
            if (!source || *source != (fs::path("/dev/block") / item.device).string())
                throw std::runtime_error("Mount source does not match requested PARTUUID");
            if (umount(selected.mountpoint) != 0)
                throw std::runtime_error(std::string("Unmount failed: ") + std::strerror(errno));
        } else if (command == "rotation" && argc == 3) {
#ifdef __ANDROID__
            if (std::string_view(argv[2]) == "show") {
                char current[PROP_VALUE_MAX]{};
                const int length = __system_property_get("persist.twrp.rotation", current);
                std::cout << (length > 0 ? current : "270 (build default)") << '\n';
            } else {
                const std::string_view angle = argv[2];
                if (angle != "0" && angle != "90" && angle != "180" && angle != "270")
                    return usage();
                if (__system_property_set("persist.twrp.rotation", argv[2]) != 0)
                    throw std::runtime_error("Recovery property service rejected rotation");
                std::cout << "Rotation saved for the next recovery UI initialization; "
                             "touch mapping must be checked on the device\n";
            }
#else
            throw std::runtime_error("Rotation property control is available only in recovery");
#endif
        } else {
            return usage();
        }
    } catch (const std::exception& error) {
        std::cerr << "uke-recoveryctl: " << error.what() << '\n';
        return 1;
    }
}
