// SPDX-License-Identifier: GPL-3.0-or-later
#include "clock-mount-policy.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    unsigned controls = 0;
    const auto check = [&](bool value) { ++controls; if (!value) throw std::runtime_error("clock mount policy control failed"); };
    using namespace ure::clock_mount;
    const std::string private_root = "1 0 0:1 / / rw - rootfs rootfs rw\n";
    std::vector<Mount> rows;
    check(mounts(private_root, rows, true));
    rows.clear(); check(!mounts("1 0 0:1 / / rw shared:1 - rootfs rootfs rw\n", rows, true));
    rows.clear(); check(!mounts("1 0 0:1 / / rw master:2 - rootfs rootfs rw\n", rows, true));
    rows.clear(); check(!mounts("1 0 0:1 / / rw propagate_from:2 - rootfs rootfs rw\n", rows, true));
    rows.clear(); check(mounts("1 0 0:1 / / rw shared:1 - rootfs rootfs rw\n", rows, false));
    rows.clear(); check(!mounts(private_root.substr(0, private_root.size() - 1), rows, true));
    rows.clear(); check(!mounts("1 0 0:1 / / rw - rootfs rootfs\n", rows, true));
    rows.clear(); check(!mounts("1 0 0:1 / / rw - rootfs rootfs rw unexpected\n", rows, true));
    rows.clear(); check(!mounts(std::string(131073, 'x'), rows, true));
    std::string many;
    for (unsigned i = 0; i < 513; ++i) many += private_root;
    rows.clear(); check(!mounts(many, rows, true));
    check(persist_contract("7", "53248", "65536", 33554432, 4096));
    check(persist_event("DEVNAME=sdf7\nPARTNAME=persist\n", "sdf7"));
    check(!persist_event("DEVNAME=sdf7\nPARTNAME=foreign\n", "sdf7"));
    check(!persist_event("DEVNAME=sdf6\nPARTNAME=persist\n", "sdf7"));
    check(!persist_event("DEVNAME=sdf7\nPARTNAME=persist\nPARTNAME=persist\n", "sdf7"));
    check(!persist_event("PARTNAME=persist\n", "sdf7"));
    check(!persist_event("DEVNAME=sdf7\nPARTNAME=persist", "sdf7"));
    check(!persist_contract("6", "53248", "65536", 33554432, 4096));
    check(!persist_contract("7", "53249", "65536", 33554432, 4096));
    check(!persist_contract("7", "53248", "65535", 33554432, 4096));
    check(!persist_contract("7", "53248", "65536", 33554433, 4096));
    check(!persist_contract("7", "53248", "65536", 33554432, 512));
    const Mount good{"8:87", "/tmp/uke-clock-persist", "ro,nosuid,nodev,noexec", "ext4", "ro,norecovery"};
    check(readonly_mount(good, "8:87"));
    auto changed = good; changed.super_flags = "ro,noload"; check(readonly_mount(changed, "8:87"));
    for (const auto* flags : {"rw,nosuid,nodev,noexec", "ro,nodev,noexec", "ro,nosuid,noexec", "ro,nosuid,nodev"}) {
        changed = good; changed.flags = flags; check(!readonly_mount(changed, "8:87"));
    }
    for (const auto* flags : {"ro", "rw,norecovery", "ro,fake-norecovery"}) {
        changed = good; changed.super_flags = flags; check(!readonly_mount(changed, "8:87"));
    }
    changed = good; changed.device = "8:88"; check(!readonly_mount(changed, "8:87"));
    changed = good; changed.target = "/persist"; check(!readonly_mount(changed, "8:87"));
    changed = good; changed.filesystem = "f2fs"; check(!readonly_mount(changed, "8:87"));
    std::cout << "PASS " << controls << " clock namespace admission controls; no clock or mount syscall\n";
}
