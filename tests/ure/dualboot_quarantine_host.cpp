// SPDX-License-Identifier: Apache-2.0
// Real host bridge and persistent coordinator; disposable files only.
#include "lifecycle_policy.hpp"
#include <filesystem>
#include <stdexcept>
#include <cstdio>

#if defined(__ANDROID__) || !defined(URE_HOST_POLICY_FIXTURE)
#error This is the host lifecycle bridge fixture.
#endif
namespace {
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
struct Fixture {
    std::filesystem::path base,quarantine,phase;
    explicit Fixture(const std::filesystem::path& parent) {
        check(parent.is_absolute() && std::filesystem::is_directory(parent),"An explicit persistent host fixture directory is required");
        std::string name=(parent/"ure-dualboot-host-XXXXXX").string();
        check(::mkdtemp(name.data())!=nullptr,"Cannot create host lifecycle fixture");
        base=name.data(); quarantine=base/"quarantine"; phase=quarantine/"phase";
        check(::setenv("URE_GUI_JOB_REGISTRY",(base/"registry").c_str(),1)==0 &&
            ::setenv("URE_OPERATION_COORDINATOR",(base/"coordinator").c_str(),1)==0 &&
            ::setenv("URE_DUALBOOT_QUARANTINE",quarantine.c_str(),1)==0,"Cannot set private host domains");
    }
    ~Fixture() { std::error_code ignored; std::filesystem::remove_all(base,ignored); }
    void clear() { std::filesystem::remove(phase); std::filesystem::remove(quarantine); }
    void publish(ure::DualbootQuarantineState state) {
        check(::mkdir(quarantine.c_str(),0700)==0 || errno==EEXIST,"Cannot create host marker directory");
        const auto* bytes=ure::dualboot_quarantine_marker(state); check(bytes!=nullptr,"Invalid host marker state");
        const int fd=::open(phase.c_str(),O_WRONLY|O_TRUNC|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        check(fd>=0,"Cannot create host marker");
        const auto length=std::strlen(bytes); const auto count=::write(fd,bytes,length);
        check(count>=0 && static_cast<std::size_t>(count)==length && ::close(fd)==0,"Cannot publish host marker");
    }
};
void no_lease_leak() {
    auto writer=ure::RuntimeActivityLease::acquire(nullptr,true);
    check(writer.valid(),"Refused bridge operation retained runtime ownership");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Supply one explicit persistent fixture parent directory");
        Fixture fixture(argv[1]);
        {
            ure::LegacyLifecycleGuard guard("mount"); check(guard.active(),"Clear host bridge refused mount");
            fixture.publish(ure::DualbootQuarantineState::Pending);
            check(!guard.active(),"Retained host lifecycle failed to recheck pending quarantine");
        }
        { ure::LegacyLifecycleGuard guard("reboot"); check(!guard.active(),"Pending host bridge permitted reboot"); }
        no_lease_leak();
        check(!ure::stage_legacy_reboot(),"Pending host bridge staged reboot"); no_lease_leak();
        fixture.clear(); check(ure::stage_legacy_reboot(),"Clear host bridge could not stage reboot");
        fixture.publish(ure::DualbootQuarantineState::Pending);
        check(!ure::stage_legacy_reboot(),"Staged host bridge ignored pending quarantine"); no_lease_leak();
        fixture.clear(); check(ure::stage_legacy_reboot(),"Cannot stage host adoption control");
        fixture.publish(ure::DualbootQuarantineState::Pending);
        { ure::LegacyLifecycleGuard guard("reboot",nullptr,true); check(!guard.active(),"Host staged adoption ignored pending quarantine"); }
        no_lease_leak();
        for(const auto state:{ure::DualbootQuarantineState::Committed,ure::DualbootQuarantineState::GptRestored}) {
            fixture.publish(state);
            for(const auto* action:{"mount","unmount","shutdown"}) { ure::LegacyLifecycleGuard guard(action); check(!guard.active(),"Completed host quarantine permitted non-reboot lifecycle"); }
            check(ure::stage_legacy_reboot(),"Completed host quarantine could not stage reboot");
            { ure::LegacyLifecycleGuard guard("reboot",nullptr,true); check(guard.active(),"Completed host quarantine refused staged reboot"); }
            no_lease_leak();
        }
        std::puts("PASS host bridge quarantine: post-acquisition policy, active recheck, staged invalidation/adoption refusal and token release; real disposable coordinator, no device effects.");
        return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
