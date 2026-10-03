// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace uke {
struct StockImage {
    const char* name;
    std::uint64_t source_bytes, partition_bytes;
    const char* source_sha256;
    const char* partition_sha256;
    const char* programming_layout;
};
inline constexpr std::uint64_t recovery_bytes = 104857600;
// The DTBO source is smaller than its OEM partition. Its reviewed whole-
// partition digest includes the preserved source, zero gap and duplicated AVB
// footer produced by AOSP fastboot copy_avb_footer. It is not a physical dump.
inline constexpr std::array<StockImage, 5> global_stock{{
    {"boot", 100663296, 100663296, "efdee1d4e1acd7f6e77615330dbcb045caeeccad8568d89fa6abd627f606e77f", "efdee1d4e1acd7f6e77615330dbcb045caeeccad8568d89fa6abd627f606e77f", "exact-source"},
    {"init_boot", 8388608, 8388608, "c4eb22f5c379678aead0cf7ab003f7534d923be16b0601a0b4da7d1705af10b2", "c4eb22f5c379678aead0cf7ab003f7534d923be16b0601a0b4da7d1705af10b2", "exact-source"},
    {"vendor_boot", 100663296, 100663296, "c2811677d6aa07753b615747c4f2dba110dd4519cf52ff3a89e41e00a5b02bcc", "c2811677d6aa07753b615747c4f2dba110dd4519cf52ff3a89e41e00a5b02bcc", "exact-source"},
    {"dtbo", 20971520, 25165824, "044aae9d9a144e9a05b91d2785a2ff4504c78caa11f8c22781839ba2f6c76490", "9e55ff8afdf178e424187f0dc7d6dd2fa570308e22d8df7ac895d65017dbc0d7", "aosp-fastboot-copy-avb-footer"},
    {"recovery", recovery_bytes, recovery_bytes, "a22c93ccd0d439d610547a47ab4d8001f72ee769f791991f65d47d5724db049b", "a22c93ccd0d439d610547a47ab4d8001f72ee769f791991f65d47d5724db049b", "exact-source"}
}};
inline bool valid_hash(std::string_view s) {
    if (s.size() != 64) return false;
    for (const char c : s) if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    return true;
}
struct Evidence {
    std::string device, vbmeta_state, flash_locked, slot_suffix, merge;
    int slots = -1, current = -1;
    bool fallback_bootable = false;
};
inline int validate(const Evidence& e) {
    if (e.device != "uke") throw std::runtime_error("Device is not uke");
    if (e.vbmeta_state != "unlocked" || e.flash_locked != "0")
        throw std::runtime_error("Unlocked bootloader evidence is incomplete");
    if (e.slots != 2 || (e.current != 0 && e.current != 1))
        throw std::runtime_error("Expected exactly two boot slots");
    if (e.slot_suffix != (e.current == 0 ? "_a" : "_b"))
        throw std::runtime_error("Boot-control slot disagrees with boot properties");
    if (e.merge != "none") throw std::runtime_error("Snapshot state is not none");
    if (!e.fallback_bootable) throw std::runtime_error("Inactive slot is not bootable");
    return e.current;
}
} // namespace uke
