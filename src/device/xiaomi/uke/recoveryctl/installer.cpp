// SPDX-License-Identifier: Apache-2.0
// Uke Global stock303 installer. All checks precede the sole block write.
#include "install_policy.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <linux/fs.h>
#include <openssl/sha.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace {
struct Fd {
    int value;
    explicit Fd(int n) : value(n) { if (n < 0) throw std::runtime_error(std::strerror(errno)); }
    ~Fd() { close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
std::string property(const char* name) {
#ifdef __ANDROID__
    char value[PROP_VALUE_MAX]{};
    __system_property_get(name, value);
    return value;
#else
    (void)name;
    throw std::runtime_error("Installation is available only in Android recovery");
#endif
}
struct Command { int status; std::string output; };
Command bootctl(const char* action, const char* argument = nullptr) {
    int ends[2];
    if (pipe2(ends, O_CLOEXEC) != 0) throw std::runtime_error("Cannot create bootctl pipe");
    Fd read_end(ends[0]), write_end(ends[1]);
    const pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("Cannot start bootctl");
    if (pid == 0) {
        dup2(write_end.value, STDOUT_FILENO);
        dup2(write_end.value, STDERR_FILENO);
        close(read_end.value);
        execl("/system/bin/bootctl", "bootctl", action, argument, static_cast<char*>(nullptr));
        _exit(127);
    }
    close(write_end.value); write_end.value = -1;
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    int status = 0;
    for (;;) {
        pollfd p{read_end.value, POLLIN | POLLHUP, 0};
        if (poll(&p, 1, 100) < 0 && errno != EINTR) {
            kill(pid, SIGKILL); waitpid(pid, &status, 0);
            throw std::runtime_error("Cannot read bootctl result");
        }
        if (p.revents & (POLLIN | POLLHUP)) {
            char buf[256]; const ssize_t n = read(read_end.value, buf, sizeof(buf));
            if (n > 0) output.append(buf, static_cast<std::size_t>(n));
        }
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0 || output.size() > 1024 || std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL); waitpid(pid, &status, 0);
            throw std::runtime_error("bootctl timed out or returned invalid output");
        }
    }
    // Drain any final bytes after the child exits.
    for (;;) { char b[256]; const ssize_t n = read(read_end.value, b, sizeof(b));
        if (n <= 0) break;
        output.append(b, static_cast<std::size_t>(n));
        if (output.size() > 1024) throw std::runtime_error("Oversized bootctl output");
    }
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    return {WIFEXITED(status) ? WEXITSTATUS(status) : 128, output};
}
std::string query(const char* action) {
    const auto c = bootctl(action);
    if (c.status != 0) throw std::runtime_error(std::string("bootctl failed: ") + action);
    return c.output;
}
int slot_number(const std::string& value) {
    if (value == "0") return 0;
    if (value == "1") return 1;
    throw std::runtime_error("Invalid boot-control slot");
}
uke::Evidence evidence() {
    uke::Evidence e;
    e.device = property("ro.product.device");
    e.vbmeta_state = property("ro.boot.vbmeta.device_state");
    e.flash_locked = property("ro.boot.flash.locked");
    e.slot_suffix = property("ro.boot.slot_suffix");
    e.slots = query("get-number-slots") == "2" ? 2 : -1;
    e.current = slot_number(query("get-current-slot"));
    e.merge = query("get-snapshot-merge-status");
    e.fallback_bootable = bootctl("is-slot-bootable", e.current == 0 ? "1" : "0").status == 0;
    return e;
}
std::string digest(int fd, std::uint64_t bytes) {
    SHA256_CTX state; SHA256_Init(&state);
    std::array<unsigned char, 65536> buffer{};
    for (std::uint64_t offset = 0; offset < bytes;) {
        const auto wanted = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), bytes - offset));
        const ssize_t n = pread(fd, buffer.data(), wanted, static_cast<off_t>(offset));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("Image read failed or ended early");
        SHA256_Update(&state, buffer.data(), static_cast<std::size_t>(n));
        offset += static_cast<std::uint64_t>(n);
    }
    unsigned char sum[SHA256_DIGEST_LENGTH]; SHA256_Final(sum, &state);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : sum) { result += hex[c >> 4]; result += hex[c & 15]; }
    return result;
}
struct Block { std::string path, label; dev_t identity; std::uint64_t bytes; };
Block block(const std::string& label, std::uint64_t expected_bytes) {
    const auto path = std::filesystem::canonical("/dev/block/by-name/" + label);
    Fd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    struct stat s{};
    if (fstat(fd.value, &s) != 0 || !S_ISBLK(s.st_mode)) throw std::runtime_error("Target is not a block device");
    std::ifstream stream("/sys/dev/block/" + std::to_string(major(s.st_rdev)) + ":" + std::to_string(minor(s.st_rdev)) + "/uevent");
    std::string line; bool matched = false;
    while (std::getline(stream, line)) if (line == "PARTNAME=" + label) matched = true;
    std::uint64_t bytes = 0;
    if (!matched || ioctl(fd.value, BLKGETSIZE64, &bytes) != 0 || bytes != expected_bytes)
        throw std::runtime_error("Partition label or size does not match stock303 policy");
    return {path.string(), label, s.st_rdev, bytes};
}
void require_stock(const Block& b, const char* expected) {
    Fd fd(open(b.path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (digest(fd.value, b.bytes) != expected)
        throw std::runtime_error("Firmware hash mismatch: " + b.label);
}
void copy(int source, int destination, std::uint64_t bytes) {
    std::array<unsigned char, 65536> buf{};
    for (std::uint64_t offset = 0; offset < bytes;) {
        const auto wanted = static_cast<std::size_t>(std::min<std::uint64_t>(buf.size(), bytes - offset));
        const ssize_t n = pread(source, buf.data(), wanted, static_cast<off_t>(offset));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("Copy source read failed");
        for (ssize_t done = 0; done < n;) {
            const ssize_t written = pwrite(destination, buf.data() + done, static_cast<std::size_t>(n - done), static_cast<off_t>(offset + done));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::runtime_error("Copy destination write failed");
            done += written;
        }
        offset += static_cast<std::uint64_t>(n);
    }
    if (fsync(destination) != 0) throw std::runtime_error("Destination sync failed");
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc != 4 || (std::string_view(argv[1]) != "check" && std::string_view(argv[1]) != "install")) {
        std::cerr << "Usage: uke-recovery-install check|install IMAGE SHA256\n"; return 2;
    }
    try {
        if (!uke::valid_hash(argv[3])) throw std::runtime_error("Invalid image SHA-256");
        Fd image(open(argv[2], O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        struct stat s{};
        if (fstat(image.value, &s) != 0 || !S_ISREG(s.st_mode) || s.st_size != static_cast<off_t>(uke::recovery_bytes))
            throw std::runtime_error("Expected one regular 100 MiB recovery image");
        if (digest(image.value, uke::recovery_bytes) != argv[3]) throw std::runtime_error("Release image hash mismatch");
        const int active = uke::validate(evidence());
        const std::string suffix = active == 0 ? "_a" : "_b";
        for (const auto& stock : uke::global_stock) {
            if (std::string_view(stock.name) == "recovery") {
                const std::string fallback = active == 0 ? "_b" : "_a";
                require_stock(block(std::string(stock.name) + fallback, stock.partition_bytes), stock.partition_sha256);
            } else {
                // Dedicated recovery depends on that slot's kernel/vendor/DT
                // stack. A bootable flag alone does not establish fallback ABI.
                for (const char* slot : {"_a", "_b"})
                    require_stock(block(std::string(stock.name) + slot, stock.partition_bytes), stock.partition_sha256);
            }
        }
        const auto target = block("recovery" + suffix, uke::recovery_bytes);
        std::cout << "Verified Global stock303 boot stack; plan: " << target.label
                  << " only. Inactive stock recovery and all boot partitions are preserved.\n";
        if (std::string_view(argv[1]) == "check") return 0;
        struct statvfs space{};
        if (statvfs("/tmp", &space) != 0 || space.f_frsize == 0 || space.f_bavail < (uke::recovery_bytes + space.f_frsize - 1) / space.f_frsize)
            throw std::runtime_error("Insufficient RAM-disk space for recovery backup");
        char backup_path[] = "/tmp/uke-recovery-backup-XXXXXX";
        Fd backup(mkstemp(backup_path));
        Fd old(open(target.path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        copy(old.value, backup.value, target.bytes);
        if (digest(old.value, target.bytes) != digest(backup.value, target.bytes)) throw std::runtime_error("Backup verification failed");
        std::cout << "Current recovery backup: " << backup_path << " (volatile; copy to host before reboot).\n";
        if (uke::validate(evidence()) != active) throw std::runtime_error("Slot changed during preflight");
        if (digest(image.value, target.bytes) != argv[3]) throw std::runtime_error("Image changed during preflight");
        Fd output(open(target.path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW));
        struct stat out{};
        if (fstat(output.value, &out) != 0 || out.st_rdev != target.identity || !S_ISBLK(out.st_mode))
            throw std::runtime_error("Target changed during preflight");
        copy(image.value, output.value, target.bytes);
        if (digest(output.value, target.bytes) != argv[3]) throw std::runtime_error("Recovery read-back mismatch; use preserved stock fallback");
        std::cout << "Active recovery written and SHA-256 read-back verified. Reboot manually.\n";
    } catch (const std::exception& e) { std::cerr << "uke-recovery-install: " << e.what() << '\n'; return 1; }
    return 0;
}
