// SPDX-License-Identifier: Apache-2.0
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <string>

bool ForceNormalBoot(const std::string&, const std::string&);
void InvokeResumeControl(const std::string&);
static bool recovery_mode;
static unsigned parsed_bootconfig;
static unsigned writable_opens;
static unsigned fd_writes;
bool IsRecoveryMode() { return recovery_mode; }

namespace android::fs_mgr {
void GetBootconfigFromString(const std::string& value, const std::string&, std::string* result) {
    ++parsed_bootconfig;
    *result = value;
}
}
namespace android::base {
bool WriteStringToFd(const std::string&, int) {
    ++fd_writes;
    return false;
}
}
extern "C" int __wrap_open(const char*, int flags, ...) {
    if ((flags & O_ACCMODE) != O_RDONLY) ++writable_opens;
    errno = EPERM;
    return -1;
}

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    const std::string commands[] = {
        "", "androidboot.force_normal_boot=0", "androidboot.force_normal_boot=1",
        "androidboot.force_normal_boot=0 androidboot.force_normal_boot=1",
        "androidboot.force_normal_boot=1 twrpfastboot=1"
    };
    const std::string configs[] = {
        "", "androidboot.force_normal_boot = \"0\"",
        "androidboot.force_normal_boot = \"1\"",
        "androidboot.force_normal_boot = \"0\"\nandroidboot.force_normal_boot = \"1\""
    };
    recovery_mode = true;
    for (const auto& command : commands)
        for (const auto& config : configs)
            require(!ForceNormalBoot(command, config),
                    "Recovery root was discarded for a normal-boot marker");
    InvokeResumeControl("");
    InvokeResumeControl("/dev/fixture-resume");
    require(parsed_bootconfig == 0 && writable_opens == 0 && fd_writes == 0,
            "Recovery attempted hibernation resume before refusal");
    recovery_mode = false;
    require(!ForceNormalBoot("", ""), "Empty non-recovery input changed");
    require(!ForceNormalBoot(commands[1], configs[1]), "Zero non-recovery markers changed");
    require(ForceNormalBoot(commands[2], ""), "Non-recovery command-line boot changed");
    require(ForceNormalBoot("", configs[2]), "Non-recovery bootconfig boot changed");
    require(!ForceNormalBoot(commands[4], configs[2]), "Non-recovery fastboot override changed");
    InvokeResumeControl("");
    require(parsed_bootconfig == 1 && writable_opens == 0, "Empty non-recovery resume changed");
    InvokeResumeControl("/dev/fixture-resume");
    require(parsed_bootconfig == 2 && writable_opens == 1 && fd_writes == 0,
            "Non-recovery resume no longer reached its intercepted sysfs open");
    std::cout << "Recovery first-stage root retained for all 20 marker combinations; recovery resume denied before parsing/open; non-recovery controls passed.\n";
}
