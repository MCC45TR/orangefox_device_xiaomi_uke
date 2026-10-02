// SPDX-License-Identifier: Apache-2.0
// Native Linux UAPI operations. No external btrfs command or shell is executed.
#include "uke.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/btrfs.h>
#include <linux/capability.h>
#include <linux/magic.h>
#include <linux/stat.h>
#include <openssl/evp.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ure {
namespace {
std::string hex(std::string_view bytes) {
    static constexpr char digits[]="0123456789abcdef";
    std::string out; out.reserve(bytes.size()*2);
    for(char byte:bytes) { const auto c=static_cast<unsigned char>(byte); out+=digits[c>>4]; out+=digits[c&15]; }
    return out;
}
template<class T> std::string hex16(const T* bytes) { return hex(std::string_view(reinterpret_cast<const char*>(bytes),16)); }
bool hex_uuid(const Value& value,bool zero=false) {
    if(!value.isString() || value.asString().size()!=32)return false;
    bool nonzero=false;
    for(char c:value.asString()) { if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return false; nonzero|=c!='0'; }
    return zero || nonzero;
}
struct stat stat_fd(int fd) { struct stat st{}; require(::fstat(fd,&st)==0,"io-error","Cannot inspect Btrfs descriptor"); return st; }
Value location(int fd) {
    const auto st=stat_fd(fd); struct statx sx{};
    require(::syscall(SYS_statx,fd,"",AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW,STATX_MNT_ID,&sx)==0 && (sx.stx_mask&STATX_MNT_ID),
        "mount-identity-unavailable","Btrfs operations require mount identity support");
    Value out; out["device"]=Json::UInt64(st.st_dev); out["inode"]=Json::UInt64(st.st_ino); out["mount_id"]=Json::UInt64(sx.stx_mnt_id); return out;
}
void check_location(const Value& value) {
    require(value.isObject() && value["device"].isUInt64() && value["inode"].isUInt64() && value["mount_id"].isUInt64(),
        "invalid-btrfs-plan","Invalid retained Btrfs location");
}
Value filesystem(int fd) {
    filesystem_tree_gate(fd); struct statfs st{};
    require(::fstatfs(fd,&st)==0 && st.f_type==BTRFS_SUPER_MAGIC,"not-btrfs","Select an already mounted Btrfs filesystem");
    struct btrfs_ioctl_fs_info_args info{};
    require(::ioctl(fd,BTRFS_IOC_FS_INFO,&info)==0,"btrfs-info-unavailable","Kernel Btrfs filesystem information is unavailable");
    Value out=location(fd); out["fsid"]=hex16(info.fsid); return out;
}
Value subvolume(int fd,bool usable=false) {
    Value out=filesystem(fd); const auto st=stat_fd(fd);
    require(st.st_ino==256 && S_ISDIR(st.st_mode),"not-subvolume","Select the subvolume root, not an ordinary directory");
    struct btrfs_ioctl_get_subvol_info_args info{}; __u64 flags=0;
    require(::ioctl(fd,BTRFS_IOC_GET_SUBVOL_INFO,&info)==0 && ::ioctl(fd,BTRFS_IOC_SUBVOL_GETFLAGS,&flags)==0,
        "btrfs-info-unavailable","Kernel subvolume identity or flags are unavailable");
    require(!usable || info.treeid>5,"unsupported-subvolume","The filesystem top-level root is not a sendable snapshot source");
    out["tree_id"]=Json::UInt64(info.treeid); out["uuid"]=hex16(info.uuid); out["parent_uuid"]=hex16(info.parent_uuid);
    out["received_uuid"]=hex16(info.received_uuid); out["generation"]=Json::UInt64(info.generation);
    out["ctransid"]=Json::UInt64(info.ctransid); out["read_only"]=(flags&BTRFS_SUBVOL_RDONLY)!=0;
    out["name_hex"]=hex(std::string_view(info.name,::strnlen(info.name,sizeof(info.name))));
    out["stream_uuid"]=out["received_uuid"]==std::string(32,'0') ? out["uuid"] : out["received_uuid"];
    return out;
}
void check_identity(const Value& value) {
    check_location(value);
    require(hex_uuid(value["fsid"]) && hex_uuid(value["uuid"]) && hex_uuid(value["parent_uuid"],true) &&
        hex_uuid(value["received_uuid"],true) && value["tree_id"].isUInt64() && value["tree_id"].asUInt64()>5 &&
        value["generation"].isUInt64() && value["ctransid"].isUInt64() && value["read_only"].isBool() &&
        value["name_hex"].isString() && value["name_hex"].asString().size()<=510 && hex_uuid(value["stream_uuid"]) &&
        value["stream_uuid"]==(value["received_uuid"]==std::string(32,'0') ? value["uuid"] : value["received_uuid"]),
        "invalid-btrfs-plan","Invalid Btrfs subvolume identity");
    const auto name=value["name_hex"].asString(); require(!name.empty() && name.size()%2==0 &&
        std::all_of(name.begin(),name.end(),[](char c) { return (c>='0' && c<='9') || (c>='a' && c<='f'); }),
        "invalid-btrfs-plan","Invalid encoded subvolume name");
}
void same_subvolume(int fd,const Value& expected,bool immutable) {
    const auto current=subvolume(fd,true);
    if(immutable)require(json(current)==json(expected),"stale-subvolume","Read-only snapshot identity, name or generation changed");
    else for(const auto* field:{"device","inode","mount_id","fsid","tree_id","uuid","received_uuid","read_only","name_hex"})
        require(json(current[field])==json(expected[field]),"stale-subvolume","Snapshot source identity or selection changed");
}
std::string seal(Value plan) { plan.removeMember("plan_sha256"); return sha256(json(plan)); }
Value record(const Root& store,const std::string& name) {
    const auto st=store.stat(name);
    require(S_ISREG(st.st_mode) && st.st_uid==::geteuid() && st.st_nlink==1 && (st.st_mode&07777)==0600,
        "unsafe-btrfs-store","Btrfs records must be private single-link regular files"); return parse_json(store.read(name));
}
void check_plan(const Value& plan) {
    require(plan["schema"]==1 && plan["format"]=="ure-btrfs-backup" && (plan["operation"]=="snapshot" || plan["operation"]=="send") &&
        plan["operation_id"].isString() && identifier(plan["operation_id"].asString()) && plan["firmware_profile"].isString() &&
        identifier(plan["firmware_profile"].asString()) && plan["source_path"].isString() && plan["plan_sha256"].isString() &&
        hash_valid(plan["plan_sha256"].asString()) && plan["plan_sha256"].asString()==seal(plan),"invalid-btrfs-plan","Invalid sealed Btrfs plan");
    components(plan["source_path"].asString()); check_location(plan["context"]); check_identity(plan["source_identity"]);
    if(plan["operation"]=="send") {
        require(plan["protocol"]==1 && plan["source_identity"]["read_only"]==true && plan["parent_path"].isString(),"invalid-btrfs-plan","Send requires read-only snapshots and protocol 1");
        if(!plan["parent_path"].asString().empty()) {
            components(plan["parent_path"].asString()); check_identity(plan["parent_identity"]);
            require(plan["parent_identity"]["read_only"]==true && plan["parent_identity"]["fsid"]==plan["source_identity"]["fsid"] &&
                plan["parent_identity"]["uuid"]!=plan["source_identity"]["uuid"],"invalid-btrfs-plan","Invalid incremental parent snapshot");
        } else require(plan["parent_identity"].isNull(),"invalid-btrfs-plan","Unexpected full-send parent");
    } else {
        require(plan["snapshot_parent"].isString() && plan["snapshot_name"].isString() && identifier(plan["snapshot_name"].asString()) &&
            plan["snapshot_name"]!="." && plan["snapshot_name"]!=".." && plan["staging_name"]==".ure-snapshot-"+plan["operation_id"].asString(),
            "invalid-btrfs-plan","Invalid snapshot destination");
        components(plan["snapshot_parent"].asString()); check_location(plan["parent_location"]);
        require(plan["parent_location"]["fsid"]==plan["source_identity"]["fsid"],"invalid-btrfs-plan","Snapshot destination filesystem differs");
    }
}
Value state_record(const Root& store,const Value& plan) {
    const auto state=record(store,"state.json"); require(state["schema"]==1 && state["plan_sha256"]==plan["plan_sha256"] && state["state"].isString(),
        "invalid-btrfs-state","Btrfs state is not bound to its plan"); return state;
}
Value state(const Value& plan,const std::string& status) {
    Value out; out["schema"]=1; out["plan_sha256"]=plan["plan_sha256"]; out["state"]=status; out["bytes"]=Json::UInt64(0); return out;
}
Value summary(const Value& plan,const Value& progress) {
    Value out=plan; out["progress"]=progress; out["private_record"]=true; out["physical_test_record"]=false; out["data_verified"]=false; return out;
}
Fd lock(const Root& store) {
    auto fd=store.open("writer.lock",O_RDWR|O_CREAT|O_NONBLOCK,0600); const auto st=stat_fd(fd.get());
    require(S_ISREG(st.st_mode) && st.st_uid==::geteuid() && st.st_nlink==1 && (st.st_mode&07777)==0600,"unsafe-btrfs-store","Unsafe Btrfs writer lock");
    require(::flock(fd.get(),LOCK_EX|LOCK_NB)==0,"operation-busy","Another Btrfs operation owns this store"); return fd;
}
Value new_plan(const Root& context,const std::string& source,const std::string& profile) {
    require(identifier(profile),"invalid-profile","Select a firmware profile identifier"); auto fd=context.open(source,O_RDONLY|O_DIRECTORY);
    Value plan; plan["schema"]=1; plan["format"]="ure-btrfs-backup"; plan["operation_id"]=operation_id(); plan["firmware_profile"]=profile;
    plan["source_path"]=source; plan["context"]=location(context.fd()); plan["source_identity"]=subvolume(fd.get(),true); return plan;
}
Value save_plan(const Root& context,Value plan,const fs::path& path) {
    Root parent(path.parent_path().empty() ? fs::path(".") : path.parent_path()); filesystem_tree_gate(parent.fd());
    auto source=context.open(plan["source_path"].asString(),O_RDONLY|O_DIRECTORY); filesystem_tree_outside(source.get(),parent.fd());
    if(plan["operation"]=="send" && !plan["parent_path"].asString().empty()) {
        auto previous=context.open(plan["parent_path"].asString(),O_RDONLY|O_DIRECTORY); filesystem_tree_outside(previous.get(),parent.fd());
    }
    plan["plan_sha256"]=seal(plan); check_plan(plan); auto store=private_directory(path,true);
    store.save_record("plan.json",plan); const auto progress=state(plan,"PLANNED"); store.save_record("state.json",progress); return summary(plan,progress);
}
void confirm(const Value& plan,const std::string& confirmation,const char* operation) {
    require(plan["operation"]==operation,"wrong-btrfs-plan","Select a plan for this Btrfs operation");
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact Btrfs plan hash");
}
void absent(int parent,const std::string& name) {
    struct stat st{}; errno=0; const auto result=::fstatat(parent,name.c_str(),&st,AT_SYMLINK_NOFOLLOW);
    require(result<0 && errno==ENOENT,"existing-target","Snapshot destination or staging name already exists; inspect it explicitly");
}
Fd stream_file(const Root& store,const std::string& name) {
    auto fd=store.open(name,O_RDONLY|O_NONBLOCK); const auto st=stat_fd(fd.get());
    require(S_ISREG(st.st_mode) && st.st_uid==::geteuid() && st.st_nlink==1 && (st.st_mode&07777)==0600,"unsafe-btrfs-store","Send streams must be private single-link regular files"); return fd;
}
void stream_matches(const Value& stream,const Value& plan) {
    const bool incremental=!plan["parent_path"].asString().empty();
    require(stream["source_uuid"]==plan["source_identity"]["stream_uuid"] && json(stream["source_ctransid"])==json(plan["source_identity"]["ctransid"]) &&
        stream["name_hex"]==plan["source_identity"]["name_hex"] && stream["incremental"]==incremental &&
        (!incremental || (stream["parent_uuid"]==plan["parent_identity"]["stream_uuid"] && json(stream["parent_ctransid"])==json(plan["parent_identity"]["ctransid"]))),
        "wrong-btrfs-stream","Send stream source or incremental parent differs from the plan");
}
std::uint64_t le(std::string_view data) {
    require(data.size()<=8,"invalid-btrfs-stream","Invalid stream integer width"); std::uint64_t out=0;
    for(std::size_t i=0;i<data.size();++i)out|=static_cast<std::uint64_t>(static_cast<unsigned char>(data[i]))<<(i*8);
    return out;
}
std::uint32_t crc32c(std::string_view bytes) {
    static const auto table=[] { std::array<std::uint32_t,256> out{}; for(unsigned i=0;i<out.size();++i) {
        std::uint32_t crc=i; for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1) ? 0x82f63b78U : 0); out[i]=crc;
    } return out; }();
    std::uint32_t crc=0; for(char byte:bytes)crc=(crc>>8)^table[(crc^static_cast<unsigned char>(byte))&255]; return crc;
}
void stream_path(std::string_view path,bool root=false,bool root_metadata=false) {
    // Kernel send uses an empty path for metadata on the subvolume root.
    // Creation, removal, rename, links and file data still need a child path.
    if(!root && root_metadata && (path.empty() || path=="."))return;
    require(!path.empty() && path.size()<=4096 && path.front()!='/' && path.find('\0')==path.npos && path.back()!='/',"invalid-btrfs-stream","Unsafe stream path");
    std::size_t offset=0; unsigned depth=0;
    while(offset<path.size()) {
        const auto slash=path.find('/',offset); const auto part=path.substr(offset,slash==path.npos ? path.npos : slash-offset);
        require(!part.empty() && part!="." && part!=".." && part.size()<=255 && ++depth<=64,"invalid-btrfs-stream","Unsafe stream path component");
        if(slash==path.npos)break;
        require(!root,"invalid-btrfs-stream","Subvolume stream name must be one component"); offset=slash+1;
    }
}
class Worker {
    pid_t pid_=-1;
public:
    explicit Worker(pid_t pid):pid_(pid) {}
    ~Worker() { if(pid_>0) { ::kill(pid_,SIGKILL); int status=0; while(::waitpid(pid_,&status,0)<0 && errno==EINTR) {} } }
    void finish() { int status=0; pid_t result; do { result=::waitpid(pid_,&status,0); } while(result<0 && errno==EINTR);
        require(result==pid_,"send-failed","Cannot collect the Btrfs send worker"); pid_=-1;
        require(WIFEXITED(status) && WEXITSTATUS(status)==0,"send-failed","Native Btrfs send failed; partial stream is retained for inspection and will be restarted"); }
};
void capability() {
    struct __user_cap_header_struct header{}; header.version=_LINUX_CAPABILITY_VERSION_3;
    std::array<struct __user_cap_data_struct,2> data{};
    require(::syscall(SYS_capget,&header,data.data())==0 && (data[CAP_SYS_ADMIN/32].effective&(1U<<(CAP_SYS_ADMIN%32))),
        "send-privilege-required","Kernel Btrfs send requires effective CAP_SYS_ADMIN; no partial stream was created");
}
void write_all(int fd,std::string_view bytes) {
    std::size_t offset=0;
    while(offset<bytes.size()) { const auto n=::write(fd,bytes.data()+offset,bytes.size()-offset); if(n<0 && errno==EINTR)continue;
        require(n>0,"io-error","Cannot write Btrfs send data"); offset+=static_cast<std::size_t>(n); }
}
void progress_write(const Root& store,Value& progress,std::uint64_t bytes) {
    progress["bytes"]=Json::UInt64(bytes); progress["updated_at"]=utc(); store.save_record("state.json",progress,true);
}
} // namespace

Value btrfs_subvolume_info(const Root& root,const std::string& relative) {
    auto fd=root.open(relative,O_RDONLY|O_DIRECTORY); Value out=subvolume(fd.get()); out["read_only_operation"]=true; out["physical_test_record"]=false; return out;
}
Value btrfs_snapshot_plan(const Root& root,const std::string& source,const std::string& parent,const std::string& name,
                          const std::string& profile,const fs::path& store) {
    require(identifier(name) && name!="." && name!="..","invalid-snapshot-name","Use a bounded snapshot name");
    auto plan=new_plan(root,source,profile); plan["operation"]="snapshot"; plan["snapshot_parent"]=parent; plan["snapshot_name"]=name;
    plan["staging_name"]=".ure-snapshot-"+plan["operation_id"].asString(); auto destination=root.open(parent,O_RDONLY|O_DIRECTORY);
    plan["parent_location"]=filesystem(destination.get()); auto selected=root.open(source,O_RDONLY|O_DIRECTORY);
    require(plan["parent_location"]["fsid"]==plan["source_identity"]["fsid"],"different-filesystem","Snapshot source and destination must share a Btrfs filesystem");
    filesystem_tree_outside(selected.get(),destination.get()); absent(destination.get(),name); absent(destination.get(),plan["staging_name"].asString());
    plan["snapshot_read_only"]=true; plan["coherence"]="atomic snapshot at execution time; nested subvolumes are not included";
    return save_plan(root,plan,store);
}
Value btrfs_snapshot_execute(const Root& root,const fs::path& path,const std::string& confirmation) {
    auto store=private_directory(path,false); auto writer=lock(store); const auto plan=record(store,"plan.json"); check_plan(plan); confirm(plan,confirmation,"snapshot");
    require(json(location(root.fd()))==json(plan["context"]),"stale-subvolume","Selected filesystem root changed");
    auto source=root.open(plan["source_path"].asString(),O_RDONLY|O_DIRECTORY); same_subvolume(source.get(),plan["source_identity"],false);
    auto parent=root.open(plan["snapshot_parent"].asString(),O_RDONLY|O_DIRECTORY);
    require(json(filesystem(parent.get()))==json(plan["parent_location"]),"stale-subvolume","Snapshot parent selection changed");
    filesystem_tree_outside(source.get(),parent.get()); auto progress=state_record(store,plan);
    const auto name=plan["snapshot_name"].asString(),staging=plan["staging_name"].asString();
    if(progress["state"]=="COMPLETE") {
        auto created=Root(Fd(::fcntl(parent.get(),F_DUPFD_CLOEXEC,0))).open(name,O_RDONLY|O_DIRECTORY);
        require(json(subvolume(created.get(),true))==json(progress["snapshot_identity"]),"stale-subvolume","Completed snapshot changed"); return summary(plan,progress);
    }
    if(progress["state"]=="PLANNED") {
        absent(parent.get(),name); absent(parent.get(),staging); struct btrfs_ioctl_vol_args_v2 args{};
        args.fd=source.get(); args.flags=BTRFS_SUBVOL_RDONLY; std::memcpy(args.name,staging.c_str(),staging.size()+1);
        require(::ioctl(parent.get(),BTRFS_IOC_SNAP_CREATE_V2,&args)==0,"snapshot-failed","Kernel could not create the read-only snapshot");
        auto created=Root(Fd(::fcntl(parent.get(),F_DUPFD_CLOEXEC,0))).open(staging,O_RDONLY|O_DIRECTORY); const auto identity=subvolume(created.get(),true);
        require(identity["read_only"]==true && identity["parent_uuid"]==plan["source_identity"]["uuid"],"snapshot-unverified","Created staging snapshot requires manual inspection");
        require(::fsync(parent.get())==0,"snapshot-unverified","Snapshot creation durability is unverified; inspect its staging name");
        progress["state"]="CREATED"; progress["snapshot_identity"]=identity; store.save_record("state.json",progress,true);
    }
    require(progress["state"]=="CREATED","invalid-btrfs-state","Snapshot state cannot be resumed");
    auto parent_root=Root(Fd(::fcntl(parent.get(),F_DUPFD_CLOEXEC,0)));
    // A crash after publication is recognized only by the recorded snapshot UUID.
    const auto at_name=parent_root.exists(name);
    auto created=parent_root.open(at_name ? name : staging,O_RDONLY|O_DIRECTORY); auto identity=subvolume(created.get(),true);
    auto expected=progress["snapshot_identity"]; if(at_name)expected["name_hex"]=hex(name);
    require(json(identity)==json(expected),"snapshot-unverified","Recorded snapshot identity changed; inspect before recovery");
    if(!at_name)require(::syscall(SYS_renameat2,parent.get(),staging.c_str(),parent.get(),name.c_str(),1)==0,
        "snapshot-unverified","Snapshot could not be published without replacement; staging is retained");
    require(::fsync(parent.get())==0,"snapshot-unverified","Published snapshot durability is unverified; inspect before recovery");
    identity=subvolume(created.get(),true); require(identity["name_hex"]==hex(name),"snapshot-unverified","Published snapshot name is unverified");
    progress["state"]="COMPLETE"; progress["snapshot_identity"]=identity; store.save_record("state.json",progress,true); return summary(plan,progress);
}
Value btrfs_send_plan(const Root& root,const std::string& source,const std::string& parent,const std::string& profile,const fs::path& store) {
    auto plan=new_plan(root,source,profile); plan["operation"]="send"; plan["protocol"]=1; plan["parent_path"]=parent; plan["parent_identity"]=Value();
    require(plan["source_identity"]["read_only"]==true,"snapshot-not-read-only","Create or select a read-only snapshot before send");
    if(!parent.empty()) { auto previous=root.open(parent,O_RDONLY|O_DIRECTORY); plan["parent_identity"]=subvolume(previous.get(),true);
        require(plan["parent_identity"]["read_only"]==true,"snapshot-not-read-only","Incremental parent must also remain read-only");
        require(plan["parent_identity"]["fsid"]==plan["source_identity"]["fsid"] && plan["parent_identity"]["uuid"]!=plan["source_identity"]["uuid"],
            "invalid-incremental-parent","Incremental parent must be a different snapshot on the same filesystem"); }
    plan["coherence"]="kernel send from retained read-only snapshots; nested subvolumes are not included";
    plan["resume_policy"]="restart incomplete stream from byte zero; completed verified streams are reused"; return save_plan(root,plan,store);
}
Value btrfs_stream_check(int fd) {
    const auto before=stat_fd(fd); require(S_ISREG(before.st_mode) && before.st_size>=17,"invalid-btrfs-stream","Expected a complete regular-file Btrfs stream");
    const auto bytes=static_cast<std::uint64_t>(before.st_size); std::uint64_t offset=0,commands=0;
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    require(hash && EVP_DigestInit_ex(hash.get(),EVP_sha256(),nullptr)==1,"hash-error","Cannot initialize stream hash");
    auto read=[&](char* data,std::size_t size) { require(size<=bytes-offset,"invalid-btrfs-stream","Truncated Btrfs command"); std::size_t done=0;
        while(done<size) { const auto n=::pread(fd,data+done,size-done,static_cast<off_t>(offset+done)); if(n<0 && errno==EINTR)continue;
            require(n>0,"invalid-btrfs-stream","Btrfs stream read failed or ended early"); done+=static_cast<std::size_t>(n); }
        require(EVP_DigestUpdate(hash.get(),data,size)==1,"hash-error","Cannot hash stream"); offset+=size; };
    std::array<char,65536> buffer{}; read(buffer.data(),17);
    require(std::string_view(buffer.data(),13)==std::string_view("btrfs-stream\0",13) && le(std::string_view(buffer.data()+13,4))==1,
        "unsupported-btrfs-stream","Only complete protocol 1 streams are accepted");
    Value out; bool ended=false;
    while(offset<bytes) {
        read(buffer.data(),10); const auto length=le(std::string_view(buffer.data(),4)); const auto command=le(std::string_view(buffer.data()+4,2));
        const auto expected_crc=le(std::string_view(buffer.data()+6,4)); require(length<=buffer.size()-10,"invalid-btrfs-stream","Protocol 1 command exceeds 64 KiB");
        read(buffer.data()+10,static_cast<std::size_t>(length)); std::fill(buffer.begin()+6,buffer.begin()+10,0);
        require(crc32c(std::string_view(buffer.data(),static_cast<std::size_t>(length)+10))==expected_crc,"btrfs-stream-corrupt","Btrfs command CRC32C differs");
        require((commands==0 && (command==1 || command==2)) || (commands>0 && command>=3 && command<=21),
            "unsupported-btrfs-stream","Unknown, nested or data-elided stream command");
        std::array<std::string_view,25> attrs{}; std::array<bool,25> present{};
        for(std::size_t cursor=10;cursor<length+10;) {
            require(length+10-cursor>=4,"invalid-btrfs-stream","Truncated Btrfs TLV header"); const auto type=le(std::string_view(buffer.data()+cursor,2));
            const auto count=le(std::string_view(buffer.data()+cursor+2,2)); cursor+=4;
            require(type>0 && type<attrs.size() && !present[type] && count<=length+10-cursor,"invalid-btrfs-stream","Unknown, duplicate or truncated Btrfs attribute");
            attrs[type]=std::string_view(buffer.data()+cursor,static_cast<std::size_t>(count)); present[type]=true; cursor+=static_cast<std::size_t>(count);
            if(type==1 || type==20)require(count==16,"invalid-btrfs-stream","Invalid stream UUID width");
            else if((type>=2 && type<=8) || type==18 || type==21 || type==23 || type==24)require(count==8,"invalid-btrfs-stream","Invalid stream integer width");
            else if(type>=9 && type<=12)require(count==12 && le(attrs[type].substr(8,4))<1000000000,"invalid-btrfs-stream","Invalid stream timestamp");
            else if(type==15 || type==16 || type==22 || (type==17 && command!=8))
                stream_path(attrs[type],commands==0,type==15 && (command==13 || command==14 || (command>=18 && command<=20)));
            else if(type==17)require(count>0 && count<=4096 && attrs[type].find('\0')==attrs[type].npos,"invalid-btrfs-stream","Invalid symlink target");
            else if(type==13)require(count>0 && count<=255 && attrs[type].find('\0')==attrs[type].npos,"invalid-btrfs-stream","Invalid attribute name");
        }
        const std::array<std::vector<unsigned>,22> required{{ {},{15,1,2},{15,1,2,20,21},{15,3},{15,3},{15,3,5,8},{15,3},{15,3},{15,3,17},
            {15,16},{15,17},{15},{15},{15,13,14},{15,13},{15,18,19},{15,18,20,21,22,23,24},{15,4},{15,5},{15,6,7},{15,9,10,11},{} }};
        for(auto type:required[command])require(present[type],"invalid-btrfs-stream","Missing required Btrfs attribute");
        for(unsigned type=1;type<present.size();++type)require(!present[type] || std::find(required[command].begin(),required[command].end(),type)!=required[command].end() || (command==20 && type==12),
            "invalid-btrfs-stream","Unexpected Btrfs command attribute");
        if(commands==0) { out["source_uuid"]=hex(attrs[1]); out["source_ctransid"]=Json::UInt64(le(attrs[2])); out["name_hex"]=hex(attrs[15]); out["incremental"]=command==2;
            require(hex_uuid(out["source_uuid"]),"invalid-btrfs-stream","Empty source UUID");
            if(command==2) { out["parent_uuid"]=hex(attrs[20]); out["parent_ctransid"]=Json::UInt64(le(attrs[21])); require(hex_uuid(out["parent_uuid"]),"invalid-btrfs-stream","Empty parent UUID"); } }
        if(command==16) { const auto clone=hex(attrs[20]); const auto transid=le(attrs[21]);
            require((clone==out["source_uuid"].asString() && transid==out["source_ctransid"].asUInt64()) ||
                (out["incremental"]==true && clone==out["parent_uuid"].asString() && transid==out["parent_ctransid"].asUInt64()),
                "unsupported-btrfs-stream","External clone sources are not accepted"); }
        ++commands;
        if(command==21) { require(offset==bytes,"invalid-btrfs-stream","Trailing data or concatenated streams are not accepted"); ended=true; break; }
    }
    require(ended,"invalid-btrfs-stream","Btrfs END command is missing"); const auto after=stat_fd(fd);
    require(before.st_size==after.st_size && before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec &&
        before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec,"stale-stream","Send stream changed during verification");
    std::array<unsigned char,32> digest{}; unsigned size=0; require(EVP_DigestFinal_ex(hash.get(),digest.data(),&size)==1 && size==32,"hash-error","Cannot finalize stream hash");
    out["sha256"]=hex(std::string_view(reinterpret_cast<const char*>(digest.data()),digest.size())); out["bytes"]=Json::UInt64(bytes);
    out["commands"]=Json::UInt64(commands); out["protocol"]=1; out["structural_validation"]=true; out["receive_executed"]=false; return out;
}
Value btrfs_backup_inspect(const fs::path& path) {
    auto store=private_directory(path,false); const auto plan=record(store,"plan.json"); check_plan(plan); return summary(plan,state_record(store,plan));
}
Value btrfs_send_verify(const fs::path& path) {
    auto store=private_directory(path,false); auto writer=lock(store); const auto plan=record(store,"plan.json"); check_plan(plan);
    require(plan["operation"]=="send","wrong-btrfs-plan","Offline data verification requires a send plan"); const auto progress=state_record(store,plan);
    require(progress["state"]=="COMPLETE","incomplete-btrfs-backup","Btrfs send is not complete"); auto fd=stream_file(store,"stream.bin");
    const auto stream=btrfs_stream_check(fd.get()); stream_matches(stream,plan); require(json(stream)==json(progress["stream"]),"btrfs-stream-corrupt","Stream hash or metadata differs from the completion record");
    auto out=summary(plan,progress); out["data_verified"]=true; out["receive_executed"]=false; return out;
}
Value btrfs_send_capture(const Root& root,const fs::path& path,const std::string& confirmation) {
    auto store=private_directory(path,false); auto writer=lock(store); const auto plan=record(store,"plan.json"); check_plan(plan); confirm(plan,confirmation,"send");
    require(json(location(root.fd()))==json(plan["context"]),"stale-subvolume","Selected filesystem root changed");
    auto source=root.open(plan["source_path"].asString(),O_RDONLY|O_DIRECTORY); same_subvolume(source.get(),plan["source_identity"],true);
    Fd previous; if(!plan["parent_path"].asString().empty()) { previous=root.open(plan["parent_path"].asString(),O_RDONLY|O_DIRECTORY); same_subvolume(previous.get(),plan["parent_identity"],true); }
    filesystem_tree_outside(source.get(),store.fd()); if(previous.get()>=0)filesystem_tree_outside(previous.get(),store.fd()); auto progress=state_record(store,plan);
    if(progress["state"]=="COMPLETE") { auto fd=stream_file(store,"stream.bin"); const auto stream=btrfs_stream_check(fd.get()); stream_matches(stream,plan);
        require(json(stream)==json(progress["stream"]),"btrfs-stream-corrupt","Completed stream changed"); auto out=summary(plan,progress); out["data_verified"]=true; return out; }
    if(progress["state"]!="SEALED") {
        require(progress["state"]=="PLANNED" || progress["state"]=="CAPTURING" || progress["state"]=="FAILED","invalid-btrfs-state","Send state cannot be restarted");
        require(!store.exists("stream.bin"),"unexpected-btrfs-stream","Unrecorded completed stream requires inspection"); capability();
        if(store.exists("partial.bin")) { auto partial=stream_file(store,"partial.bin"); require(::unlinkat(store.fd(),"partial.bin",0)==0 && ::fsync(store.fd())==0,"io-error","Cannot reset owned incomplete stream"); }
        auto partial=store.open("partial.bin",O_RDWR|O_CREAT|O_EXCL,0600); progress=state(plan,"CAPTURING"); progress_write(store,progress,0);
        int pipes[2]; require(::pipe2(pipes,O_CLOEXEC)==0,"io-error","Cannot create send pipe"); Fd input(pipes[0]),output(pipes[1]); const auto owner=::getpid();
        const auto child=::fork(); require(child>=0,"io-error","Cannot start native send worker");
        if(child==0) {
            if(::prctl(PR_SET_PDEATHSIG,SIGKILL)!=0 || ::getppid()!=owner)::_exit(125);
            ::close(input.get()); ::close(writer.get()); ::close(partial.get()); struct btrfs_ioctl_send_args args{};
            args.send_fd=output.get(); args.parent_root=previous.get()>=0 ? plan["parent_identity"]["tree_id"].asUInt64() : 0;
            const int result=::ioctl(source.get(),BTRFS_IOC_SEND,&args); ::close(output.get()); ::_exit(result==0 ? 0 : 1);
        }
        Worker worker(child); output=Fd(); std::uint64_t bytes=0,last=monotonic_ms();
        try {
            std::array<char,65536> buffer{};
            while(true) {
                struct pollfd ready{input.get(),POLLIN,0}; const int polled=::poll(&ready,1,1000); if(polled<0 && errno==EINTR)continue;
                require(polled>=0 && !(ready.revents&POLLNVAL),"io-error","Cannot observe native send data");
                if(monotonic_ms()-last>=1000) { progress_write(store,progress,bytes); last=monotonic_ms(); }
                if(polled==0)continue;
                const auto n=::read(input.get(),buffer.data(),buffer.size()); if(n<0 && errno==EINTR)continue;
                require(n>=0,"io-error","Cannot read native send data"); if(n==0)break;
                require(bytes<=static_cast<std::uint64_t>(INT64_MAX)-static_cast<std::uint64_t>(n),"size-limit","Send stream exceeds file size limits");
                struct statvfs free{}; require(::fstatvfs(partial.get(),&free)==0 && free.f_frsize>0 && free.f_bavail>=(1048576+buffer.size()+free.f_frsize-1)/free.f_frsize,
                    "insufficient-space","Btrfs store has less than the reserved free-space margin");
                write_all(partial.get(),std::string_view(buffer.data(),static_cast<std::size_t>(n))); bytes+=static_cast<std::uint64_t>(n);
            }
            worker.finish(); same_subvolume(source.get(),plan["source_identity"],true); if(previous.get()>=0)same_subvolume(previous.get(),plan["parent_identity"],true);
            require(::fsync(partial.get())==0,"io-error","Cannot sync completed send data"); const auto stream=btrfs_stream_check(partial.get()); stream_matches(stream,plan);
            progress["state"]="SEALED"; progress["stream"]=stream; progress_write(store,progress,bytes);
        } catch(...) { progress["state"]="FAILED"; try { progress_write(store,progress,bytes); } catch(...) {} throw; }
    }
    // The sealed checksum makes publication resumable without trusting partial data.
    Fd stream;
    if(store.exists("stream.bin")) {
        auto fd=store.open("stream.bin",O_RDONLY|O_NONBLOCK); const auto st=stat_fd(fd.get());
        require(S_ISREG(st.st_mode) && st.st_uid==::geteuid() && (st.st_mode&07777)==0600 && st.st_nlink<=2,"unsafe-btrfs-store","Unsafe published send stream");
        if(store.exists("partial.bin")) { auto partial=store.open("partial.bin",O_RDONLY|O_NONBLOCK); const auto p=stat_fd(partial.get());
            require(st.st_dev==p.st_dev && st.st_ino==p.st_ino && st.st_nlink==2,"unexpected-btrfs-stream","Published and staging stream identities differ");
            require(::unlinkat(store.fd(),"partial.bin",0)==0,"io-error","Cannot finish send publication"); }
        stream=std::move(fd);
    } else {
        stream=stream_file(store,"partial.bin"); const auto inspected=btrfs_stream_check(stream.get()); stream_matches(inspected,plan);
        require(json(inspected)==json(progress["stream"]),"btrfs-stream-corrupt","Sealed staging stream changed");
        require(::linkat(store.fd(),"partial.bin",store.fd(),"stream.bin",0)==0 && ::unlinkat(store.fd(),"partial.bin",0)==0,"io-error","Cannot publish send stream without replacement");
    }
    const auto inspected=btrfs_stream_check(stream.get()); stream_matches(inspected,plan);
    require(stat_fd(stream.get()).st_nlink==1 && json(inspected)==json(progress["stream"]),"btrfs-stream-corrupt","Published stream differs from its seal");
    require(::fsync(store.fd())==0,"io-error","Cannot sync send publication"); progress["state"]="COMPLETE"; store.save_record("state.json",progress,true);
    auto out=summary(plan,progress); out["data_verified"]=true; return out;
}
} // namespace ure
