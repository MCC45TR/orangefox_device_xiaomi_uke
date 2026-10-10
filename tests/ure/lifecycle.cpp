// SPDX-License-Identifier: Apache-2.0
#include "lifecycle_hooks.hpp"
#include "operation_lease.hpp"
#include <algorithm>
#include <array>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>

namespace {
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> void reject(F function,const std::string& code) {
    try { function(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected refusal: "+error.code+", expected "+code); return; }
    throw std::runtime_error("Missing refusal: "+code);
}
[[maybe_unused]] void child_check(const std::function<void()>& action) {
    const auto pid=::fork(); check(pid>=0,"Cannot fork lifecycle contender");
    if(pid==0) { try { action(); ::_exit(0); } catch(...) { ::_exit(1); } }
    int status=0; check(::waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0,"Independent lifecycle contender failed");
}
[[maybe_unused]] ure::Value verified() { ure::Value value; value["state"]="COMPLETE"; value["verified"]=true; value["cleanup_complete"]=true; return value; }
[[maybe_unused]] void blocked() {
    LifecycleProbe::reset();
    TWPartition data; PartitionManager.Partitions={&data}; GUIAction gui; FastbootDevice fastboot;
    check(!data.UnMount(false),"Active owner permitted partition unmount");
    check(!PartitionManager.UnMount_By_Path("/data",false),"Active owner permitted manager unmount");
    check(ensure_path_unmounted("/data")==-1,"Active owner permitted fs_mgr unmount");
    check(gui.reboot("system")==1,"Active owner permitted GUI reboot scheduling");
    check(TWFunc::tw_reboot(rb_system)==-1,"Active owner permitted direct reboot");
    for(const auto handler:std::array{ShutDownHandler,RebootHandler,RebootBootloaderHandler,RebootFastbootHandler,RebootRecoveryHandler})
        check(!handler(&fastboot,{}),"Active owner permitted fastbootd lifecycle");
    check(LifecycleProbe::effects.empty() && LifecycleProbe::unmounts.empty(),"Refused lifecycle produced a side effect");
    PartitionManager.Partitions.clear();
}
void permitted(const ure::OperationBinding& binding) {
    LifecycleProbe::reset(); TWPartition data,child; child.Mount_Point="/data-child"; child.Is_SubPartition=true; child.SubPartition_Of="/data";
    data.Is_Storage=true; data.MTP_Storage_ID=1; PartitionManager.Partitions={&data,&child};
#ifndef __ANDROID__
    LifecycleProbe::during_effect=[&]{reject([&]{ure::OperationLease::acquire(binding);},"operation-busy");};
#else
    (void)binding;
#endif
    check(PartitionManager.UnMount_By_Path("/data",false),"Explicit manager token did not cover nested unmount");
    check(LifecycleProbe::unmounts.size()==2,"Related partition unmount was skipped");
    LifecycleProbe::reset(); check(ensure_path_unmounted("/data")==0,"Idle fs_mgr unmount refused");
    LifecycleProbe::reset(); GUIAction gui; check(gui.reboot("system")==0,"Idle GUI reboot refused");
#ifndef __ANDROID__
    check(ure::operation_lease_status()["active_exclusion"]==true,"GUI reboot did not retain exclusion across handoff");
    child_check([&]{reject([&]{ure::OperationLease::acquire(binding);},"operation-busy");});
    check(!data.UnMount(false),"Staged GUI reboot allowed unrelated unmount");
    LifecycleProbe::during_effect=[&]{reject([&]{ure::OperationLease::acquire(binding);},"operation-busy");};
#endif
    check(TWFunc::tw_reboot(rb_system)==0,"Direct reboot failed to adopt the queued GUI token");
    check(!LifecycleProbe::effects.empty(),"Permitted reboot produced no mocked effect");
    PartitionManager.Partitions.clear(); LifecycleProbe::reset(); FastbootDevice fastboot;
    for(const auto handler:std::array{ShutDownHandler,RebootHandler,RebootBootloaderHandler,RebootFastbootHandler,RebootRecoveryHandler})
        check(handler(&fastboot,{}),"Idle fastbootd lifecycle refused");
}
void system_boot_callbacks() {
    for(const auto command:{rb_current,rb_system}) {
        LifecycleProbe::reset();
        check(TWFunc::tw_reboot(command)==0,"Prepared system reboot refused");
        const auto& effects=LifecycleProbe::effects;
        const auto prepared=std::find(effects.begin(),effects.end(),"system-boot.prepare");
        const auto reboot=std::find(effects.begin(),effects.end(),"property.callback:sys.powerctl=reboot,");
        check(prepared!=effects.end() && reboot!=effects.end() && prepared<reboot &&
            std::count(effects.begin(),effects.end(),"system-boot.prepare")==1,"System reboot did not prepare its selector exactly once before reboot");
        LifecycleProbe::reset(); LifecycleProbe::system_boot_error="system-boot-command-unrecognized";
        check(TWFunc::tw_reboot(command)==-1,"Selector refusal permitted system reboot");
        check(std::none_of(effects.begin(),effects.end(),[](const auto& effect) {
            return effect.starts_with("property.callback:") || effect=="android-reboot.callback" || effect=="reboot.syscall.callback";
        }),"Selector refusal requested a reboot");
        check(std::count(effects.begin(),effects.end(),"system-boot.prepare")==1 &&
            LifecycleProbe::errors==std::vector<std::string>{"System reboot refused: system-boot-command-unrecognized\n"},"Selector refusal was not reported");
    }
    LifecycleProbe::reset(); FastbootDevice fastboot;
    check(RebootHandler(&fastboot,{}),"Prepared fastbootd system reboot refused");
    const std::vector<std::string> expected{"system-boot.prepare","fastboot.status.callback",
        "property.callback:sys.powerctl=reboot,from_fastboot","fastboot.close.callback","pause.callback"};
    check(LifecycleProbe::effects==expected,"Fastbootd acknowledged or requested reboot before selector preparation");
    LifecycleProbe::reset(); LifecycleProbe::system_boot_error="system-boot-command-unrecognized";
    check(!RebootHandler(&fastboot,{}),"Selector refusal permitted fastbootd system reboot");
    check(LifecycleProbe::effects==std::vector<std::string>{"system-boot.prepare"} && LifecycleProbe::status.empty() &&
        LifecycleProbe::failures==std::vector<std::string>{"system-boot-command-unrecognized"},"Fastbootd selector refusal acknowledged or requested reboot");
    LifecycleProbe::reset();
}
#ifdef __ANDROID__
#ifndef URE_HOST_POLICY_FIXTURE
#error Android lifecycle callback tests require an explicit host policy fixture.
#endif
struct QuarantineFixture {
    std::string base,quarantine,phase,previous;
    bool had_previous=false;
    QuarantineFixture() {
        std::array<char,80> name{}; std::strcpy(name.data(),"/tmp/ure-lifecycle-quarantine-XXXXXX");
        check(::mkdtemp(name.data())!=nullptr,"Cannot create private lifecycle quarantine fixture");
        base=name.data(); quarantine=base+"/quarantine"; phase=quarantine+"/phase";
        if(const auto* selected=::getenv("URE_DUALBOOT_QUARANTINE")) { previous=selected; had_previous=true; }
        check(::setenv("URE_DUALBOOT_QUARANTINE",quarantine.c_str(),1)==0,"Cannot select lifecycle quarantine fixture");
    }
    void directory() const { check(::mkdir(quarantine.c_str(),0700)==0 || errno==EEXIST,"Cannot create private quarantine directory"); }
    void publish(const char* bytes) const {
        directory(); const int fd=::open(phase.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
        check(fd>=0,"Cannot create lifecycle quarantine phase");
        const auto length=std::strlen(bytes); std::size_t done=0;
        while(done<length) {
            const auto count=::write(fd,bytes+done,length-done);
            if(count<0 && errno==EINTR)continue;
            check(count>0,"Cannot write lifecycle quarantine phase"); done+=static_cast<std::size_t>(count);
        }
        check(::fsync(fd)==0 && ::close(fd)==0,"Cannot synchronize lifecycle quarantine phase");
    }
    void publish(ure::DualbootQuarantineState state) const {
        const auto* marker=ure::dualboot_quarantine_marker(state); check(marker!=nullptr,"Invalid lifecycle quarantine state"); publish(marker);
    }
    void clear() const {
        check(::unlink(phase.c_str())==0 || errno==ENOENT,"Cannot remove lifecycle quarantine phase");
        check(::rmdir(quarantine.c_str())==0 || errno==ENOENT,"Cannot remove lifecycle quarantine directory");
    }
    ~QuarantineFixture() {
        ::unlink(phase.c_str()); ::rmdir(quarantine.c_str()); ::rmdir(base.c_str());
        if(had_previous)::setenv("URE_DUALBOOT_QUARANTINE",previous.c_str(),1); else ::unsetenv("URE_DUALBOOT_QUARANTINE");
    }
};
void no_callback_effects(const char* context) {
    // TWPartition::UnMount first probes its mounted state. That read-only
    // observation is not an effect; the guard must precede every callback.
    check(LifecycleProbe::effects.empty() && LifecycleProbe::unmounts.empty() &&
        LifecycleProbe::data_reads==0,context);
}
void quarantine_denied(bool terminal) {
    LifecycleProbe::reset(); TWPartition data; PartitionManager.Partitions={&data}; GUIAction gui; FastbootDevice fastboot;
    check(!data.UnMount(false),"Quarantine permitted direct partition unmount");
    check(!PartitionManager.UnMount_By_Path("/data",false),"Quarantine permitted manager unmount");
    check(ensure_path_unmounted("/data")==-1,"Quarantine permitted fs_mgr unmount");
    for(const auto* command:{"system","bootloader","poweroff","download","edl","fastboot","unknown"})
        check(gui.reboot(command)==1,"Quarantine scheduled a non-recovery GUI reboot");
    for(const auto command:std::array{rb_current,rb_system,rb_poweroff,rb_bootloader,rb_download,rb_edl,rb_fastboot})
        check(TWFunc::tw_reboot(command)==-1,"Quarantine permitted a non-recovery direct reboot");
    for(const auto handler:std::array{ShutDownHandler,RebootHandler,RebootBootloaderHandler,RebootFastbootHandler})
        check(!handler(&fastboot,{}),"Quarantine permitted a non-recovery fastbootd transition");
    if(!terminal) {
        check(gui.reboot("recovery")==1,"Unsafe quarantine scheduled recovery reboot");
        check(TWFunc::tw_reboot(rb_recovery)==-1,"Unsafe quarantine permitted direct recovery reboot");
        check(!RebootRecoveryHandler(&fastboot,{}),"Unsafe quarantine permitted fastbootd recovery reboot");
    }
    no_callback_effects("Quarantine refusal reached a settings, script, property, mount or reboot callback");
    PartitionManager.Partitions.clear();
}
void quarantine_callbacks() {
    QuarantineFixture fixture;
    fixture.publish(ure::DualbootQuarantineState::Pending); quarantine_denied(false);
    fixture.publish("URE-DUALBOOT-QUARANTINE-V1\nUNKNOWN\n"); quarantine_denied(false);
    fixture.clear(); fixture.directory(); quarantine_denied(false); fixture.clear();
    for(const auto state:std::array{ure::DualbootQuarantineState::Committed,ure::DualbootQuarantineState::GptRestored}) {
        fixture.publish(state); quarantine_denied(true);
        LifecycleProbe::reset(); GUIAction gui;
        check(gui.reboot("recovery")==0,"Terminal quarantine refused GUI recovery handoff");
        const std::vector<std::string> gui_effects{"sync.callback","data.set:tw_gui_done","data.set:tw_reboot_arg"};
        check(LifecycleProbe::effects==gui_effects && LifecycleProbe::values["tw_reboot_arg"]=="recovery" &&
            LifecycleProbe::values["tw_gui_done"]=="1" && LifecycleProbe::unmounts.empty(),"GUI recovery handoff produced unexpected effects");
        LifecycleProbe::reset();
        check(TWFunc::tw_reboot(rb_recovery)==0,"Terminal quarantine refused queued recovery reboot");
        const std::vector<std::string> recovery_effects{"sync.callback","property.callback:sys.powerctl=reboot,recovery"};
        check(LifecycleProbe::effects==recovery_effects && LifecycleProbe::unmounts.empty() &&
            LifecycleProbe::probes==0 && LifecycleProbe::data_reads==0,"Queued recovery reboot reached old settings, log, script or mount callbacks");
        LifecycleProbe::reset();
        check(TWFunc::tw_reboot(rb_recovery)==0 && LifecycleProbe::effects==recovery_effects,
            "Direct recovery reboot failed its minimal terminal-quarantine path");
        LifecycleProbe::reset(); FastbootDevice fastboot;
        check(RebootRecoveryHandler(&fastboot,{}),"Terminal quarantine refused fastbootd recovery reboot");
        const std::vector<std::string> fastboot_effects{"fastboot.status.callback","property.callback:sys.powerctl=reboot,recovery","fastboot.close.callback","pause.callback"};
        check(LifecycleProbe::effects==fastboot_effects && LifecycleProbe::unmounts.empty() &&
            LifecycleProbe::probes==0 && LifecycleProbe::data_reads==0,"Fastbootd recovery reboot produced unexpected effects");
    }
    fixture.publish(ure::DualbootQuarantineState::Committed); LifecycleProbe::reset(); GUIAction gui;
    check(gui.reboot("recovery")==0,"Cannot stage terminal recovery handoff recheck");
    fixture.publish(ure::DualbootQuarantineState::Pending); LifecycleProbe::reset();
    check(TWFunc::tw_reboot(rb_recovery)==-1,"Queued recovery reboot ignored a later pending marker");
    no_callback_effects("Invalidated recovery handoff reached an effect callback");
    fixture.clear(); auto writer=ure::runtime_lifecycle_acquire(false);
    check(writer.valid(),"Quarantine callback refusal or reboot handoff leaked runtime ownership");
}
#endif
}
void gui_err(const char* message) { LifecycleProbe::errors.emplace_back(message); }
int main() {
    try {
        ure::Value target; target["kind"]="mock-file"; target["role"]="lifecycle-oracle";
        ure::Value targets(Json::arrayValue); targets.append(target); ure::Value journal; journal["fixture"]=true;
        const ure::OperationBinding binding{"fixture.lifecycle","lifecycle-test",ure::sha256("exact-lifecycle-plan"),targets,journal};
        {
            auto gui_job=ure::RuntimeActivityLease::acquire("abcdef0123456789abcdef0123456789");
            check(gui_job.valid(),"Cannot register global GUI lifetime fixture"); blocked();
        }
        check(ure::runtime_active_jobs()==0,"Global GUI lifetime fixture was not retired");
#ifdef __ANDROID__
        // Host execution of Android's compile-time policy, with no real device
        // mount/reboot syscall. Environment settings cannot open management.
        static_assert(!ure::device_operation_coordinator_accepted());
        reject([&]{ure::configure_operation_coordinator("/tmp/unsafe-domain");},"ownership-unavailable");
        reject([&]{ure::OperationLease::acquire(binding);},"ownership-unavailable");
        check(ure::operation_lease_status()["available"]==false,"Android policy exposed a host coordinator");
        permitted(binding);
        quarantine_callbacks();
#else
        {
            auto operation=ure::OperationLease::acquire(binding); blocked(); operation.checkpoint("fixture-running"); blocked();
        }
        blocked();
        auto different=binding; different.plan_sha256=ure::sha256("wrong-plan");
        reject([&]{ure::OperationLease::acquire(different,ure::LeaseAdmission::RecoverSameOperation);},"operation-owner-mismatch");
        {
            auto operation=ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation); operation.release_verified(verified());
        }
        permitted(binding);
        check(ure::operation_lease_status()["state"]=="IDLE","Lifecycle fixture leaked a token");
        {
            ure::LegacyLifecycleGuard parent("unmount"); check(parent.active(),"Idle explicit lifecycle token refused");
            ure::LegacyLifecycleGuard nested("unmount",&parent); check(nested.active(),"Explicit nested lifecycle token refused");
            ure::LegacyLifecycleGuard unrelated("unmount"); check(!unrelated.active(),"Lifecycle token was borrowed implicitly");
            child_check([&]{ure::LegacyLifecycleGuard inherited("unmount",&parent); check(!inherited.active(),"Child borrowed an inherited parent token");});
        }
#endif
        system_boot_callbacks();
        std::cout<<"PASS complete production lifecycle callbacks: pre-effect refusal, retained exact owner, explicit nested unmount, staged GUI reboot handoff, direct/fastbootd transitions and compile-time Android admission boundary";
#ifdef __ANDROID__
        std::cout<<", pending/malformed quarantine refusal and recovery-only terminal callbacks";
#endif
        std::cout<<"; all mount/reboot effects mocked\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
