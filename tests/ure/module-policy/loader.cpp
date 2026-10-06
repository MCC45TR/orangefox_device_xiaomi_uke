// SPDX-License-Identifier: Apache-2.0
// Host-only fixture: compile the actual libmodprobe parser/loader and extension.
// Link wrappers replace packaged-recovery detection and module/process syscalls.
// No kernel module is inserted; every finit_module attempt is recorded.
#include <modprobe/modprobe.h>
#include <android-base/logging.h>

#include <sys/syscall.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
std::atomic<bool> recovery_present{true};
std::atomic<unsigned> external_handler_attempts{0};
std::mutex attempt_lock;
std::set<std::string> attempts;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}
void Require(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}
void Write(const fs::path& path, const std::string& value) {
    std::ofstream output(path);
    Require(static_cast<bool>(output), "fixture file open failed");
    output << value;
    Require(static_cast<bool>(output), "fixture file write failed");
}
void ResetAttempts() {
    std::lock_guard lock(attempt_lock);
    attempts.clear();
}
void NoDeniedAttempts() {
    std::lock_guard lock(attempt_lock);
    Require(!attempts.count("charger_partition.ko"), "charger module reached finit_module");
    Require(!attempts.count("ufs_ffu.ko"), "UFS updater reached finit_module");
    Require(!attempts.count("qti_battery_charger.ko"), "hard dependent reached finit_module");
    Require(!attempts.count("indirect.ko"), "indirect dependent reached finit_module");
}
void Synthetic(const fs::path& base) {
    fs::create_directories(base);
    const std::vector<std::string> names = {"charger_partition", "ufs_ffu",
        "qti_battery_charger", "indirect", "usb_safe", "soft_safe", "stock_forbidden"};
    for (const auto& name : names) Write(base / (name + ".ko"), "host fixture\n");
    Write(base / "modules.dep",
        "charger_partition.ko:\nufs_ffu.ko:\n"
        "qti_battery_charger.ko: charger_partition.ko\n"
        "indirect.ko: qti_battery_charger.ko charger_partition.ko\n"
        "usb_safe.ko:\nsoft_safe.ko:\nstock_forbidden.ko:\n");
    Write(base / "modules.alias", "alias of:charger* charger_partition\n"
        "alias of:ufs* ufs_ffu\nalias of:dependent* indirect\n");
    Write(base / "modules.softdep", "softdep soft_safe pre: charger_partition ufs_ffu "
        "post: charger_partition ufs_ffu\n");
    Write(base / "modules.options", "");
    Write(base / "modules.blocklist", "blocklist stock_forbidden\n");
    Write(base / "modules.load.recovery", "charger_partition.ko\nufs_ffu.ko\n"
        "qti_battery_charger.ko\nindirect.ko\nusb_safe.ko\nsoft_safe.ko\n"
        "stock_forbidden.ko\n");
}
void Stock(const fs::path& base, bool parallel) {
    ResetAttempts();
    Modprobe stock({base.string()}, "modules.load.recovery", false);
    const std::vector<std::string> denied = {"charger_partition", "ufs_ffu",
        "qti_battery_charger", "qcom-hv-haptics", "mi_thermal_interface",
        "qcom-spmi-wled", "leds-qti-flash"};
    for (const auto& name : denied) Require(stock.IsBlocklisted(name), "stock denial: " + name);
    for (const auto& name : {"dwc3-msm", "phy-msm-ssusb-qmp", "phy-msm-snps-eusb2", "msm_drm",
                            "qcom_tsens", "qcom_lpm", "qcom-cpufreq-hw", "qti_cpufreq_cdev",
                            "cpu_voltage_cooling", "ddr_cdev", "qti_devfreq_cdev"}) {
        Require(!stock.IsBlocklisted(name), "independent stock driver denied: " + std::string(name));
    }
    Require(parallel ? stock.LoadModulesParallel(2) : stock.LoadListedModules(true),
            "actual stock list/dependencies failed");
    NoDeniedAttempts();
    std::lock_guard lock(attempt_lock);
    for (const auto& name : denied) {
        std::string canonical = name;
        for (auto& c : canonical) if (c == '-') c = '_';
        // File names retain stock hyphens; inspect both forms.
        Require(!attempts.count(name + ".ko") && !attempts.count(canonical + ".ko"),
                "stock denied dependent reached finit_module: " + name);
    }
    for (const auto& name : {"dwc3-msm.ko", "phy-msm-ssusb-qmp.ko", "phy-msm-snps-eusb2.ko",
                            "msm_drm.ko", "qcom_tsens.ko", "qcom_lpm.ko", "qti_cpufreq_cdev.ko",
                            "cpu_voltage_cooling.ko", "ddr_cdev.ko", "qti_devfreq_cdev.ko"}) {
        Require(attempts.count(name), "stock independent driver not reached: " + std::string(name));
    }
}
}  // namespace

extern "C" int __wrap_access(const char* path, int mode) {
    Require(std::string(path) == "/system/bin/recovery" && mode == F_OK,
            "unexpected source access() call");
    if (recovery_present.load()) return 0;
    errno = ENOENT;
    return -1;
}

extern "C" pid_t __wrap_fork() {
    ++external_handler_attempts;
    errno = EPERM;
    return -1;
}

extern "C" int __wrap_execv(const char*, char* const[]) {
    Fail("external handler execution is forbidden in this host fixture");
}

extern "C" long __wrap_syscall(long number, ...) {
    Require(number == __NR_finit_module, "unexpected syscall; none will be forwarded");
    va_list args;
    va_start(args, number);
    const int fd = va_arg(args, int);
    const char* options = va_arg(args, const char*);
    const int flags = va_arg(args, int);
    va_end(args);
    Require(options != nullptr && flags == 0, "invalid mock module arguments");
    char path[4096] = {};
    const std::string link = "/proc/self/fd/" + std::to_string(fd);
    const auto size = readlink(link.c_str(), path, sizeof(path) - 1);
    Require(size > 0, "module fd cannot be identified");
    std::lock_guard lock(attempt_lock);
    attempts.insert(fs::path(path).filename().string());
    return 0;
}

int main(int argc, char** argv) {
    Require(argc == 3, "usage: fixture SYNTHETIC_DIR STOCK_FIXTURE_DIR");
    android::base::SetMinimumLogSeverity(android::base::FATAL);
    const fs::path synthetic(argv[1]);
    Synthetic(synthetic);
    recovery_present = true;
    for (bool use_oem_blocklist : {false, true}) {
        Modprobe loader({synthetic.string()}, "modules.load.recovery", use_oem_blocklist);
        Require(loader.IsBlocklisted("charger-partition.ko"), "canonical denial absent");
        Require(loader.IsBlocklisted("/lib/modules/ufs_ffu.ko"), "path denial absent");
        Require(loader.IsBlocklisted("indirect"), "hard-dependency denial absent");
        ResetAttempts();
        for (const auto& name : {"charger_partition", "ufs_ffu", "of:charger0", "of:ufs0",
                                "charger-partition.ko", "/lib/modules/ufs_ffu.ko",
                                "qti_battery_charger", "indirect", "of:dependent0"}) {
            Require(!loader.LoadWithAliases(name, true), "denied direct/alias request succeeded");
            Require(loader.LoadWithAliases(name, false), "non-strict API contract changed");
            NoDeniedAttempts();
        }
        Require(loader.LoadWithAliases("soft_safe", true), "safe softdep owner failed");
        Require(loader.LoadWithAliases("usb_safe", true), "safe direct request failed");
        NoDeniedAttempts();
    }
    for (bool parallel : {false, true}) {
        ResetAttempts();
        Modprobe loader({synthetic.string()}, "modules.load.recovery", false);
        Require(parallel ? loader.LoadModulesParallel(2) : loader.LoadListedModules(true),
                "sequential/parallel denied-module skip failed");
        NoDeniedAttempts();
        Require(!attempts.count("stock_forbidden.ko"), "stock denial lost");
    }
    fs::remove(synthetic / "modules.blocklist");
    ResetAttempts();
    Modprobe missing_list({synthetic.string()}, "modules.load.recovery", false);
    Require(missing_list.LoadListedModules(true), "missing OEM blocklist failed");
    NoDeniedAttempts();

    Stock(argv[2], false);
    Stock(argv[2], true);

    recovery_present = false;
    ResetAttempts();
    Modprobe normal({synthetic.string()}, "modules.load.recovery", false);
    Require(!normal.IsBlocklisted("charger_partition") && !normal.IsBlocklisted("ufs_ffu"),
            "normal boot policy changed");
    Require(normal.LoadWithAliases("of:charger0", true) && normal.LoadWithAliases("of:ufs0", true),
            "normal alias behavior changed");
    Require(attempts.count("charger_partition.ko") && attempts.count("ufs_ffu.ko"),
            "normal mock insertion was not exercised");
    Require(external_handler_attempts == 0, "unexpected external handler request");
    std::cout << "PASS: actual parser/loader/extension; direct, aliases, hard/soft dependencies, "
                 "sequential/parallel, missing OEM list, explicit bypass, exact stock dependency "
                 "graph, and normal-boot behavior. Module syscalls were mocked.\n";
}
