// SPDX-License-Identifier: Apache-2.0
#include "recovery_write_policy.hpp"
#include <array>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/types.h>
int ure_installer_main(int,char**);
namespace { int opens=0, forks=0, ioctls=0; }
// Never access a real file, spawn a bootctl process or perform a device ioctl.
extern "C" int __wrap_open(const char*,int,...) { ++opens; errno=ENOENT; return -1; }
extern "C" pid_t __wrap_fork() { ++forks; errno=EAGAIN; return -1; }
extern "C" int __wrap_ioctl(int,unsigned long,...) { ++ioctls; errno=ENOTTY; return -1; }
int main() {
    std::ostringstream diagnostics;
    auto* original=std::cerr.rdbuf(diagnostics.rdbuf());
    bool passed=true;
    for (std::string hash : {std::string(64,'a'),std::string("invalid-hash")}) {
        std::array<std::string,4> values={"uke-recovery-install","install","fixture-only",hash};
        std::array<char*,4> args={values[0].data(),values[1].data(),values[2].data(),values[3].data()};
        diagnostics.str("");
        passed=passed && ure_installer_main(4,args.data())==1 && opens==0 && forks==0 && ioctls==0;
        passed=passed && diagnostics.str().find(ure::legacy_write_decision(ure::LegacyWrite::Flash).message)!=std::string::npos;
    }
    std::array<std::string,4> values={"uke-recovery-install","check","fixture-only",std::string(64,'a')};
    std::array<char*,4> args={values[0].data(),values[1].data(),values[2].data(),values[3].data()};
    passed=passed && ure_installer_main(4,args.data())==1 && opens==1 && forks==0 && ioctls==0;
    std::cerr.rdbuf(original);
    if (!passed) { std::cerr << "Installer opened storage before denial or lost read-only check mode.\n"; return 1; }
    std::cout << "Production installer refused writes before file open, fork and ioctl; read-only check reached its input probe.\n";
}
