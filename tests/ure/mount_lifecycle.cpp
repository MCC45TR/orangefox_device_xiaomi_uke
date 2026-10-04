// SPDX-License-Identifier: Apache-2.0
// Complete production mount callbacks, with every mount effect mocked.
#include "write-gate-hooks.h"
#include "operation_lease.hpp"
#include <iostream>

void gui_err(const char* message) { GateProbe::errors.emplace_back(message); }
namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
template<class F> void reject(F function,const char* expected) {
    try { function(); } catch(const ure::Error& error) { check(error.code==expected,"Unexpected ownership refusal"); return; }
    throw std::runtime_error("Expected ownership refusal");
}
void blocked() {
    GateProbe::reset(); TWPartition partition;
    check(!partition.Mount(true),"Active or retained owner permitted a new mount");
    check(GateProbe::mounts.empty() && GateProbe::commands.empty() && GateProbe::side_effects==0 && GateProbe::namespace_effects==0,
          "Refused mount produced an effect");
    partition.Symlink_Path=GateProbe::fixture; partition.Symlink_Mount_Point="mock-bind";
    check(!partition.Bind_Mount(true),"Active or retained owner permitted a direct bind mount");
    check(GateProbe::mounts.empty() && GateProbe::commands.empty() && GateProbe::side_effects==0,"Refused bind mount produced an effect");
}
}
int main() {
    try {
        ure::Value target; target["kind"]="mount-callback-oracle";
        ure::Value targets(Json::arrayValue); targets.append(target); ure::Value journal; journal["fixture"]=true;
        const ure::OperationBinding binding{"fixture.mount","mount-lifetime",ure::sha256("exact-mount-lifetime-plan"),targets,journal};
        { auto owner=ure::OperationLease::acquire(binding); blocked(); owner.checkpoint("MOUNT_CONTENDER"); blocked(); }
        blocked();
        { auto owner=ure::OperationLease::acquire(binding,ure::LeaseAdmission::RecoverSameOperation);
          ure::Value terminal; terminal["state"]="CANCELLED_SAFE"; terminal["verified"]=true; terminal["cleanup_complete"]=true;
          owner.release_verified(terminal); }
        for(const std::string filesystem:{"ext4","f2fs","ntfs","exfat"}) {
            GateProbe::reset(); TWPartition partition; partition.Current_File_System=partition.Fstab_File_System=filesystem;
            GateProbe::during_mount=[&]{reject([&]{ure::OperationLease::acquire(binding);},"operation-busy");};
            if(filesystem=="exfat")GateProbe::mount_results={-1,-1,0};
            check(partition.Mount(true),"Idle readonly mount or nested exFAT unmount refused");
            check(GateProbe::side_effects==0,"Readonly mount invoked a writable effect");
        }
        GateProbe::reset(); TWPartition partition; partition.Symlink_Mount_Point="mock-bind"; partition.Symlink_Path=GateProbe::fixture;
        GateProbe::during_mount=[&]{reject([&]{ure::OperationLease::acquire(binding);},"operation-busy");};
        check(partition.Mount(true),"Explicit mount token did not cover its nested bind mount");
        check(GateProbe::mounts.size()==2 && (GateProbe::mounts.back().flags&MS_BIND),"Nested production bind mount was skipped");
        GateProbe::reset(); GateProbe::mounted_read_only=false;
        check(!partition.Bind_Mount(true) && GateProbe::mounts.empty(),"Direct bind mount exposed a writable source");
        check(ure::operation_lease_status()["state"]=="IDLE","Mount callbacks leaked lifecycle exclusion");
        std::cout<<"PASS production mount lifetime: active/retained owner refusals before effects, ext4/F2FS and helper mounts, explicit nested bind/exFAT cleanup, cross-job exclusion during callbacks, and writable bind-source refusal; no real mounts\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
