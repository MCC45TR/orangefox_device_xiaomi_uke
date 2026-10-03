// SPDX-License-Identifier: Apache-2.0
#define main recovery_installer_entry
#include "../src/device/xiaomi/uke/recoveryctl/installer.cpp"
#undef main
#include <functional>

void expect_rejection(const std::function<void()>& f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Unsafe evidence was accepted");
}
int main(int argc, char* argv[]) {
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
        if (argc == 2) {
            const auto& pin = uke::global_stock[3];
            if (std::string_view(pin.name) != "dtbo" || pin.source_bytes != 20971520 || pin.partition_bytes != 25165824 ||
                std::string_view(pin.programming_layout) != "aosp-fastboot-copy-avb-footer")
                throw std::runtime_error("DTBO source/partition programming contract differs");
            Fd original(open((std::filesystem::path(argv[1]) / "dtbo.img").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
            struct stat original_stat{};
            if (fstat(original.value, &original_stat) != 0 || !S_ISREG(original_stat.st_mode) || original_stat.st_size != static_cast<off_t>(pin.source_bytes) ||
                digest(original.value, pin.source_bytes) != pin.source_sha256)
                throw std::runtime_error("Independent OEM DTBO source pin differs");
            char normal_path[] = "/tmp/uke-installer-dtbo-XXXXXX";
            Fd normal(mkstemp(normal_path));
            struct RemoveFixture { const char* path; ~RemoveFixture() { unlink(path); } } remove{normal_path};
            if (ftruncate(normal.value, static_cast<off_t>(pin.partition_bytes)) != 0) throw std::runtime_error("Cannot size normalized fixture");
            copy(original.value, normal.value, pin.source_bytes);
            std::array<char, 64> footer{};
            if (pread(original.value, footer.data(), footer.size(), static_cast<off_t>(pin.source_bytes - footer.size())) != static_cast<ssize_t>(footer.size()) ||
                std::string_view(footer.data(), 4) != "AVBf" ||
                pwrite(normal.value, footer.data(), footer.size(), static_cast<off_t>(pin.partition_bytes - footer.size())) != static_cast<ssize_t>(footer.size()))
                throw std::runtime_error("Cannot independently duplicate OEM AVB footer");
            const Block normalized{normal_path, "dtbo-fixture", 0, pin.partition_bytes};
            require_stock(normalized, pin.partition_sha256);
            if (digest(normal.value, pin.source_bytes) != pin.source_sha256 || digest(normal.value, pin.partition_bytes) == pin.source_sha256)
                throw std::runtime_error("DTBO source/whole-partition hashes were conflated");
            auto corrupt = [&](std::uint64_t at) {
                char old = 0;
                if (pread(normal.value, &old, 1, static_cast<off_t>(at)) != 1) throw std::runtime_error("Cannot read corruption fixture");
                const char changed = static_cast<char>(old ^ 0x55);
                if (pwrite(normal.value, &changed, 1, static_cast<off_t>(at)) != 1) throw std::runtime_error("Cannot alter corruption fixture");
                expect_rejection([&] { require_stock(normalized, pin.partition_sha256); });
                if (pwrite(normal.value, &old, 1, static_cast<off_t>(at)) != 1) throw std::runtime_error("Cannot restore corruption fixture");
            };
            corrupt(128); corrupt(pin.source_bytes + 4096); corrupt(pin.partition_bytes - 1);
            if (ftruncate(normal.value, static_cast<off_t>(pin.source_bytes)) != 0) throw std::runtime_error("Cannot truncate fixture");
            expect_rejection([&] { require_stock(normalized, pin.partition_sha256); });
        }
        std::cout << "Installer slot, snapshot, full-partition DTBO/footer corruption, hash, copy and host-refusal tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
