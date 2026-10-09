// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ure {
namespace readonly_fstab {

// This is an admission rule for the measured Uke wrapped-key layout, not a
// general Android fstab parser. A different crypto policy requires review.
constexpr std::size_t kMaxFileBytes = 65536;
constexpr std::size_t kMaxLineBytes = 2047;
constexpr const char* kKeyDirectory = "/metadata/vold/metadata_encryption";
constexpr const char* kFileEncryption =
    "fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized+wrappedkey_v0";
constexpr const char* kMetadataEncryption =
    "metadata_encryption=aes-256-xts:wrappedkey_v0";

struct Entry {
    std::string block_device;
    std::string mount_point;
    std::string filesystem;
    std::string mount_options;
    std::string manager_options;
    std::string key_directory;
};

struct Import {
    bool accepted = false;
    std::string reason;
    Entry metadata;
    Entry data;
    std::string additional_fstab;
};

inline std::vector<std::string> split(std::string_view input, char delimiter) {
    std::vector<std::string> result;
    while (true) {
        const auto separator = input.find(delimiter);
        result.emplace_back(input.substr(0, separator));
        if (separator == std::string_view::npos) return result;
        input.remove_prefix(separator + 1);
    }
}

inline Import refuse(const char* reason) {
    Import result;
    result.reason = reason;
    return result;
}

inline std::string serialize(const Entry& entry) {
    return entry.block_device + " " + entry.mount_point + " " + entry.filesystem +
        " ro,nosuid,nodev,noatime," + entry.mount_options + " " + entry.manager_options + "\n";
}

inline Import sanitize(std::string_view input) {
    if (input.empty() || input.size() > kMaxFileBytes) return refuse("invalid-file-size");
    for (unsigned char character : input) {
        if ((character < 32 && character != '\t' && character != '\n' && character != '\r') ||
            character == 127) return refuse("invalid-file-encoding");
    }
    Import result;
    bool have_data = false;
    bool have_metadata = false;
    for (const auto& line : split(input, '\n')) {
        if (line.size() > kMaxLineBytes) return refuse("oversized-line");
        std::istringstream stream(line);
        std::vector<std::string> fields;
        for (std::string field; stream >> field;) fields.push_back(field);
        if (fields.empty() || fields.front().front() == '#') continue;
        if (fields.size() < 2 || (fields[1] != "/data" && fields[1] != "/metadata")) continue;
        if (fields.size() != 5) return refuse("ambiguous-target-fields");
        const bool data = fields[1] == "/data";
        bool& present = data ? have_data : have_metadata;
        if (present) return refuse("duplicate-target-entry");
        present = true;
        const std::string name = data ? "userdata" : "metadata";
        if (fields[0] != "/dev/block/bootdevice/by-name/" + name &&
            fields[0] != "/dev/block/by-name/" + name) return refuse("unreviewed-block-path");
        if (fields[2] != "f2fs" && fields[2] != "ext4") return refuse("unsupported-filesystem");
        bool inlinecrypt = false;
        for (const auto& option : split(fields[3], ',')) {
            if (option.empty()) return refuse("empty-mount-option");
            if (option == "inlinecrypt") inlinecrypt = true;
        }
        constexpr std::array<std::string_view, 3> crypto_names = {
            "fileencryption", "keydirectory", "metadata_encryption"
        };
        const std::array<std::string, 3> crypto_values = {
            kFileEncryption, std::string("keydirectory=") + kKeyDirectory, kMetadataEncryption
        };
        std::array<bool, 3> crypto_present{};
        bool wrappedkey = false;
        for (const auto& option : split(fields[4], ',')) {
            if (option.empty()) return refuse("empty-manager-option");
            const std::string_view option_name = std::string_view(option).substr(0, option.find('='));
            if (option_name == "wrappedkey") {
                if (data || wrappedkey || option != "wrappedkey") return refuse("unreviewed-wrappedkey-marker");
                wrappedkey = true;
            }
            for (std::size_t index = 0; index != crypto_names.size(); ++index) {
                if (option_name != crypto_names[index]) continue;
                if (!data || crypto_present[index] || option != crypto_values[index])
                    return refuse("unreviewed-crypto-option");
                crypto_present[index] = true;
            }
            if (option_name == "forceencrypt" || option_name == "encryptable" ||
                option_name == "forcefdeorfbe") return refuse("unsupported-encryption-mode");
        }
        if (data && (!inlinecrypt || !crypto_present[0] || !crypto_present[1] || !crypto_present[2]))
            return refuse("incomplete-crypto-profile");
        Entry& entry = data ? result.data : result.metadata;
        entry.block_device = fields[0];
        entry.mount_point = fields[1];
        entry.filesystem = fields[2];
        entry.mount_options = entry.filesystem == "f2fs" ? "norecovery" : "noload";
        entry.manager_options = "wait";
        if (data) {
            entry.mount_options += ",inlinecrypt";
            entry.key_directory = kKeyDirectory;
            for (const auto& value : crypto_values) entry.manager_options += "," + value;
        } else if (wrappedkey) {
            entry.manager_options += ",wrappedkey";
        }
    }
    if (!have_data || !have_metadata) return refuse("incomplete-target-pair");
    result.additional_fstab = serialize(result.metadata) + serialize(result.data);
    result.accepted = true;
    return result;
}

inline bool same_block_device(const std::string& imported, const std::string& trusted) {
    struct stat actual{};
    struct stat expected{};
    return !trusted.empty() && stat(imported.c_str(), &actual) == 0 &&
        stat(trusted.c_str(), &expected) == 0 && S_ISBLK(actual.st_mode) &&
        S_ISBLK(expected.st_mode) && actual.st_rdev == expected.st_rdev;
}

inline bool read_bounded(const std::string& path, std::string* output) {
    if (!output) return false;
    output->clear();
    const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) return false;
    struct stat info{};
    bool accepted = fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_size > 0 && static_cast<std::uint64_t>(info.st_size) <= kMaxFileBytes;
    std::string contents;
    if (accepted) {
        std::array<char, 4096> buffer{};
        for (;;) {
            const auto count = read(descriptor, buffer.data(), buffer.size());
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 || (count > 0 && contents.size() + count > kMaxFileBytes)) {
                accepted = false;
                break;
            }
            if (count == 0) break;
            contents.append(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    if (close(descriptor) != 0) accepted = false;
    if (accepted) *output = std::move(contents);
    return accepted;
}

// Only the fixed recovery-ramdisk destination is passed by the adapter.
inline bool publish_ramdisk_copy(const std::string& destination, const std::string& contents) {
    std::string pattern = destination + ".XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) return false;
    bool accepted = true;
    std::size_t written = 0;
    while (written != contents.size()) {
        const auto count = write(descriptor, contents.data() + written, contents.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { accepted = false; break; }
        written += static_cast<std::size_t>(count);
    }
    if (close(descriptor) != 0) accepted = false;
    if (accepted && rename(temporary.data(), destination.c_str()) != 0) accepted = false;
    if (!accepted) unlink(temporary.data());
    return accepted;
}

}  // namespace readonly_fstab
}  // namespace ure
