// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "operation_guard.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/btrfs.h>
#include <linux/btrfs_tree.h>
#include <linux/magic.h>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace ure {
namespace {
std::string hex(const unsigned char* bytes,std::size_t count=16) { static const char digits[]="0123456789abcdef"; std::string out;
    for(std::size_t i=0;i<count;++i) { out+=digits[bytes[i]>>4]; out+=digits[bytes[i]&15]; } return out; }
Value filesystem(int fd) {
    filesystem_tree_gate(fd); struct statfs st{}; struct btrfs_ioctl_fs_info_args info{};
    require(::fstatfs(fd,&st)==0 && st.f_type==BTRFS_SUPER_MAGIC,"not-btrfs","Select a mounted Btrfs filesystem with a matching recovery kernel");
    require(::ioctl(fd,BTRFS_IOC_FS_INFO,&info)==0,"btrfs-info-unavailable","Cannot inspect native Btrfs identity");
    Value out=descriptor_identity(fd); out["fsid"]=hex(info.fsid); out["devices"]=Json::UInt64(info.num_devices); out["max_device_id"]=Json::UInt64(info.max_id); return out;
}
Value progress(const struct btrfs_scrub_progress& data) {
    Value out;
#define URE_SCRUB(field) out[#field]=Json::UInt64(data.field)
    URE_SCRUB(data_extents_scrubbed); URE_SCRUB(tree_extents_scrubbed); URE_SCRUB(data_bytes_scrubbed); URE_SCRUB(tree_bytes_scrubbed);
    URE_SCRUB(read_errors); URE_SCRUB(csum_errors); URE_SCRUB(verify_errors); URE_SCRUB(no_csum); URE_SCRUB(csum_discards);
    URE_SCRUB(super_errors); URE_SCRUB(malloc_errors); URE_SCRUB(uncorrectable_errors); URE_SCRUB(unverified_errors); URE_SCRUB(corrected_errors);
#undef URE_SCRUB
    return out;
}
Value balance(int fd) {
    struct btrfs_ioctl_balance_args data{}; Value out;
    if(::ioctl(fd,BTRFS_IOC_BALANCE_PROGRESS,&data)==0) { out["running"]=true; out["state"]=Json::UInt64(data.state);
        out["expected_chunks"]=Json::UInt64(data.stat.expected); out["considered_chunks"]=Json::UInt64(data.stat.considered); out["completed_chunks"]=Json::UInt64(data.stat.completed); }
    else { require(errno==ENOTCONN,"balance-status-unavailable","Cannot inspect balance progress"); out["running"]=false; }
    return out;
}
std::string seal(Value value) { value.removeMember("plan_sha256"); return sha256(json(value)); }
Value volume(const Root& root,const std::string& path) { return btrfs_subvolume_info(root,path); }
void writable(int fd) { struct statvfs st{}; require(::fstatvfs(fd,&st)==0 && !(st.f_flag&ST_RDONLY),"read-only-btrfs","This operation requires a writable Btrfs mount"); }
void missing(const Root& root,const std::string& path) { require(!root.exists(path),"existing-subvolume","Destination already exists; it will not be overwritten"); }
std::pair<std::string,std::string> split(const std::string& path) {
    const auto parts=components(path); require(!parts.empty(),"invalid-subvolume-path","A child subvolume path is required");
    return {fs::path(path).parent_path().empty() ? "." : fs::path(path).parent_path().generic_string(),parts.back()};
}
Value record(const Root& store,const std::string& name) { const auto st=store.stat(name);
    require(S_ISREG(st.st_mode) && st.st_uid==::geteuid() && st.st_nlink==1 && (st.st_mode&07777)==0600,"unsafe-btrfs-journal","Btrfs journal records must be private regular files"); return parse_json(store.read(name)); }
void validate(const Value& plan) { require(plan["schema"]==1 && plan["operation"]=="btrfs.manage" && identifier(plan["operation_id"].asString()) &&
    hash_valid(plan["plan_sha256"].asString()) && seal(plan)==plan["plan_sha256"].asString(),"invalid-btrfs-plan","Invalid sealed native Btrfs management plan"); }
void save(const Root& store,Value& state,const std::string& phase) { state["state"]=phase; state["updated_at"]=utc(); store.save_record("state.json",state,true); }
void identity(const Root& root,const std::string& path,const Value& expected,bool immutable=true) {
    const auto current=volume(root,path);
    if(immutable)require(json(current)==json(expected),"stale-subvolume","Subvolume identity, generation or flags changed since review");
    else for(const auto* key:{"fsid","uuid","tree_id","read_only","received_uuid"})require(json(current[key])==json(expected[key]),"stale-subvolume","Selected subvolume changed");
}
void renamed_identity(const Root& root,const std::string& path,Value expected) {
    const auto name=split(path).second; expected["name_hex"]=hex(reinterpret_cast<const unsigned char*>(name.data()),name.size());
    identity(root,path,expected);
}
void create_snapshot(const Root& root,const std::string& source,const std::string& destination,bool read_only) {
    const auto [parent,name]=split(destination); auto src=root.open(source,O_RDONLY|O_DIRECTORY),dst=root.open(parent,O_RDONLY|O_DIRECTORY);
    filesystem_tree_outside(src.get(),dst.get()); missing(root,destination); struct btrfs_ioctl_vol_args_v2 args{}; args.fd=src.get();
    args.flags=read_only ? BTRFS_SUBVOL_RDONLY : 0; std::memcpy(args.name,name.c_str(),name.size()+1);
    require(::ioctl(dst.get(),BTRFS_IOC_SNAP_CREATE_V2,&args)==0 && ::fsync(dst.get())==0,"snapshot-failed","Cannot create and sync the selected snapshot");
}
void exclude_mounts_and_users(int fd) {
    struct stat selected{}; require(::fstat(fd,&selected)==0,"io-error","Cannot inspect selected subvolume"); Root system("/");
    auto proc=system.open("proc",O_RDONLY|O_DIRECTORY); struct statfs st{};
    require(::fstatfs(proc.get(),&st)==0 && st.f_type==PROC_SUPER_MAGIC,"ownership-unavailable","Subvolume replacement needs real process/mount observations");
    auto numeric=[](const std::string& text) { return !text.empty() && std::all_of(text.begin(),text.end(),[](char c){return c>='0' && c<='9';}); };
    for(const auto& pid:system.list("proc",16384))if(numeric(pid)) {
        try {
            std::istringstream mounts(system.read("proc/"+pid+"/mountinfo",4*1024*1024)); std::string line;
            const auto number=std::to_string(major(selected.st_dev))+":"+std::to_string(minor(selected.st_dev));
            while(std::getline(mounts,line)) { std::istringstream row(line); std::string id,parent,device; row>>id>>parent>>device;
                require(device!=number,"mounted-subvolume","Active or snapshot subvolume is mounted in a visible namespace"); }
            for(const auto* anchor:{"cwd","root"}) {
                struct stat used{}; const auto path=pid+"/"+anchor;
                if(::fstatat(proc.get(),path.c_str(),&used,0)<0) {
                    if(errno==ENOENT)continue; // Exited task or a kernel thread without a userspace anchor.
                    throw Error("ownership-unavailable","A process working directory or root cannot be inspected");
                }
                require(used.st_dev!=selected.st_dev,"busy-subvolume","A visible process has its working directory or root inside the selected subvolume");
            }
            Root files(system.open("proc/"+pid+"/fd",O_RDONLY|O_DIRECTORY));
            for(const auto& name:files.list(".",4096)) { struct stat opened{};
                if(::fstatat(files.fd(),name.c_str(),&opened,0)<0) { if(errno==ENOENT)continue; throw Error("ownership-unavailable","A process descriptor cannot be inspected"); }
                if(pid==std::to_string(::getpid()) && opened.st_dev==selected.st_dev && opened.st_ino==selected.st_ino)continue;
                require(opened.st_dev!=selected.st_dev,"busy-subvolume","A visible process still owns the selected subvolume"); }
        } catch(const Error& error) { struct stat item{}; if(::fstatat(proc.get(),pid.c_str(),&item,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT)continue; throw; }
    }
}
bool maintenance_stopped(const Root& root,const Value& plan) {
    const auto action=plan["request"]["action"].asString();
    if(action=="balance")return balance(root.fd())["running"]==false;
    require(action=="scrub","invalid-maintenance-plan","Maintenance closure requires the exact scrub or balance plan");
    struct btrfs_ioctl_scrub_args args{}; args.devid=plan["request"]["device_id"].asUInt64();
    if(::ioctl(root.fd(),BTRFS_IOC_SCRUB_PROGRESS,&args)==0)return false;
    require(errno==ENOTCONN,"scrub-status-unavailable","Cannot independently verify scrub worker closure"); return true;
}
Value checked_state(const Root& store,const Value& plan) {
    const auto state=record(store,"state.json");
    require(state["schema"]==1 && state["plan_sha256"]==plan["plan_sha256"] && state["state"].isString(),
        "invalid-btrfs-journal","Management state is not bound to its persisted sealed plan"); return state;
}
void verify_complete(const Root& root,const Value& plan,const Value& state) {
    require(json(filesystem(root.fd()))==json(plan["context"]),"stale-btrfs-filesystem","Filesystem identity changed before terminal verification");
    const auto& request=plan["request"]; const auto action=request["action"].asString();
    if(action=="create" || action=="snapshot") {
        const auto created=volume(root,request["path"].asString());
        require(json(created)==json(state["created_identity"]) && created["fsid"]==plan["context"]["fsid"] && created["tree_id"].asUInt64()>5,
            "btrfs-verification-failed","Created subvolume no longer matches its recorded identity");
        if(action=="snapshot")require(created["read_only"]==request["read_only"] && created["parent_uuid"]==plan["source_identity"]["uuid"],
            "btrfs-verification-failed","Snapshot flags or source lineage differ from the reviewed request");
    } else if(action=="rollback") {
        identity(root,request["path"].asString(),state["active_identity"]);
        renamed_identity(root,request["saved_path"].asString(),plan["active_identity"]);
        identity(root,request["snapshot"].asString(),plan["snapshot_identity"]);
    } else if(action=="readonly") {
        const auto current=volume(root,request["path"].asString());
        require(json(current)==json(state["updated_identity"]) && current["read_only"]==request["read_only"],
            "btrfs-verification-failed","Subvolume flags do not match the reviewed choice");
        for(const auto* key:{"fsid","uuid","tree_id","received_uuid"})require(current[key]==plan["source_identity"][key],
            "btrfs-verification-failed","Selected subvolume identity changed while setting flags");
    } else if(action=="delete") {
        require(!root.exists(request["path"].asString()),"btrfs-verification-failed","Deleted subvolume still exists");
        identity(root,request["backup_snapshot"].asString(),plan["backup_identity"]);
    } else if(action=="resize") {
        struct btrfs_ioctl_dev_info_args device{}; device.devid=1;
        require(::ioctl(root.fd(),BTRFS_IOC_DEV_INFO,&device)==0 && device.total_bytes==request["target_bytes"].asUInt64(),
            "btrfs-verification-failed","Filesystem size readback differs from the reviewed target");
    } else require(maintenance_stopped(root,plan),"maintenance-still-active","Kernel maintenance is still active; operation ownership remains retained");
}
} // namespace
Value btrfs_native_info(const Root& root,const std::string& operation) {
    auto out=filesystem(root.fd()); out["native_ioctl"]=true; out["read_only_operation"]=true; out["physical_test_record"]=false;
    if(operation=="info") { out["subvolume"]=volume(root,"."); return out; }
    if(operation=="balance-status") { out["balance"]=balance(root.fd()); return out; }
    if(operation=="usage") {
        struct btrfs_ioctl_space_args count;
        std::memset(&count,0,sizeof(count)); // UAPI includes a zero-length trailing array.
        require(::ioctl(root.fd(),BTRFS_IOC_SPACE_INFO,&count)==0 && count.total_spaces<=128,"btrfs-info-unavailable","Cannot inspect allocation groups");
        std::vector<unsigned char> bytes(sizeof(count)+static_cast<std::size_t>(count.total_spaces)*sizeof(struct btrfs_ioctl_space_info),0);
        auto* data=reinterpret_cast<struct btrfs_ioctl_space_args*>(bytes.data()); data->space_slots=count.total_spaces;
        require(::ioctl(root.fd(),BTRFS_IOC_SPACE_INFO,data)==0 && data->total_spaces<=count.total_spaces,"stale-btrfs-usage","Allocation group count changed during inspection");
        out["allocation_groups"]=Value(Json::arrayValue);
        for(std::uint64_t i=0;i<data->total_spaces;++i) { Value item; item["flags"]=Json::UInt64(data->spaces[i].flags); item["total_bytes"]=Json::UInt64(data->spaces[i].total_bytes);
            item["used_bytes"]=Json::UInt64(data->spaces[i].used_bytes); out["allocation_groups"].append(item); } return out;
    }
    if(operation=="device-stats" || operation=="scrub-status") {
        require(out["max_device_id"].asUInt64()<=4096,"size-limit","Btrfs device scan exceeds its bound"); out["device_stats"]=Value(Json::arrayValue);
        for(std::uint64_t id=1;id<=out["max_device_id"].asUInt64();++id) {
            struct btrfs_ioctl_dev_info_args device{}; device.devid=id;
            if(::ioctl(root.fd(),BTRFS_IOC_DEV_INFO,&device)<0) { if(errno==ENODEV)continue; throw Error("btrfs-info-unavailable","Cannot inspect Btrfs device"); }
            Value item; item["device_id"]=Json::UInt64(id); item["total_bytes"]=Json::UInt64(device.total_bytes); item["used_bytes"]=Json::UInt64(device.bytes_used);
            if(operation=="device-stats") { struct btrfs_ioctl_get_dev_stats stats{}; stats.devid=id; stats.nr_items=BTRFS_DEV_STAT_VALUES_MAX;
                require(::ioctl(root.fd(),BTRFS_IOC_GET_DEV_STATS,&stats)==0,"btrfs-info-unavailable","Cannot inspect Btrfs error counters"); item["counters"]=Value(Json::arrayValue);
                for(unsigned i=0;i<stats.nr_items;++i)item["counters"].append(Json::UInt64(stats.values[i])); }
            else { struct btrfs_ioctl_scrub_args args{}; args.devid=id;
                if(::ioctl(root.fd(),BTRFS_IOC_SCRUB_PROGRESS,&args)==0) { item["running"]=true; item["progress"]=progress(args.progress); }
                else { require(errno==ENOTCONN,"scrub-status-unavailable","Cannot inspect scrub status"); item["running"]=false; } }
            out["device_stats"].append(item);
        } return out;
    }
    require(operation=="subvolumes","unknown-command","Unknown native read-only Btrfs operation"); out["subvolumes"]=Value(Json::arrayValue);
    struct btrfs_ioctl_search_args args{}; args.key.tree_id=BTRFS_ROOT_TREE_OBJECTID; args.key.min_objectid=BTRFS_FIRST_FREE_OBJECTID;
    args.key.max_objectid=UINT64_MAX; args.key.min_type=BTRFS_ROOT_ITEM_KEY; args.key.max_type=BTRFS_ROOT_ITEM_KEY; args.key.max_offset=UINT64_MAX; args.key.max_transid=UINT64_MAX;
    for(unsigned pages=0;pages<128;++pages) {
        args.key.nr_items=128; require(::ioctl(root.fd(),BTRFS_IOC_TREE_SEARCH,&args)==0,"btrfs-search-unavailable","Kernel subvolume tree search is unavailable");
        if(args.key.nr_items==0)return out;
        std::size_t position=0; std::uint64_t last=0;
        for(unsigned i=0;i<args.key.nr_items;++i) {
            require(position+sizeof(struct btrfs_ioctl_search_header)<=sizeof(args.buf),"invalid-btrfs-search","Tree-search header exceeds its buffer");
            struct btrfs_ioctl_search_header header{}; std::memcpy(&header,args.buf+position,sizeof(header)); position+=sizeof(header);
            require(header.len<=sizeof(args.buf)-position && header.objectid>=args.key.min_objectid,"invalid-btrfs-search","Invalid tree-search item");
            if(header.type==BTRFS_ROOT_ITEM_KEY && header.len>=offsetof(struct btrfs_root_item,uuid)+16) {
                struct btrfs_root_item item{}; std::memcpy(&item,args.buf+position,std::min<std::size_t>(header.len,sizeof(item)));
                Value row; row["tree_id"]=Json::UInt64(header.objectid); row["uuid"]=hex(item.uuid); row["parent_uuid"]=hex(item.parent_uuid); row["received_uuid"]=hex(item.received_uuid);
                row["read_only"]=(item.flags&BTRFS_ROOT_SUBVOL_RDONLY)!=0; row["generation"]=Json::UInt64(item.generation); out["subvolumes"].append(row);
            } position+=header.len; last=header.objectid;
        }
        if(last==UINT64_MAX)return out;
        args.key.min_objectid=last+1;
    }
    out["truncated"]=true; return out;
}
Value btrfs_manage_plan(const Root& root,const Value& request,const std::string& profile) {
    require(identifier(profile) && request.isObject() && request["schema"]==1 && request["action"].isString(),"invalid-btrfs-request","Select a native Btrfs action and firmware profile");
    const auto action=request["action"].asString(); std::set<std::string> fields{"schema","action"};
    auto allow=[&](std::initializer_list<const char*> keys) { for(const auto* key:keys)fields.insert(key); };
    if(action=="create")allow({"path"});
    else if(action=="snapshot")allow({"source","path","read_only"});
    else if(action=="rollback")allow({"path","snapshot","saved_path"});
    else if(action=="delete")allow({"path","backup_snapshot"});
    else if(action=="readonly")allow({"path","read_only"});
    else if(action=="resize")allow({"target_bytes"});
    else if(action=="scrub")allow({"device_id","repair"});
    else if(action=="balance")allow({"usage_percent","chunk_limit"});
    else require(action=="scrub-cancel" || action=="balance-pause" || action=="balance-cancel","unknown-btrfs-action","Unknown native Btrfs management action");
    for(const auto& key:request.getMemberNames())require(fields.contains(key),"invalid-btrfs-request","A Btrfs request field is unrelated to the selected action");
    Value plan; plan["schema"]=1; plan["operation"]="btrfs.manage"; plan["operation_id"]=operation_id(); plan["request"]=request; plan["context"]=filesystem(root.fd()); plan["firmware_profile"]=profile;
    if(action=="create" || action=="snapshot" || action=="rollback") {
        require(request["path"].isString(),"invalid-btrfs-request","A selected child path is required"); const auto path=request["path"].asString(); const auto [parent,name]=split(path);
        auto parent_fd=root.open(parent,O_RDONLY|O_DIRECTORY); writable(parent_fd.get()); plan["parent_identity"]=descriptor_identity(parent_fd.get());
        require(filesystem(parent_fd.get())["fsid"]==plan["context"]["fsid"],"different-filesystem","Subvolume parent must remain on the selected Btrfs filesystem");
        require(identifier(name) && name!="." && name!="..","invalid-subvolume-name","Use a bounded subvolume name");
        if(action!="rollback")missing(root,path);
        if(action=="snapshot") { require(request["source"].isString() && request["read_only"].isBool(),"invalid-btrfs-request","Snapshot source and read-only choice are required");
            plan["source_identity"]=volume(root,request["source"].asString()); auto src=root.open(request["source"].asString(),O_RDONLY|O_DIRECTORY); filesystem_tree_outside(src.get(),parent_fd.get()); }
        if(action=="rollback") {
            require(request["snapshot"].isString() && request["saved_path"].isString(),"invalid-btrfs-request","Rollback must retain the original subvolume at an unused saved path");
            plan["active_identity"]=volume(root,path); plan["snapshot_identity"]=volume(root,request["snapshot"].asString());
            require(plan["active_identity"]["tree_id"].asUInt64()>5,"protected-btrfs-root","The filesystem top-level root cannot be replaced");
            require(plan["snapshot_identity"]["read_only"]==true && plan["snapshot_identity"]["parent_uuid"]==plan["active_identity"]["uuid"],"wrong-rollback-snapshot","Rollback requires a read-only snapshot derived from the selected original");
            const auto [saved_parent,saved_name]=split(request["saved_path"].asString()); require(saved_parent==parent && identifier(saved_name),"invalid-rollback-path","Original and saved subvolumes must share a parent directory"); missing(root,request["saved_path"].asString());
            auto active=root.open(path,O_RDONLY|O_DIRECTORY); exclude_mounts_and_users(active.get());
            auto snapshot=root.open(request["snapshot"].asString(),O_RDONLY|O_DIRECTORY); filesystem_tree_outside(active.get(),snapshot.get());
            plan["staging_path"]=(fs::path(parent)/(".ure-rollback-"+plan["operation_id"].asString())).generic_string(); missing(root,plan["staging_path"].asString());
        }
    } else if(action=="delete" || action=="readonly") {
        require(request["path"].isString(),"invalid-btrfs-request","Select an exact child subvolume"); plan["source_identity"]=volume(root,request["path"].asString());
        require(plan["source_identity"]["tree_id"].asUInt64()>5,"protected-btrfs-root","The filesystem top-level root cannot be deleted or have its flags changed");
        auto source=root.open(request["path"].asString(),O_RDONLY|O_DIRECTORY); writable(root.fd());
        if(action=="delete") { require(request["backup_snapshot"].isString(),"backup-required","Deletion requires a retained read-only snapshot backup");
            plan["backup_identity"]=volume(root,request["backup_snapshot"].asString());
            require(plan["backup_identity"]["read_only"]==true && plan["backup_identity"]["parent_uuid"]==plan["source_identity"]["uuid"],"wrong-backup-snapshot","Deletion backup is not a read-only snapshot of the selected source");
            auto backup=root.open(request["backup_snapshot"].asString(),O_RDONLY|O_DIRECTORY); filesystem_tree_outside(source.get(),backup.get()); exclude_mounts_and_users(source.get()); }
        else require(request["read_only"].isBool() && (request["read_only"]==true || plan["source_identity"]["received_uuid"]==std::string(32,'0')),
            "received-snapshot-protected","Received snapshots cannot be made writable without preserving their incremental lineage");
    } else if(action=="resize") { writable(root.fd()); require(plan["context"]["devices"].asUInt64()==1 && plan["context"]["max_device_id"].asUInt64()==1 && request["target_bytes"].isUInt64() && request["target_bytes"].asUInt64()>=256*1024*1024 && request["target_bytes"].asUInt64()%4096==0,
        "invalid-btrfs-size","Resize currently supports one device and an exact 4 KiB multiple of at least 256 MiB"); plan["usage"]=btrfs_native_info(root,"usage"); }
    else if(action=="scrub" || action=="scrub-cancel") {
        if(action=="scrub") {
        require(request["device_id"].isUInt64() && request["device_id"].asUInt64()>0 && request["device_id"].asUInt64()<=plan["context"]["max_device_id"].asUInt64(),"invalid-btrfs-device","Select an existing filesystem device ID");
        struct btrfs_ioctl_dev_info_args device{}; device.devid=request["device_id"].asUInt64();
        require(::ioctl(root.fd(),BTRFS_IOC_DEV_INFO,&device)==0,"invalid-btrfs-device","Selected Btrfs device ID is absent");
        require(request["repair"].isBool(),"invalid-btrfs-request","Choose scrub verification or repair explicitly"); struct statvfs mount{};
            require(::fstatvfs(root.fd(),&mount)==0,"io-error","Cannot inspect scrub mount flags");
            require(request["repair"]==true || (mount.f_flag&ST_RDONLY),"read-only-mount-required","A verification-only scrub needs a read-only mount; read-only scrub on a writable mount can still modify filesystem metadata");
            if(request["repair"]==true)writable(root.fd()); }
    } else if(action=="balance") { writable(root.fd()); require(request["usage_percent"].isUInt() && request["usage_percent"].asUInt()<=90 && request["chunk_limit"].isUInt() &&
        request["chunk_limit"].asUInt()>0 && request["chunk_limit"].asUInt()<=128,"invalid-balance-filter","Balance requires bounded usage and chunk limits"); require(balance(root.fd())["running"]==false,"balance-busy","Another balance is already active"); }
    else require(action=="balance-pause" || action=="balance-cancel","unknown-btrfs-action","Unknown native Btrfs management action");
    for(const auto* key:{"source_identity","active_identity","snapshot_identity","backup_identity"})if(plan.isMember(key))
        require(plan[key]["fsid"]==plan["context"]["fsid"],"different-filesystem","Every selected subvolume must belong to the reviewed filesystem");
    plan["native_ioctl"]=true; plan["physical_test_record"]=false; plan["private_record"]=true; plan["confirmation_required"]=true;
    plan["risk"]=action=="rollback" ? "Replace the unmounted active path with a writable snapshot clone; retain the original at the saved path. Mounted roots and users are blocked; update boot rootflags separately." :
        action=="delete" ? "Delete only the selected subvolume; a derived read-only backup remains. Nested subvolumes are never recursively removed." :
        "Modify only the selected mounted Btrfs filesystem; maintenance is not a raw-block rollback transaction.";
    plan["plan_sha256"]=seal(plan); return plan;
}
Value btrfs_manage_execute(const Root& root,const Value& plan,const fs::path& path,const std::string& confirmation) {
    validate(plan); require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact native Btrfs plan hash");
    const auto& request=plan["request"]; const auto action=request["action"].asString();
    require(action!="scrub-cancel" && action!="balance-pause" && action!="balance-cancel","owner-control-required",
        "Cancel or pause requires the captured running root, sealed plan and original journal through the owner-control API");
    require(json(filesystem(root.fd()))==json(plan["context"]),"stale-btrfs-filesystem","Filesystem, mount or device identity changed");
    const bool resume=fs::exists(path);
    if(!resume) {
        auto refreshed=btrfs_manage_plan(root,plan["request"],plan["firmware_profile"].asString()); refreshed["operation_id"]=plan["operation_id"];
        if(refreshed.isMember("staging_path"))refreshed["staging_path"]=plan["staging_path"];
        refreshed["plan_sha256"]=seal(refreshed); require(json(refreshed)==json(plan),"stale-btrfs-plan","Native Btrfs request or observed identities changed since review");
    } else {
        auto prior=private_directory(path,false); const auto persisted=record(prior,"plan.json"); validate(persisted);
        require(json(persisted)==json(plan),"wrong-btrfs-plan","Recovery requires the exact persisted sealed management plan"); checked_state(prior,plan);
    }
    ManagedOperation operation(operation_binding("btrfs.manage",plan,path,operation_targets(plan["context"])),resume);
    require(json(filesystem(root.fd()))==json(plan["context"]),"stale-btrfs-filesystem","Filesystem changed during operation admission");
    Root journal_parent(path.parent_path().empty() ? fs::path(".") : path.parent_path()); filesystem_tree_gate(journal_parent.fd());
    for(const auto* key:{"path","source","snapshot","backup_snapshot","saved_path"})if(plan["request"][key].isString() && root.exists(plan["request"][key].asString())) {
        auto selected=root.open(plan["request"][key].asString(),O_RDONLY|O_DIRECTORY); filesystem_tree_outside(selected.get(),journal_parent.fd());
    }
    auto store=private_directory(path,!resume); auto writer=store.open("writer.lock",O_RDWR|O_CREAT,0600);
    struct stat lock{}; require(::fstat(writer.get(),&lock)==0 && S_ISREG(lock.st_mode) && lock.st_nlink==1 && lock.st_uid==::geteuid() && (lock.st_mode&07777)==0600 && ::flock(writer.get(),LOCK_EX|LOCK_NB)==0,"operation-busy","Btrfs operation journal is unsafe or busy");
    Value state; if(resume) { require(json(record(store,"plan.json"))==json(plan),"wrong-btrfs-plan","Existing journal belongs to another operation"); state=checked_state(store,plan); }
    else { store.save_record("plan.json",plan); state["schema"]=1; state["plan_sha256"]=plan["plan_sha256"]; save(store,state,"PLANNED"); }
    if(state["state"]=="COMPLETE") {
        verify_complete(root,plan,state); writer=Fd(); return operation.finish(state,true,true);
    }
    if(state["state"]=="CANCELLED_SAFE" || (resume && (state["state"]=="SCRUBBING" || state["state"]=="BALANCING" || state["state"]=="PAUSED"))) {
        if(state["state"]=="CANCELLED_SAFE")require(operation.token().has_retained_intent(),"maintenance-recovery-required","Already released cancelled maintenance cannot be replayed");
        require((action=="scrub" || action=="balance") && maintenance_stopped(root,plan),"maintenance-recovery-required",
            "Exact-plan recovery requires independently observed inactive kernel maintenance");
        state["kernel_maintenance_inactive"]=true; state["maintenance_result_verified"]=false; state["successful"]=false;
        save(store,state,"CANCELLED_SAFE"); writer=Fd(); return operation.finish(state,true,true);
    }
    if(action=="rollback") {
        const auto active=request["path"].asString(),snapshot=request["snapshot"].asString(),saved=request["saved_path"].asString(),staging=plan["staging_path"].asString();
        identity(root,snapshot,plan["snapshot_identity"]);
        const auto [parent,name]=split(active); const auto [ignored,stage_name]=split(staging); const auto [ignored_saved,saved_name]=split(saved); (void)ignored; (void)ignored_saved;
        auto directory=root.open(parent,O_RDONLY|O_DIRECTORY);
        if(state["state"]=="PLANNED") {
            identity(root,active,plan["active_identity"]); auto selected=root.open(active,O_RDONLY|O_DIRECTORY); exclude_mounts_and_users(selected.get());
            operation.begin("BTRFS_ROLLBACK_STAGING");
            create_snapshot(root,snapshot,staging,false); state["staged_identity"]=volume(root,staging); save(store,state,"STAGED");
        }
        if(state["state"]=="STAGED") {
            const auto now=volume(root,active);
            if(now["uuid"]==plan["active_identity"]["uuid"]) {
                identity(root,active,plan["active_identity"]); identity(root,staging,state["staged_identity"]); auto selected=root.open(active,O_RDONLY|O_DIRECTORY); exclude_mounts_and_users(selected.get());
                operation.begin("BTRFS_ROLLBACK_EXCHANGING");
                require(::syscall(SYS_renameat2,directory.get(),name.c_str(),directory.get(),stage_name.c_str(),RENAME_EXCHANGE)==0 && ::fsync(directory.get())==0,"rollback-exchange-failed","Cannot exchange original and snapshot clone");
            } else { require(now["uuid"]==state["staged_identity"]["uuid"],"changed-subvolume","Rollback active path diverged"); renamed_identity(root,staging,plan["active_identity"]); }
            save(store,state,"EXCHANGED");
        }
        if(state["state"]=="EXCHANGED") {
            renamed_identity(root,active,state["staged_identity"]);
            if(root.exists(staging)) { renamed_identity(root,staging,plan["active_identity"]); missing(root,saved);
                operation.begin("BTRFS_ROLLBACK_RETAINING_ORIGINAL");
                require(::syscall(SYS_renameat2,directory.get(),stage_name.c_str(),directory.get(),saved_name.c_str(),RENAME_NOREPLACE)==0 && ::fsync(directory.get())==0,"rollback-save-failed","Cannot retain the original subvolume"); }
            renamed_identity(root,saved,plan["active_identity"]); state["original_saved_path"]=saved; state["active_identity"]=volume(root,active); save(store,state,"COMPLETE");
        }
        require(state["state"]=="COMPLETE","maintenance-recovery-required","Rollback did not reach a verified terminal state");
        verify_complete(root,plan,state); writer=Fd(); return operation.finish(state,true,true);
    }
    require(state["state"]=="PLANNED","maintenance-recovery-required","Interrupted maintenance must be inspected using native status and explicit cancellation; it is not restarted blindly");
    if(plan.isMember("source_identity"))identity(root,request["source"].isString() ? request["source"].asString() : request["path"].asString(),plan["source_identity"]);
    if(action=="create" || action=="snapshot") {
        const auto destination=request["path"].asString(); if(action=="snapshot") {
            operation.begin("BTRFS_SNAPSHOT_CREATING"); create_snapshot(root,request["source"].asString(),destination,request["read_only"].asBool());
        }
        else { const auto [parent,name]=split(destination); auto directory=root.open(parent,O_RDONLY|O_DIRECTORY); missing(root,destination);
            require(json(descriptor_identity(directory.get()))==json(plan["parent_identity"]),"stale-subvolume-parent","Creation parent changed"); struct btrfs_ioctl_vol_args_v2 args{}; std::memcpy(args.name,name.c_str(),name.size()+1);
            operation.begin("BTRFS_SUBVOLUME_CREATING");
            require(::ioctl(directory.get(),BTRFS_IOC_SUBVOL_CREATE_V2,&args)==0 && ::fsync(directory.get())==0,"subvolume-create-failed","Cannot create native Btrfs subvolume"); }
        state["created_identity"]=volume(root,destination);
    } else if(action=="readonly") {
        auto source=root.open(request["path"].asString(),O_RDONLY|O_DIRECTORY); __u64 flags=0;
        require(::ioctl(source.get(),BTRFS_IOC_SUBVOL_GETFLAGS,&flags)==0,"btrfs-info-unavailable","Cannot inspect subvolume flags");
        flags=request["read_only"].asBool() ? flags|BTRFS_SUBVOL_RDONLY : flags&~BTRFS_SUBVOL_RDONLY;
        operation.begin("BTRFS_FLAGS_CHANGING");
        require(::ioctl(source.get(),BTRFS_IOC_SUBVOL_SETFLAGS,&flags)==0 && ::fsync(source.get())==0,"subvolume-flags-failed","Cannot change subvolume read-only state");
        state["updated_identity"]=volume(root,request["path"].asString());
    } else if(action=="delete") {
        identity(root,request["backup_snapshot"].asString(),plan["backup_identity"]); auto selected=root.open(request["path"].asString(),O_RDONLY|O_DIRECTORY); exclude_mounts_and_users(selected.get());
        const auto [parent,name]=split(request["path"].asString()); auto directory=root.open(parent,O_RDONLY|O_DIRECTORY); struct btrfs_ioctl_vol_args_v2 args{}; std::memcpy(args.name,name.c_str(),name.size()+1);
        save(store,state,"DELETING"); operation.begin("BTRFS_SUBVOLUME_DELETING"); require(::ioctl(directory.get(),BTRFS_IOC_SNAP_DESTROY_V2,&args)==0 && ::fsync(directory.get())==0,"subvolume-delete-failed","Deletion failed; nested subvolumes and mounted roots are not forced");
        state["retained_backup_identity"]=volume(root,request["backup_snapshot"].asString());
    } else if(action=="resize") {
        const auto text="1:"+std::to_string(request["target_bytes"].asUInt64()); struct btrfs_ioctl_vol_args args{}; std::memcpy(args.name,text.c_str(),text.size()+1); save(store,state,"RESIZING");
        operation.begin("BTRFS_RESIZING");
        require(::ioctl(root.fd(),BTRFS_IOC_RESIZE,&args)==0,"btrfs-resize-failed","Kernel refused the filesystem size; underlying partition is never resized here"); state["usage"]=btrfs_native_info(root,"usage");
    } else if(action=="scrub") {
        struct btrfs_ioctl_scrub_args args{}; args.devid=request["device_id"].asUInt64(); args.end=UINT64_MAX; args.flags=request["repair"].asBool() ? 0 : BTRFS_SCRUB_READONLY; save(store,state,"SCRUBBING");
        operation.begin("BTRFS_SCRUBBING"); const auto outcome=::ioctl(root.fd(),BTRFS_IOC_SCRUB,&args); const auto failure=errno;
        if(outcome<0 && failure==ECANCELED) {
            require(maintenance_stopped(root,plan),"maintenance-still-active","Cancelled scrub has not released its kernel worker");
            state["kernel_maintenance_inactive"]=true; state["maintenance_result_verified"]=false; state["successful"]=false;
            save(store,state,"CANCELLED_SAFE"); writer=Fd(); return operation.finish(state,true,true);
        }
        require(outcome==0,"scrub-failed","Scrub failed; inspect native scrub/device status"); state["scrub_progress"]=progress(args.progress);
    } else if(action=="balance") {
        struct btrfs_ioctl_balance_args args{}; args.flags=BTRFS_BALANCE_DATA|BTRFS_BALANCE_METADATA;
        for(auto* filters:{&args.data,&args.meta}) { filters->usage=request["usage_percent"].asUInt(); filters->limit=request["chunk_limit"].asUInt(); filters->flags=BTRFS_BALANCE_ARGS_USAGE|BTRFS_BALANCE_ARGS_LIMIT; }
        save(store,state,"BALANCING"); operation.begin("BTRFS_BALANCING"); const auto outcome=::ioctl(root.fd(),BTRFS_IOC_BALANCE_V2,&args); const auto failure=errno;
        if(outcome<0 && failure==ECANCELED) {
            state["maintenance_result_verified"]=false; state["successful"]=false;
            if(maintenance_stopped(root,plan)) {
                state["kernel_maintenance_inactive"]=true; save(store,state,"CANCELLED_SAFE"); writer=Fd(); return operation.finish(state,true,true);
            }
            state["operation_owner_retained"]=true; save(store,state,"PAUSED"); return state;
        }
        require(outcome==0,"balance-failed","Balance failed; inspect native balance status");
        state["completed_chunks"]=Json::UInt64(args.stat.completed);
    }
    state["native_ioctl"]=true; state["physical_test_record"]=false; verify_complete(root,plan,state); save(store,state,"COMPLETE");
    writer=Fd(); return operation.finish(state,true,true);
}
Value btrfs_manage_control(const Root& captured_root,const Value& running_plan,const fs::path& path,const std::string& action,const std::string& confirmation) {
    validate(running_plan); require(confirmation==running_plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact running maintenance plan hash");
    const auto running_action=running_plan["request"]["action"].asString();
    require((running_action=="scrub" && action=="scrub-cancel") ||
        (running_action=="balance" && (action=="balance-pause" || action=="balance-cancel")),"invalid-maintenance-control","Control action does not match the captured running maintenance plan");
    auto store=private_directory(path,false);
    const auto check=[&] {
        require(json(record(store,"plan.json"))==json(running_plan),"wrong-btrfs-plan","Control journal differs from the captured running plan");
        require(json(filesystem(captured_root.fd()))==json(running_plan["context"]),"stale-btrfs-filesystem","Captured maintenance root, mount or FSID changed");
        const auto state=checked_state(store,running_plan);
        require((running_action=="scrub" && state["state"]=="SCRUBBING") ||
            (running_action=="balance" && (state["state"]=="BALANCING" || state["state"]=="PAUSED")),
            "maintenance-not-running","Captured journal has no matching active or paused maintenance worker");
    };
    check(); const auto binding=operation_binding("btrfs.manage",running_plan,path,operation_targets(running_plan["context"]));
    auto control=OwnerControlLease::acquire(binding); check(); control.require_binding(binding);
    require(!maintenance_stopped(captured_root,running_plan),"maintenance-not-running","Exact filesystem maintenance is already inactive");
    if(action=="scrub-cancel")require(::ioctl(captured_root.fd(),BTRFS_IOC_SCRUB_CANCEL)==0,"scrub-cancel-failed","Cannot cancel the captured filesystem scrub");
    else { const int request=action=="balance-pause" ? BTRFS_BALANCE_CTL_PAUSE : BTRFS_BALANCE_CTL_CANCEL;
        require(::ioctl(captured_root.fd(),BTRFS_IOC_BALANCE_CTL,request)==0,"balance-control-failed","Cannot control the captured filesystem balance"); }
    Value result; result["state"]="CONTROL_SENT"; result["action"]=action; result["plan_sha256"]=running_plan["plan_sha256"];
    result["context"]=running_plan["context"]; result["operation_owner_retained"]=true; result["target_contents_verified"]=false;
    result["private_record"]=true; result["physical_test_record"]=false; return result;
}
} // namespace ure
