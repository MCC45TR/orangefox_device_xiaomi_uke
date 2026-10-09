// SPDX-License-Identifier: Apache-2.0
// Dependency-free Android policy exercised on private host files. No tablet,
// mount, formatter, reboot or privileged raw device effect is performed.
#include "lifecycle_policy.hpp"
#include <cstdio>
#include <sys/wait.h>

#if !defined(__ANDROID__) || !defined(URE_HOST_POLICY_FIXTURE)
#error This executable is a host fixture of the shipping Android policy.
#endif
namespace {
void check(bool value,const char* message) noexcept {
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
struct Fixture {
    std::array<char,256> base{},registry{},quarantine{},real{},phase{},temporary{},hard{};
    Fixture() noexcept {
        std::strcpy(base.data(),"/tmp/ure-dualboot-quarantine-XXXXXX");
        check(::mkdtemp(base.data())!=nullptr,"Cannot create private fixture");
        auto path=[&](auto& output,const char* suffix) { check(std::snprintf(output.data(),output.size(),"%s/%s",base.data(),suffix)>0,"Cannot form fixture path"); };
        path(registry,"registry"); path(quarantine,"quarantine"); path(real,"real");
        path(phase,"quarantine/phase"); path(temporary,"quarantine/phase.new"); path(hard,"quarantine/phase.hard");
        check(::setenv("URE_GUI_JOB_REGISTRY",registry.data(),1)==0 && ::setenv("URE_DUALBOOT_QUARANTINE",quarantine.data(),1)==0,"Cannot select host fixture domains");
    }
    void directory() const noexcept { check(::mkdir(quarantine.data(),0700)==0 || errno==EEXIST,"Cannot create quarantine fixture directory"); }
    void content(const char* bytes,std::size_t length) const noexcept {
        directory(); const int fd=::open(temporary.data(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        check(fd>=0,"Cannot create fixture phase"); std::size_t done=0;
        while(done<length) {
            const auto count=::write(fd,bytes+done,length-done);
            if(count<0 && errno==EINTR)continue;
            check(count>0,"Cannot write fixture phase"); done+=static_cast<std::size_t>(count);
        }
        check(::fsync(fd)==0 && ::close(fd)==0 && ::rename(temporary.data(),phase.data())==0,"Cannot publish fixture phase");
    }
    void publish(ure::DualbootQuarantineState state) const noexcept {
        const auto* marker=ure::dualboot_quarantine_marker(state); check(marker!=nullptr,"Invalid fixture terminal phase"); content(marker,std::strlen(marker));
    }
    void clear() const noexcept {
        ::unlink(hard.data()); ::unlink(phase.data()); ::unlink(temporary.data()); ::rmdir(phase.data());
        check(::rmdir(quarantine.data())==0 || errno==ENOENT,"Cannot clear quarantine fixture");
    }
    ~Fixture() {
        clear(); std::array<char,256> lock{};
        std::snprintf(lock.data(),lock.size(),"%s/activity.lock",registry.data()); ::unlink(lock.data()); ::rmdir(registry.data()); ::rmdir(base.data());
    }
};
void all_denied() noexcept {
    for(const auto* action:{"mount","unmount","shutdown","reboot","write","unknown"})check(!ure::dualboot_quarantine_permits(action),"Unsafe marker permitted an action");
    for(const auto* action:{"mount","unmount","shutdown","reboot"}) {
        ure::LegacyLifecycleGuard guard(action); check(!guard.active(),"Unsafe marker permitted a managed lifecycle");
    }
    check(!ure::stage_legacy_reboot(),"Unsafe marker staged a reboot");
    auto writer=ure::runtime_lifecycle_acquire(false); check(writer.valid(),"Refused lifecycle leaked its runtime lease");
}
void wait_for(pid_t child) noexcept {
    check(child>=0,"Cannot fork policy contender"); int status=0;
    check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"Cross-process runtime exclusion failed");
}
}
int main() {
    Fixture fixture;
    check(ure::dualboot_quarantine_state()==ure::DualbootQuarantineState::Clear,"Missing final directory is not normal");
    for(const auto* action:{"mount","unmount","shutdown","reboot"}) { ure::LegacyLifecycleGuard guard(action); check(guard.active(),"Clear policy refused a lifecycle"); }
    { ure::LegacyLifecycleGuard invalid(nullptr); check(!invalid.active(),"Null action admitted"); }
    { ure::LegacyLifecycleGuard invalid("unknown"); check(!invalid.active(),"Unknown action admitted"); }
    {
        auto writer=ure::runtime_lifecycle_acquire(false); check(writer.valid(),"Cannot obtain writer lifecycle exclusion");
        ure::LegacyLifecycleGuard mount("mount"); check(!mount.active(),"Lifecycle ignored active writer before marker creation");
        check(!ure::stage_legacy_reboot(),"Active writer allowed staged reboot");
        const auto child=::fork(); if(child==0) { ure::LegacyLifecycleGuard reboot("reboot"); ::_exit(reboot.active() ? 1 : 0); } wait_for(child);
    }
    {
        ure::LegacyLifecycleGuard parent("unmount"); check(parent.active(),"Clear parent guard refused");
        ure::LegacyLifecycleGuard nested("mount",&parent); check(nested.active(),"Clear related guard refused");
        const auto child=::fork(); if(child==0) { ure::LegacyLifecycleGuard inherited("mount",&parent); ::_exit(inherited.active() ? 1 : 0); } wait_for(child);
        const auto contender=::fork(); if(contender==0) { auto writer=ure::runtime_lifecycle_acquire(false); ::_exit(writer.valid() ? 1 : 0); } wait_for(contender);
        fixture.publish(ure::DualbootQuarantineState::Pending);
        check(!parent.active() && !nested.active(),"An active guard failed to recheck new quarantine");
    }
    check(ure::dualboot_quarantine_state()==ure::DualbootQuarantineState::Pending,"Pending phase not classified"); all_denied(); fixture.clear();
    fixture.directory(); check(ure::dualboot_quarantine_state()==ure::DualbootQuarantineState::Invalid,"Directory without phase was accepted"); all_denied();
    for(const auto* content:{"","URE-DUALBOOT-QUARANTINE-V1\n","URE-DUALBOOT-QUARANTINE-V1\nCOMMITTED","URE-DUALBOOT-QUARANTINE-V1\nCOMMITTED\nextra", "URE-DUALBOOT-QUARANTINE-V1\nUNKNOWN\n","COMMITTED\n"}) {
        fixture.content(content,std::strlen(content)); check(ure::dualboot_quarantine_state()==ure::DualbootQuarantineState::Invalid,"Malformed phase was accepted"); all_denied();
    }
    std::array<char,128> large{}; large.fill('x'); fixture.content(large.data(),large.size()); all_denied();
    const char nul[]="URE-DUALBOOT-QUARANTINE-V1\nCOMMITTED\n\0"; fixture.content(nul,sizeof(nul)-1); all_denied();
    fixture.publish(ure::DualbootQuarantineState::Committed); check(::chmod(fixture.phase.data(),0644)==0,"Cannot change fixture file mode"); all_denied();
    check(::chmod(fixture.phase.data(),0600)==0 && ::link(fixture.phase.data(),fixture.hard.data())==0,"Cannot make linked marker"); all_denied(); check(::unlink(fixture.hard.data())==0,"Cannot remove linked marker");
    check(::unlink(fixture.phase.data())==0 && ::symlink("phase.missing",fixture.phase.data())==0,"Cannot make symlink marker"); all_denied(); check(::unlink(fixture.phase.data())==0,"Cannot clear symlink marker");
    check(::mkfifo(fixture.phase.data(),0600)==0,"Cannot make FIFO marker"); all_denied(); check(::unlink(fixture.phase.data())==0,"Cannot clear FIFO marker");
    check(::mkdir(fixture.phase.data(),0600)==0,"Cannot make directory marker"); all_denied(); check(::rmdir(fixture.phase.data())==0,"Cannot clear directory marker");
    fixture.publish(ure::DualbootQuarantineState::Committed); check(::chmod(fixture.quarantine.data(),0755)==0,"Cannot change fixture directory mode"); all_denied(); check(::chmod(fixture.quarantine.data(),0700)==0,"Cannot restore private mode");
    check(::rename(fixture.quarantine.data(),fixture.real.data())==0 && ::symlink(fixture.real.data(),fixture.quarantine.data())==0,"Cannot make symlink quarantine directory"); all_denied();
    check(::unlink(fixture.quarantine.data())==0 && ::rename(fixture.real.data(),fixture.quarantine.data())==0,"Cannot restore real fixture directory");
    for(const auto state:{ure::DualbootQuarantineState::Committed,ure::DualbootQuarantineState::GptRestored}) {
        fixture.publish(state); check(ure::dualboot_quarantine_state()==state,"Terminal marker not classified");
        for(const auto* action:{"mount","unmount","shutdown","write"})check(!ure::dualboot_quarantine_permits(action),"Terminal marker permitted a non-reboot action");
        {
            ure::LegacyLifecycleGuard reboot("reboot"); check(reboot.active(),"Terminal marker refused managed reboot");
            ure::LegacyLifecycleGuard nested("unmount",&reboot); check(!nested.active(),"Reboot token was used to bypass unmount quarantine");
        }
        check(ure::stage_legacy_reboot(),"Terminal marker could not stage reboot");
        { ure::LegacyLifecycleGuard reboot("reboot",nullptr,true); check(reboot.active(),"Terminal staged reboot handoff failed"); }
        auto writer=ure::runtime_lifecycle_acquire(false); check(writer.valid(),"Reboot handoff leaked runtime ownership");
    }
    fixture.clear(); check(ure::stage_legacy_reboot(),"Clear policy could not stage reboot");
    fixture.publish(ure::DualbootQuarantineState::Pending); check(!ure::stage_legacy_reboot(),"Staged reboot ignored later pending phase");
    { auto writer=ure::runtime_lifecycle_acquire(false); check(writer.valid(),"Invalidated staged reboot leaked ownership"); }
    fixture.clear(); check(ure::stage_legacy_reboot(),"Cannot stage reboot for adoption recheck");
    fixture.publish(ure::DualbootQuarantineState::Pending);
    { ure::LegacyLifecycleGuard reboot("reboot",nullptr,true); check(!reboot.active(),"Adopted staged reboot ignored later pending phase"); }
    { auto writer=ure::runtime_lifecycle_acquire(false); check(writer.valid(),"Denied staged adoption leaked ownership"); }
    fixture.clear(); std::array<char,256> invalid_path{};
    std::snprintf(invalid_path.data(),invalid_path.size(),"%s/missing-parent/quarantine",fixture.base.data());
    check(::setenv("URE_DUALBOOT_QUARANTINE",invalid_path.data(),1)==0,"Cannot select missing parent fixture"); all_denied();
    check(::setenv("URE_DUALBOOT_QUARANTINE","/tmp/../tmp/quarantine",1)==0,"Cannot select dot path fixture"); all_denied();
    check(::setenv("URE_DUALBOOT_QUARANTINE",fixture.quarantine.data(),1)==0,"Cannot restore valid fixture path");
    std::puts("PASS dependency-free Android quarantine: strict phases/private types, absent/malformed controls, active rechecks, runtime exclusion before marker creation, nested action limits, staged reboot adoption and cross-process denial; host files only.");
    return 0;
}
