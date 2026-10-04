// SPDX-License-Identifier: Apache-2.0
#include "lifecycle_hooks.hpp"
#include "operation_lease.hpp"
#include <array>
#include <csignal>
#include <iostream>
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
        std::cout<<"PASS complete production lifecycle callbacks: pre-effect refusal, retained exact owner, explicit nested unmount, staged GUI reboot handoff, direct/fastbootd transitions and compile-time Android admission boundary; all mount/reboot effects mocked\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
