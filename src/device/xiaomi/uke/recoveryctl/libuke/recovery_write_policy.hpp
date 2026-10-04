// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string_view>

namespace ure {
// Legacy recovery entry points carry neither a pinned target nor a reviewed,
// durable transaction. UI preferences, Android properties, fstab flags and a
// claimed advanced mode must never authorize those entry points. Future live
// writes belong in the reviewed URE backend, not in an allow switch here.
enum class LegacyWrite {
    Wipe, Format, Repair, Resize, Restore, Flash, InstallPackage,
    PartitionMetadata, SlotSwitch, BootPatch, FileMutation, Script,
    WritableMount, EncryptionAccess
};
struct RecoveryWriteDecision {
    bool allowed;
    const char* code;
    const char* message;
};
constexpr bool live_storage_backend_accepted() noexcept { return false; }
constexpr RecoveryWriteDecision legacy_write_decision(LegacyWrite) noexcept {
    return {false, "ure-legacy-write-unavailable",
        "Device writes are unavailable: verified device and firmware checks, a backup and a reviewed URE storage plan are required."};
}
constexpr const char* legacy_write_name(LegacyWrite operation) noexcept {
    switch (operation) {
        case LegacyWrite::Wipe: return "wipe";
        case LegacyWrite::Format: return "format";
        case LegacyWrite::Repair: return "repair";
        case LegacyWrite::Resize: return "resize";
        case LegacyWrite::Restore: return "restore";
        case LegacyWrite::Flash: return "flash";
        case LegacyWrite::InstallPackage: return "install-package";
        case LegacyWrite::PartitionMetadata: return "partition-metadata";
        case LegacyWrite::SlotSwitch: return "slot-switch";
        case LegacyWrite::BootPatch: return "boot-patch";
        case LegacyWrite::FileMutation: return "file-mutation";
        case LegacyWrite::Script: return "script";
        case LegacyWrite::WritableMount: return "writable-mount";
        case LegacyWrite::EncryptionAccess: return "encryption-access";
    }
    return "unknown";
}
// MS_RDONLY alone permits ext4 journal replay. Preserve the no-replay option
// even when a filesystem mount retries without user-supplied fstab options.
constexpr const char* legacy_read_only_recovery_option(std::string_view filesystem) noexcept {
    return filesystem == "ext4" ? "noload" : filesystem == "f2fs" ? "norecovery" : "";
}
// Fastbootd keeps transport/readback and explicit reboot routes. Everything
// else, including newly added and OEM commands, is denied before dispatch.
constexpr bool legacy_fastboot_command_permitted(std::string_view command) noexcept {
    return command == "getvar" || command == "download" || command == "fetch" ||
        command == "reboot" || command == "reboot-bootloader" ||
        command == "reboot-fastboot" || command == "reboot-recovery" || command == "shutdown";
}
} // namespace ure
