// SPDX-License-Identifier: Apache-2.0
#define main recovery_installer_entry
#include "../src/device/xiaomi/uke/recoveryctl/installer.cpp"
#undef main
#include <functional>

void expect_rejection(const std::function<void()>& f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Unsafe evidence was accepted");
}
int main() {
    try {
        const uke::Evidence good{"uke", "unlocked", "0", "_a", "none", 2, 0, true};
        if (uke::validate(good) != 0) throw std::runtime_error("Valid fixture rejected");
        auto b = good; b.current = 1; b.slot_suffix = "_b";
        if (uke::validate(b) != 1) throw std::runtime_error("B slot fixture rejected");
        for (const auto& status : {"merging", "snapshotted", "unknown", "cancelled", "", "none\nerror"}) {
            auto e = good; e.merge = status; expect_rejection([&] { uke::validate(e); });
        }
        auto e = good; e.device = "muyu"; expect_rejection([&] { uke::validate(e); });
        e = good; e.vbmeta_state = "locked"; expect_rejection([&] { uke::validate(e); });
        e = good; e.flash_locked = ""; expect_rejection([&] { uke::validate(e); });
        e = good; e.slots = 3; expect_rejection([&] { uke::validate(e); });
        e = good; e.slot_suffix = "_b"; expect_rejection([&] { uke::validate(e); });
        e = good; e.fallback_bootable = false; expect_rejection([&] { uke::validate(e); });
        if (uke::valid_hash("abc") || uke::valid_hash(std::string(64, 'x'))) throw std::runtime_error("Malformed hash accepted");
        char first[] = "/tmp/uke-installer-source-XXXXXX";
        char second[] = "/tmp/uke-installer-destination-XXXXXX";
        Fd source(mkstemp(first)), destination(mkstemp(second));
        unlink(first); unlink(second);
        if (write(source.value, "abc", 3) != 3) throw std::runtime_error("Fixture write failed");
        const std::string expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        if (digest(source.value, 3) != expected) throw std::runtime_error("SHA-256 mismatch");
        copy(source.value, destination.value, 3);
        if (digest(destination.value, 3) != expected) throw std::runtime_error("Read-back mismatch");
        expect_rejection([&] { digest(source.value, 4); });
        expect_rejection([&] { copy(source.value, destination.value, 4); });
        if (pwrite(destination.value, "x", 1, 1) != 1 || digest(destination.value, 3) == expected)
            throw std::runtime_error("Corrupt read-back not detected");
        expect_rejection([&] { property("ro.product.device"); });
        std::cout << "Installer slot, snapshot, hash, copy and host-refusal tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
