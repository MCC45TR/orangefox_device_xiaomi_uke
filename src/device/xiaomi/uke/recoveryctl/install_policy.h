// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace uke {
struct StockImage { const char* name; std::uint64_t bytes; const char* sha256; };
inline constexpr std::uint64_t recovery_bytes = 104857600;
inline constexpr std::array<StockImage, 5> global_stock{{
    {"boot", 100663296, "efdee1d4e1acd7f6e77615330dbcb045caeeccad8568d89fa6abd627f606e77f"},
    {"init_boot", 8388608, "c4eb22f5c379678aead0cf7ab003f7534d923be16b0601a0b4da7d1705af10b2"},
    {"vendor_boot", 100663296, "c2811677d6aa07753b615747c4f2dba110dd4519cf52ff3a89e41e00a5b02bcc"},
    {"dtbo", 25165824, "044aae9d9a144e9a05b91d2785a2ff4504c78caa11f8c22781839ba2f6c76490"},
    {"recovery", recovery_bytes, "a22c93ccd0d439d610547a47ab4d8001f72ee769f791991f65d47d5724db049b"}
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
