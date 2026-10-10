// SPDX-License-Identifier: Apache-2.0
// Optional native test binary. Never installed in the recovery payload.
#include "libuke/uke.h"
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
namespace {
void check(bool value,const std::string& message) { ure::require(value,"vm-fixture-failed",message); }
template<class F> void reject(F function,const std::string& expected) {
    try { function(); } catch(const ure::Error& error) { check(error.code==expected,"Unexpected rejection: "+error.code+"; "+error.what()); return; }
    throw ure::Error("vm-fixture-failed","Missing rejection: "+expected);
}
void payload(const ure::Root& root,const std::string& path,const std::string& bytes) {
    auto fd=root.open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);
    check(::write(fd.get(),bytes.data(),bytes.size())==static_cast<ssize_t>(bytes.size()) && ::fsync(fd.get())==0,"Cannot sync fixture payload");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3 && std::string(argv[1])=="/mnt/btrfs" && std::string(argv[2])=="/run/ure-btrfs-vm","Use the isolated VM fixture mounts");
        const auto compatible=ure::bounded_read("/sys/firmware/devicetree/base/compatible",4096);
        check(compatible.find("linux,dummy-virt")!=std::string::npos && ure::bounded_read("/proc/cmdline").find("ure_fixture=1")!=std::string::npos,
            "This destructive disposable-image fixture may run only in its QEMU virt guest");
        struct utsname system{}; check(::uname(&system)==0,"Cannot identify guest kernel");
        ure::Root root(argv[1]); const ure::fs::path journals(argv[2]); ure::fs::create_directories(journals);
        ure::Value report; report["schema"]=1; report["validation_kind"]="qemu-system-native-ioctl"; report["physical_device"]=false;
        report["guest_kernel"]=system.release; report["checks"]=ure::Value(Json::arrayValue);
        auto passed=[&](const char* name) { report["checks"].append(name); std::cout<<"VM_CHECK "<<name<<std::endl; };
        unsigned serial=0;
        auto request=[](const char* action) { ure::Value value; value["schema"]=1; value["action"]=action; return value; };
        auto manage=[&](const ure::Value& value) {
            const auto plan=ure::btrfs_manage_plan(root,value,"generic-virt-fixture"); const auto store=journals/("manage-"+std::to_string(++serial));
            auto state=ure::btrfs_manage_execute(root,plan,store,plan["plan_sha256"].asString()); check(state["state"]=="COMPLETE","Native management job did not complete");
            check(ure::btrfs_manage_execute(root,plan,store,plan["plan_sha256"].asString())["state"]=="COMPLETE","Completed job could not be reopened"); return state;
        };
        check(ure::btrfs_native_info(root,"info")["subvolume"]["tree_id"].asUInt64()==5,"Fixture must mount the filesystem top-level root");
        auto create=request("create"); create["path"]="active"; manage(create); payload(root,"active/payload","original\n");
        passed("native-subvolume-create");
        create["path"]="rename-source"; manage(create); payload(root,"rename-source/payload","retained rename payload\n");
        auto rename=request("rename"); rename["path"]="rename-source"; rename["new_path"]="renamed-source";
        auto invalid=rename; invalid["new_path"]="../escaped";
        reject([&] { ure::btrfs_manage_plan(root,invalid,"generic-virt-fixture"); },"invalid-path");
        invalid["new_path"]="active";
        reject([&] { ure::btrfs_manage_plan(root,invalid,"generic-virt-fixture"); },"existing-subvolume");
        invalid["new_path"]="active/renamed-source";
        reject([&] { ure::btrfs_manage_plan(root,invalid,"generic-virt-fixture"); },"invalid-rename-path");
        passed("rename-confines-path-and-refuses-overwrite-or-move");
        {
            const auto plan=ure::btrfs_manage_plan(root,rename,"generic-virt-fixture");
            reject([&] { ure::btrfs_manage_execute(root,plan,journals/"rename-unconfirmed",std::string(64,'0')); },"confirmation-required");
            check(root.exists("rename-source") && !root.exists("renamed-source"),"Unconfirmed rename changed paths");
            payload(root,"rename-source/later","changed since review\n");
            auto source=root.open("rename-source",O_RDONLY|O_DIRECTORY); check(::syncfs(source.get())==0,"Cannot commit the stale source fixture");
            reject([&] { ure::btrfs_manage_execute(root,plan,journals/"rename-stale",plan["plan_sha256"].asString()); },"stale-btrfs-plan");
            check(root.exists("rename-source") && !root.exists("renamed-source"),"Stale rename changed paths");
        }
        passed("rename-requires-exact-consent-and-unchanged-source");
        const auto before_rename=ure::btrfs_subvolume_info(root,"rename-source"); manage(rename);
        const auto after_rename=ure::btrfs_subvolume_info(root,"renamed-source");
        check(!root.exists("rename-source") && root.read("renamed-source/payload")=="retained rename payload\n" &&
            before_rename["uuid"]==after_rename["uuid"] && before_rename["tree_id"]==after_rename["tree_id"] &&
            before_rename["inode"]==after_rename["inode"],"Rename lost contents or subvolume identity");
        passed("native-atomic-rename-preserves-uuid-inode-and-payload");
        auto snapshot=[&](const std::string& source,const std::string& name) {
            const auto store=journals/("snapshot-"+std::to_string(++serial));
            const auto plan=ure::btrfs_snapshot_plan(root,source,".",name,"generic-virt-fixture",store);
            const auto captured=ure::btrfs_snapshot_execute(root,store,plan["plan_sha256"].asString());
            check(captured["progress"]["state"]=="COMPLETE" && captured["progress"]["snapshot_identity"]["read_only"]==true,"Read-only snapshot publication failed");
            check(ure::btrfs_snapshot_execute(root,store,plan["plan_sha256"].asString())["progress"]["state"]=="COMPLETE","Published snapshot reopen failed");
        };
        snapshot("active","first"); check(root.read("first/payload")=="original\n","Snapshot payload differs"); passed("atomic-read-only-snapshot");
        auto send=[&](const std::string& source,const std::string& parent) {
            const auto store=journals/("send-"+std::to_string(++serial)); const auto plan=ure::btrfs_send_plan(root,source,parent,"generic-virt-fixture",store);
            check(ure::btrfs_send_capture(root,store,plan["plan_sha256"].asString())["progress"]["state"]=="COMPLETE","Native send failed");
            const auto verified=ure::btrfs_send_verify(store); check(verified["data_verified"]==true,"Offline stream verification failed"); return store;
        };
        const auto full=send("first",""); passed("full-send-crc32c-lineage-and-sha256");
        payload(root,"active/payload","modified\n"); snapshot("active","second"); send("second","first"); passed("incremental-send-and-parent-lineage");
        {
            ure::Root store(full); auto stream=store.open("stream.bin",O_RDWR); char byte=0;
            check(::pread(stream.get(),&byte,1,30)==1,"Cannot read corruption fixture byte"); const auto saved=byte; byte^=1;
            check(::pwrite(stream.get(),&byte,1,30)==1 && ::fsync(stream.get())==0,"Cannot sync corrupted fixture stream");
            reject([&] { ure::btrfs_stream_check(stream.get()); },"btrfs-stream-corrupt");
            check(::pwrite(stream.get(),&saved,1,30)==1 && ::fsync(stream.get())==0,"Cannot restore fixture stream byte");
        }
        passed("corrupt-stream-rejected");
        auto rollback=request("rollback"); rollback["path"]="active"; rollback["snapshot"]="first"; rollback["saved_path"]="retained-original";
        {
            int ready[2]{},finish[2]{}; check(::pipe(ready)==0 && ::pipe(finish)==0,"Cannot create busy-process fixture pipes");
            const auto child=::fork(); check(child>=0,"Cannot fork busy-process fixture");
            if(child==0) { ::close(ready[0]); ::close(finish[1]); auto selected=root.open("active",O_RDONLY|O_DIRECTORY);
                if(::fchdir(selected.get())!=0)::_exit(2);
                selected=ure::Fd(); const char byte=1; if(::write(ready[1],&byte,1)!=1)::_exit(3);
                char release=0; while(::read(finish[0],&release,1)<0 && errno==EINTR) {} ::_exit(0); }
            ::close(ready[1]); ::close(finish[0]); char byte=0; check(::read(ready[0],&byte,1)==1,"Busy fixture was not ready");
            try { reject([&] { ure::btrfs_manage_plan(root,rollback,"generic-virt-fixture"); },"busy-subvolume"); }
            catch(...) { ::kill(child,SIGKILL); ::waitpid(child,nullptr,0); throw; }
            check(::write(finish[1],&byte,1)==1,"Cannot release busy fixture"); int status=0; check(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"Busy fixture cleanup failed");
            ::close(ready[0]); ::close(finish[1]);
        }
        passed("process-working-directory-blocks-rollback");
        const auto rollback_plan=ure::btrfs_manage_plan(root,rollback,"generic-virt-fixture");
        reject([&] { ure::btrfs_manage_execute(root,rollback_plan,"/mnt/btrfs/active/unsafe-journal",rollback_plan["plan_sha256"].asString()); },"recursive-backup");
        check(!root.exists("active/unsafe-journal"),"Unsafe journal was created before rejection"); passed("journal-outside-affected-subvolumes");
        manage(rollback); check(root.read("active/payload")=="original\n" && root.read("retained-original/payload")=="modified\n","Rollback did not retain both data versions");
        passed("rollback-exchange-retains-original");
        auto clone=request("snapshot"); clone["source"]="active"; clone["path"]="temporary"; clone["read_only"]=false; manage(clone);
        auto flag=request("readonly"); flag["path"]="temporary"; flag["read_only"]=true; manage(flag); flag["read_only"]=false; manage(flag);
        passed("snapshot-and-read-only-flags");
        snapshot("temporary","deletion-backup"); auto remove=request("delete"); remove["path"]="temporary"; remove["backup_snapshot"]="deletion-backup"; manage(remove);
        check(!root.exists("temporary") && root.read("deletion-backup/payload")=="original\n","Deletion lost its backup"); passed("delete-retains-derived-snapshot");
        for(const auto* operation:{"usage","subvolumes","device-stats","scrub-status","balance-status"})ure::btrfs_native_info(root,operation);
        passed("native-inventory-allocation-and-status");
        auto scrub=request("scrub"); scrub["device_id"]=Json::UInt64(1); scrub["repair"]=false;
        reject([&] { ure::btrfs_manage_plan(root,scrub,"generic-virt-fixture"); },"read-only-mount-required"); scrub["repair"]=true;
        const auto scrubbed=manage(scrub); check(scrubbed["scrub_progress"]["uncorrectable_errors"].asUInt64()==0,"Disposable filesystem scrub found uncorrectable errors");
        passed("scrub-repair-and-verification-mount-policy");
        auto balanced=request("balance"); balanced["usage_percent"]=90; balanced["chunk_limit"]=1; manage(balanced); passed("bounded-filtered-balance");
        auto resize=request("resize"); resize["target_bytes"]=Json::UInt64(384*1024*1024); manage(resize);
        resize["target_bytes"]=Json::UInt64(512*1024*1024); manage(resize); passed("mounted-resize-shrink-and-grow");
        report["passed"]=true; std::cout<<"URE_BTRFS_VM_RESULT "<<ure::json(report)<<std::endl; return 0;
    } catch(const ure::Error& error) { std::cerr<<"URE_BTRFS_VM_ERROR "<<error.code<<": "<<error.what()<<std::endl; return 1; }
    catch(const std::exception& error) { std::cerr<<"URE_BTRFS_VM_ERROR "<<error.what()<<std::endl; return 1; }
}
