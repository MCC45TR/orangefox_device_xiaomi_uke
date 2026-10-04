// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "operation_guard.hpp"
#include "tree_listing.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <map>
#include <openssl/evp.h>
#include <set>
#include <sys/file.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <linux/stat.h>

namespace ure {
namespace {
constexpr unsigned entry_limit=1000000, page_limit=8192;
constexpr std::size_t page_bytes=3*1024*1024, attr_limit=131072;
std::string hex(std::string_view bytes) {
    static constexpr char digits[]="0123456789abcdef";
    std::string out; out.reserve(bytes.size()*2);
    for(char byte:bytes) { const auto c=static_cast<unsigned char>(byte); out+=digits[c>>4]; out+=digits[c&15]; }
    return out;
}
std::string unhex(const Value& value,std::size_t limit) {
    require(value.isString(),"invalid-tree","Expected a hex-encoded byte string");
    const auto text=value.asString();
    require(text.size()%2==0 && text.size()/2<=limit,"invalid-tree","Encoded tree field exceeds its limit");
    auto nibble=[](char c) { if(c>='0' && c<='9')return c-'0'; if(c>='a' && c<='f')return c-'a'+10; throw Error("invalid-tree","Invalid hex byte"); };
    std::string out; out.reserve(text.size()/2);
    for(std::size_t i=0;i<text.size();i+=2)out+=static_cast<char>((nibble(text[i])<<4)|nibble(text[i+1]));
    return out;
}
class Hash {
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context_{EVP_MD_CTX_new(),EVP_MD_CTX_free};
public:
    Hash() { require(context_ && EVP_DigestInit_ex(context_.get(),EVP_sha256(),nullptr)==1,"hash-error","Cannot initialize tree hash"); }
    void add(std::string_view data) { require(EVP_DigestUpdate(context_.get(),data.data(),data.size())==1,"hash-error","Cannot update tree hash"); }
    std::string finish() { std::array<unsigned char,32> bytes{}; unsigned size=0;
        require(EVP_DigestFinal_ex(context_.get(),bytes.data(),&size)==1 && size==32,"hash-error","Cannot finish tree hash");
        return hex(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())); }
};
std::string content_hash(int fd,std::uint64_t bytes) {
    Hash hash; std::array<char,65536> buffer{}; std::uint64_t offset=0;
    while(offset<bytes) {
        const auto count=::pread(fd,buffer.data(),static_cast<std::size_t>(std::min(bytes-offset,static_cast<std::uint64_t>(buffer.size()))),static_cast<off_t>(offset));
        if(count<0 && errno==EINTR)continue;
        require(count>0,"truncated-source","Tree file ended before its size boundary"); hash.add(std::string_view(buffer.data(),static_cast<std::size_t>(count))); offset+=static_cast<std::uint64_t>(count);
    }
    return hash.finish();
}
bool cache_insert(std::set<std::string>& cache,const std::string& name) {
    if(cache.contains(name))return false;
    if(cache.size()>=65536)cache.clear();
    cache.insert(name); return true;
}
bool same(const struct stat& a,const struct stat& b) { return a.st_dev==b.st_dev && a.st_ino==b.st_ino; }
struct stat info(int fd) { struct stat st{}; require(::fstat(fd,&st)==0,"io-error","Cannot inspect tree descriptor"); return st; }
std::uint64_t mount_id(int fd) {
    struct statx st{};
    require(::syscall(SYS_statx,fd,"",AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW,STATX_MNT_ID,&st)==0 && (st.stx_mask&STATX_MNT_ID),
        "mount-identity-unavailable","Tree backups require kernel mount identity support"); return st.stx_mnt_id;
}
void root_gate(int fd) {
    struct statfs filesystem{};
    require(::fstatfs(fd,&filesystem)==0,"io-error","Cannot inspect tree filesystem");
    require(filesystem.f_type!=0x9fa0 && filesystem.f_type!=0x62656572 && filesystem.f_type!=0x73636673 &&
        filesystem.f_type!=0x27e0eb && filesystem.f_type!=0x64626720,"unsafe-tree-root","Kernel pseudo-filesystems cannot be tree backup roots");
    std::vector<std::pair<struct stat,bool>> protected_roots;
    for(const auto* path:{"/data","/metadata","/persist","/mnt/vendor/persist"}) {
        struct stat st{},parent{};
        if(::stat(path,&st)==0 && S_ISDIR(st.st_mode))protected_roots.emplace_back(st,
            ::stat((fs::path(path)/"..").c_str(),&parent)==0 && st.st_dev!=parent.st_dev);
    }
    Fd current(::fcntl(fd,F_DUPFD_CLOEXEC,0));
    require(current.get()>=0,"io-error","Cannot retain tree ancestry");
    for(unsigned depth=0;depth<256;++depth) {
        const auto st=info(current.get());
        for(const auto& [p,separate]:protected_roots)require(!same(st,p) && !(separate && st.st_dev==p.st_dev),
            "protected-tree-root","Android userdata, metadata and calibration trees require their own trust workflow");
        Fd parent(::openat(current.get(),"..",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
        require(parent.get()>=0,"io-error","Cannot inspect tree ancestor");
        if(same(st,info(parent.get())))return;
        current=std::move(parent);
    }
    throw Error("invalid-root","Tree ancestry exceeds its limit");
}
void outside(int source,int destination) {
    const auto origin=info(source); Fd current(::fcntl(destination,F_DUPFD_CLOEXEC,0));
    require(current.get()>=0,"io-error","Cannot retain destination ancestry");
    for(unsigned depth=0;depth<256;++depth) {
        const auto st=info(current.get()); require(!same(st,origin),"recursive-backup","Backup destination is inside the selected source tree");
        Fd parent(::openat(current.get(),"..",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
        require(parent.get()>=0,"io-error","Cannot inspect destination ancestry");
        if(same(st,info(parent.get())))return;
        current=std::move(parent);
    }
    throw Error("invalid-root","Destination ancestry exceeds its limit");
}
std::vector<std::string> parts(const std::string& path) {
    require(!path.empty() && path.size()<=4096 && path.front()!='/' && path.find('\0')==path.npos,"invalid-tree","Invalid relative tree path");
    if(path==".")return {};
    std::vector<std::string> out; std::size_t offset=0;
    while(offset<path.size()) {
        const auto slash=path.find('/',offset); auto piece=path.substr(offset,slash==path.npos ? path.npos : slash-offset);
        require(!piece.empty() && piece!="." && piece!=".." && piece.size()<=255,"invalid-tree","Invalid tree path component");
        out.push_back(piece); require(out.size()<=64,"size-limit","Tree path depth exceeds 64");
        if(slash==path.npos)break;
        offset=slash+1; require(offset<path.size(),"invalid-tree","Trailing tree path separator");
    } return out;
}
Fd open_tree(int root,const std::string& path,int flags,mode_t mode=0) {
    const auto names=parts(path); Fd current(::fcntl(root,F_DUPFD_CLOEXEC,0));
    require(current.get()>=0,"io-error","Cannot retain tree root");
    const auto mounted=mount_id(root);
    for(std::size_t i=0;i<names.size();++i) {
        const int opts=(i+1==names.size()) ? flags : O_RDONLY|O_DIRECTORY;
        int next=::openat(current.get(),names[i].c_str(),opts|O_CLOEXEC|O_NOFOLLOW,mode);
        if(next<0 && (opts&O_NOATIME) && errno==EPERM)next=::openat(current.get(),names[i].c_str(),(opts&~O_NOATIME)|O_CLOEXEC|O_NOFOLLOW,mode);
        require(next>=0,"path-unavailable","Tree path is unavailable or unsafe"); current=Fd(next);
        require(mount_id(current.get())==mounted && info(current.get()).st_dev==info(root).st_dev,"nested-mount","Select each nested mount or subvolume separately; tree traversal never crosses filesystem boundaries");
    } return current;
}
std::string proc_path(int parent,const std::string& name) { return "/proc/self/fd/"+std::to_string(parent)+"/"+name; }
Value attrs(int fd,const std::string& link_path={}) {
    auto list=[&](char* buffer,std::size_t size) { return link_path.empty() ? ::flistxattr(fd,buffer,size) : ::llistxattr(link_path.c_str(),buffer,size); };
    const auto n=list(nullptr,0); Value result(Json::objectValue);
    if(n<0 && (errno==ENOTSUP || errno==EOPNOTSUPP))return result;
    require(n>=0 && static_cast<std::size_t>(n)<=65536,"metadata-unavailable","Cannot enumerate all tree attributes");
    std::string names(static_cast<std::size_t>(n),'\0');
    require(list(names.data(),names.size())==n,"stale-source","Tree attributes changed during enumeration");
    std::size_t budget=0;
    for(std::size_t offset=0;offset<names.size();) {
        const auto end=names.find('\0',offset); require(end!=names.npos && end>offset,"metadata-unavailable","Malformed tree attribute list");
        const auto name=names.substr(offset,end-offset); offset=end+1;
        auto get=[&](void* buffer,std::size_t size) { return link_path.empty() ? ::fgetxattr(fd,name.c_str(),buffer,size) : ::lgetxattr(link_path.c_str(),name.c_str(),buffer,size); };
        const auto bytes=get(nullptr,0); require(bytes>=0 && static_cast<std::size_t>(bytes)<=65536,"metadata-unavailable","Cannot read complete tree attribute");
        budget+=name.size()+static_cast<std::size_t>(bytes); require(budget<=attr_limit,"size-limit","Tree attribute budget exceeded");
        std::string value(static_cast<std::size_t>(bytes),'\0'); require(get(value.data(),value.size())==bytes,"stale-source","Tree attribute changed during read");
        result[hex(name)]=hex(value);
    } return result;
}
Value stamp(const struct stat& st) {
    Value out; out["device"]=Json::UInt64(st.st_dev); out["inode"]=Json::UInt64(st.st_ino); out["links"]=Json::UInt64(st.st_nlink);
    out["uid"]=Json::UInt(st.st_uid); out["gid"]=Json::UInt(st.st_gid); out["mode"]=Json::UInt(st.st_mode&07777);
    out["mtime_seconds"]=Json::Int64(st.st_mtim.tv_sec); out["mtime_nanoseconds"]=Json::Int64(st.st_mtim.tv_nsec);
    out["ctime_seconds"]=Json::Int64(st.st_ctim.tv_sec); out["ctime_nanoseconds"]=Json::Int64(st.st_ctim.tv_nsec);
    out["kind"]=S_ISREG(st.st_mode) ? "file" : S_ISDIR(st.st_mode) ? "directory" : S_ISLNK(st.st_mode) ? "symlink" :
        S_ISFIFO(st.st_mode) ? "fifo" : S_ISCHR(st.st_mode) ? "char" : S_ISBLK(st.st_mode) ? "block" : S_ISSOCK(st.st_mode) ? "socket" : "unknown";
    out["bytes"]=Json::UInt64(S_ISREG(st.st_mode) && st.st_size>=0 ? static_cast<std::uint64_t>(st.st_size) : 0);
    out["rdev"]=Json::UInt64(st.st_rdev); return out;
}
std::string listing_hash(SortedTreeDirectory& names) {
    Hash hash; std::string name; names.rewind(); while(names.next(name))hash.add(hex(name)+"\n"); names.rewind(); return hash.finish();
}
Value extents(int fd,std::uint64_t bytes) {
    Value out(Json::arrayValue); std::uint64_t offset=0;
    while(offset<bytes) {
        errno=0; const auto data=::lseek(fd,static_cast<off_t>(offset),SEEK_DATA);
        if(data<0 && errno==ENXIO)break;
        if(data<0 && (errno==EINVAL || errno==ENOTSUP)) { out.clear(); Value e; e["offset"]=Json::UInt64(0); e["bytes"]=Json::UInt64(bytes); out.append(e); return out; }
        require(data>=0 && static_cast<std::uint64_t>(data)>=offset && static_cast<std::uint64_t>(data)<bytes,"io-error","Invalid sparse data range");
        const auto hole=::lseek(fd,data,SEEK_HOLE);
        require(hole>data,"io-error","Invalid sparse hole range");
        const auto end=std::min(bytes,static_cast<std::uint64_t>(hole)); Value e;
        e["offset"]=Json::UInt64(static_cast<std::uint64_t>(data)); e["bytes"]=Json::UInt64(end-static_cast<std::uint64_t>(data));
        out.append(e); require(out.size()<=16384,"size-limit","File sparse extent count exceeds its limit"); offset=end;
    } return out;
}
Value entry(int root,const std::string& path,bool content,TreeListingBudget& budget,int scratch,SortedTreeDirectory* prepared=nullptr) {
    auto fd=open_tree(root,path,O_PATH|O_NONBLOCK); const auto st=info(fd.get()); Value out=stamp(st); out["path_hex"]=hex(path);
    require(out["kind"]!="unknown","unsupported-tree-entry","Unknown file type in source tree");
    if(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) {
        auto data=open_tree(root,path,O_RDONLY|O_NONBLOCK|O_NOATIME|(S_ISDIR(st.st_mode) ? O_DIRECTORY : 0));
        require(json(stamp(info(data.get())))==json(stamp(st)),"stale-source","Tree entry changed while opening"); out["xattrs"]=attrs(data.get());
        if(S_ISREG(st.st_mode) && content) { require(st.st_size>=0,"invalid-file","Tree files require a finite size"); out["sha256"]=content_hash(data.get(),out["bytes"].asUInt64()); out["extents"]=extents(data.get(),out["bytes"].asUInt64()); }
        if(S_ISDIR(st.st_mode)) {
            if(prepared)out["children_sha256"]=listing_hash(*prepared);
            else { SortedTreeDirectory names(data.get(),scratch,budget); out["children_sha256"]=listing_hash(names); }
        }
        require(json(stamp(info(data.get())))==json(stamp(st)),"stale-source","Tree entry changed during inspection");
    } else {
        const fs::path relative(path); auto parent=open_tree(root,relative.parent_path().empty() ? "." : relative.parent_path().string(),O_RDONLY|O_DIRECTORY);
        out["xattrs"]=attrs(-1,proc_path(parent.get(),relative.filename().string()));
        if(S_ISLNK(st.st_mode)) {
            std::array<char,4097> buffer{}; const auto n=::readlinkat(fd.get(),"",buffer.data(),4096);
            require(n>=0 && n<4096,"invalid-link","Cannot read bounded symlink target"); out["target_hex"]=hex(std::string_view(buffer.data(),static_cast<std::size_t>(n)));
        }
    }
    auto again=open_tree(root,path,O_PATH|O_NONBLOCK); require(json(stamp(info(again.get())))==json(stamp(st)),"stale-source","Tree entry was replaced"); return out;
}
std::string seal(Value value,const char* field="plan_sha256") { value.removeMember(field); return sha256(json(value)); }
Value read_record(const Root& store,const std::string& name) {
    const auto st=store.stat(name); require(S_ISREG(st.st_mode) && st.st_uid==::geteuid() && st.st_nlink==1 && (st.st_mode&07777)==0600,
        "unsafe-tree-store","Tree records must be private single-link regular files"); return parse_json(store.read(name,4*1024*1024));
}
std::string page_name(unsigned index) { return "entries-"+std::to_string(index)+".json"; }
void check_plan(const Value& plan) {
    require(plan["schema"]==1 && plan["format"]=="ure-tree-backup" && plan["operation_id"].isString() && identifier(plan["operation_id"].asString()) &&
        plan["firmware_profile"].isString() && identifier(plan["firmware_profile"].asString()) && plan["pages"].isArray() &&
        !plan["pages"].empty() && plan["pages"].size()<=page_limit && plan["entries"].isUInt() && plan["entries"].asUInt()>0 &&
        plan["entries"].asUInt()<=entry_limit && plan["logical_bytes"].isUInt64() && plan["stored_data_bytes"].isUInt64() &&
        plan["runtime_sockets"].isUInt() && plan["runtime_sockets"].asUInt()<=plan["entries"].asUInt() &&
        plan["source_identity"].isObject() && plan["source_mount_id"].isUInt64() && plan["selected_path"].isString() &&
        plan["plan_sha256"].isString() && hash_valid(plan["plan_sha256"].asString()) && plan["plan_sha256"].asString()==seal(plan) &&
        plan["atomic_snapshot"]==false && plan["compression"]=="none","invalid-tree","Invalid sealed tree backup plan");
    components(plan["selected_path"].asString());
    for(const auto& p:plan["pages"])require(p.isString() && hash_valid(p.asString()),"invalid-tree","Invalid tree page hash");
}
Value summary(const Value& plan,const std::string& state) {
    Value out=plan; out.removeMember("pages"); out["page_count"]=plan["pages"].size(); out["state"]=state;
    out["verified"]=state=="COMPLETE"; out["physical_test_record"]=false; return out;
}
Value page(const Root& store,const Value& plan,unsigned index) {
    auto result=read_record(store,page_name(index));
    require(result.isArray() && !result.empty() && result.size()<=256 && sha256(json(result))==plan["pages"][index].asString(),"tree-corrupt","Tree page content or hash differs"); return result;
}
std::string path_of(const Value& e) { auto path=unhex(e["path_hex"],4096); parts(path); return path; }
void schema_entry(const Value& e) {
    const auto path=path_of(e); (void)path;
    require(e["kind"].isString() && e["uid"].isUInt() && e["gid"].isUInt() && e["mode"].isUInt() && e["mode"].asUInt()<=07777 &&
        e["device"].isUInt64() && e["inode"].isUInt64() && e["links"].isUInt64() && e["bytes"].isUInt64() && e["bytes"].asUInt64()<=INT64_MAX && e["rdev"].isUInt64() &&
        e["mtime_seconds"].isInt64() && e["mtime_nanoseconds"].isInt64() && e["mtime_nanoseconds"].asInt64()>=0 && e["mtime_nanoseconds"].asInt64()<1000000000 &&
        e["xattrs"].isObject(),"invalid-tree","Invalid tree metadata");
    std::size_t budget=0;
    for(const auto& encoded:e["xattrs"].getMemberNames()) {
        const auto name=unhex(Value(encoded),255),value=unhex(e["xattrs"][encoded],65536);
        require(!name.empty() && name.find('\0')==name.npos,"invalid-tree","Invalid extended attribute name"); budget+=name.size()+value.size();
    }
    require(budget<=attr_limit,"size-limit","Tree attribute budget exceeded");
    const auto kind=e["kind"].asString();
    require(kind=="file" || kind=="directory" || kind=="symlink" || kind=="fifo" || kind=="char" || kind=="block" || kind=="socket","invalid-tree","Invalid entry kind");
    if(kind=="directory")require(e["children_sha256"].isString() && hash_valid(e["children_sha256"].asString()),"invalid-tree","Invalid directory namespace hash");
    if(kind=="symlink") { const auto target=unhex(e["target_hex"],4096); require(!target.empty() && target.find('\0')==target.npos,"invalid-tree","Invalid symlink target"); }
    if(kind=="file") {
        require(e["sha256"].isString() && hash_valid(e["sha256"].asString()) && e["extents"].isArray() && e["extents"].size()<=16384,"invalid-tree","Invalid file content description");
        std::uint64_t end=0;
        for(const auto& extent:e["extents"]) {
            require(extent["offset"].isUInt64() && extent["bytes"].isUInt64(),"invalid-tree","Invalid sparse extent");
            const auto offset=extent["offset"].asUInt64(),bytes=extent["bytes"].asUInt64();
            require(offset>=end && bytes>0 && offset<e["bytes"].asUInt64() && bytes<=e["bytes"].asUInt64()-offset,"invalid-tree","Sparse extents overlap or exceed file"); end=offset+bytes;
        }
        if(e.isMember("hardlink_hex"))parts(unhex(e["hardlink_hex"],4096));
    }
}
void copy_extents(int source,int target,const Value& e) {
    require(::ftruncate(target,static_cast<off_t>(e["bytes"].asUInt64()))==0,"io-error","Cannot size sparse backup file");
    std::array<char,65536> buffer{};
    for(const auto& extent:e["extents"]) {
        auto offset=extent["offset"].asUInt64(),left=extent["bytes"].asUInt64();
        while(left) {
            const auto amount=static_cast<std::size_t>(std::min(left,static_cast<std::uint64_t>(buffer.size())));
            const auto n=::pread(source,buffer.data(),amount,static_cast<off_t>(offset)); if(n<0 && errno==EINTR)continue;
            require(n>0,"truncated-source","Tree file ended during capture"); std::size_t written=0;
            while(written<static_cast<std::size_t>(n)) {
                const auto count=::pwrite(target,buffer.data()+written,static_cast<std::size_t>(n)-written,static_cast<off_t>(offset+written));
                if(count<0 && errno==EINTR)continue;
                require(count>0,"io-error","Tree data write failed"); written+=static_cast<std::size_t>(count);
            }
            offset+=static_cast<std::uint64_t>(n); left-=static_cast<std::uint64_t>(n);
        }
    }
}
std::string blob_name(const Value& e) { return "data-"+e["sha256"].asString()+".bin"; }
void verified_blob(const Root& store,const Value& e) {
    auto data=store.open(blob_name(e),O_RDONLY|O_NONBLOCK); const auto st=info(data.get());
    require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode&07777)==0600 && st.st_size>=0 &&
        static_cast<std::uint64_t>(st.st_size)==e["bytes"].asUInt64() && content_hash(data.get(),e["bytes"].asUInt64())==e["sha256"].asString(),"tree-corrupt","Tree data blob fails size, privacy or checksum verification");
}
void all_entries(const Root& store,const Value& plan,const std::function<void(const Value&)>& callback) {
    struct Directory { std::string path,last,expected; Hash children; };
    std::vector<Directory> stack; std::map<std::string,std::string> linked_files;
    unsigned count=0,sockets=0; std::uint64_t logical=0,stored=0; std::size_t link_budget=0;
    const auto finish_directory=[&]() { require(stack.back().children.finish()==stack.back().expected,"invalid-tree","Archived directory names differ from their namespace hash"); stack.pop_back(); };
    for(unsigned index=0;index<plan["pages"].size();++index)for(const auto& e:page(store,plan,index)) {
        schema_entry(e); const auto path=path_of(e),kind=e["kind"].asString();
        require(++count<=entry_limit,"size-limit","Tree entry limit exceeded");
        if(count==1)require(path=="." && kind=="directory","invalid-tree","Tree must begin with its root directory");
        else {
            const auto directory=fs::path(path).parent_path().string(),parent=directory.empty() ? std::string(".") : directory,name=fs::path(path).filename().string();
            while(!stack.empty() && stack.back().path!=parent)finish_directory();
            require(!stack.empty() && (stack.back().last.empty() || name>stack.back().last),"invalid-tree","Tree paths must have unique sorted siblings and depth-first parents");
            stack.back().last=name; stack.back().children.add(hex(name)+"\n");
        }
        if(kind=="file" && e["links"].asUInt64()>1) {
            auto shared=e; shared.removeMember("path_hex"); shared.removeMember("hardlink_hex"); const auto digest=sha256(json(shared));
            if(e.isMember("hardlink_hex")) { const auto link=unhex(e["hardlink_hex"],4096);
                require(linked_files.contains(link) && linked_files[link]==digest,"invalid-tree","Hardlink source content, identity and metadata must match an earlier file"); }
            if(!e.isMember("hardlink_hex")) { link_budget+=path.size()+192; require(link_budget<=64*1024*1024,"size-limit","Hardlink index exceeds 64 MiB"); linked_files[path]=digest; }
        } else require(!e.isMember("hardlink_hex"),"invalid-tree","Hardlink record is not a multiply linked file");
        if(kind=="directory") { require(stack.size()<65,"size-limit","Directory depth exceeds its limit"); stack.push_back({path,"",e["children_sha256"].asString(),Hash{}}); }
        if(kind=="socket")++sockets;
        if(kind=="file" && !e.isMember("hardlink_hex"))for(const auto& extent:e["extents"]) {
            require(stored<=UINT64_MAX-extent["bytes"].asUInt64(),"size-limit","Tree extent sum overflows"); stored+=extent["bytes"].asUInt64();
        }
        require(logical<=UINT64_MAX-e["bytes"].asUInt64(),"size-limit","Tree byte count overflows"); logical+=e["bytes"].asUInt64(); callback(e);
    }
    while(!stack.empty())finish_directory();
    require(count==plan["entries"].asUInt() && logical==plan["logical_bytes"].asUInt64() && stored==plan["stored_data_bytes"].asUInt64() &&
        sockets==plan["runtime_sockets"].asUInt(),"invalid-tree","Tree summary differs from its entries");
}
Fd lock(const Root& store) {
    auto fd=store.open("lock",O_RDWR|O_CREAT,0600); const auto st=info(fd.get());
    require(st.st_uid==::geteuid() && st.st_nlink==1 && S_ISREG(st.st_mode) && (st.st_mode&07777)==0600 &&
        ::flock(fd.get(),LOCK_EX|LOCK_NB)==0,"busy-tree-store","Tree store is unsafe or already locked"); return fd;
}
Root selected(const Root& context,const Value& plan) {
    auto fd=context.open(plan["selected_path"].asString(),O_RDONLY|O_DIRECTORY); root_gate(fd.get());
    require(json(stamp(info(fd.get())))==json(plan["source_identity"]) && mount_id(fd.get())==plan["source_mount_id"].asUInt64(),"stale-source","Selected tree root changed"); return Root(std::move(fd));
}
void source_match(int root,const Value& e,TreeListingBudget& budget,int scratch) {
    auto expected=e; expected.removeMember("hardlink_hex");
    expected.removeMember("sha256"); expected.removeMember("extents");
    auto current=entry(root,path_of(e),false,budget,scratch);
    require(json(current)==json(expected),"stale-source","Tree namespace, content or metadata changed since planning");
}
}
void filesystem_tree_gate(int fd) { root_gate(fd); }
void filesystem_tree_outside(int source,int destination) { outside(source,destination); }
Value descriptor_identity(int fd) {
    const auto st=info(fd); Value out;
    out["device"]=Json::UInt64(st.st_dev); out["inode"]=Json::UInt64(st.st_ino);
    out["mount_id"]=Json::UInt64(mount_id(fd)); return out;
}
Value backup_tree_plan(const Root& context,const std::string& relative,const std::string& profile,const fs::path& destination) {
    require(identifier(profile),"invalid-profile","A firmware profile is required");
    auto source=context.open(relative,O_RDONLY|O_DIRECTORY); root_gate(source.get());
    Root parent(destination.parent_path().empty() ? fs::path(".") : destination.parent_path()); root_gate(parent.fd()); outside(source.get(),parent.fd());
    const auto initial=stamp(info(source.get()));
    Value admission; admission["operation_id"]=operation_id(); admission["operation"]="tree.backup.plan";
    admission["source_identity"]=initial; admission["selected_path"]=relative; admission["firmware_profile"]=profile;
    ManagedOperation operation(operation_binding("tree.backup.plan",admission,destination,
        operation_targets(operation_target(source.get(),"tree-source"))));
    require(json(stamp(info(source.get())))==json(initial),"stale-source","Source changed before tree planning admission");
    auto store=private_directory(destination,true); auto held=lock(store);
    Value plan; plan["schema"]=1; plan["format"]="ure-tree-backup"; plan["operation_id"]=admission["operation_id"]; plan["created_utc"]=utc();
    plan["firmware_profile"]=profile; plan["firmware_identity_validated"]=false; plan["selected_path"]=relative; plan["source_identity"]=initial;
    plan["source_mount_id"]=Json::UInt64(mount_id(source.get())); struct statfs filesystem{};
    require(::fstatfs(source.get(),&filesystem)==0,"io-error","Cannot inspect source filesystem"); plan["filesystem_magic"]=Json::Int64(filesystem.f_type);
    plan["compression"]="none"; plan["sparse_representation"]="reported-data-extents"; plan["atomic_snapshot"]=false;
    plan["coherence"]="unchanged namespace, content and metadata observed; not a filesystem snapshot";
    plan["encryption_state"]="already-accessible-filesystem; no unlock performed"; plan["private_record"]=true;
    plan["tool"]="libuke-recovery/tree-v1"; plan["pages"]=Value(Json::arrayValue);
    Value records(Json::arrayValue); unsigned count=0,sockets=0; std::uint64_t bytes=0,stored=0; std::size_t link_budget=0;
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::string> hardlinks;
    auto flush=[&]() { if(records.empty())return; require(plan["pages"].size()<page_limit && json(records).size()<=page_bytes,"size-limit","Tree page count or size exceeds its limit");
        const auto index=plan["pages"].size(); store.save_record(page_name(index),records); plan["pages"].append(sha256(json(records))); records=Value(Json::arrayValue); };
    struct Frontier { std::string path,expected; SortedTreeDirectory names; };
    TreeListingBudget enumeration;
    // Precharge the complete bounded frontier before its allocation. Directory
    // metadata is retained as a digest, never as an ancestor JSON/name vector.
    TreeListingMemory frontier_memory(enumeration,65*(sizeof(Frontier)+2*(4097+65)));
    std::vector<Frontier> frontier; frontier.reserve(65);
    const auto visit=[&](const std::string& path) {
        require(count<entry_limit,"size-limit","Tree exceeds one million entries"); ++count;
        auto current=open_tree(source.get(),path,O_PATH|O_NONBLOCK);
        require(!same(info(current.get()),info(store.fd())),"recursive-backup","Source traversal reached its own backup directory");
        const auto observed=stamp(info(current.get())); const bool is_directory=observed["kind"]=="directory"; SortedTreeDirectory names;
        if(is_directory) { auto directory=open_tree(source.get(),path,O_RDONLY|O_DIRECTORY); root_gate(directory.get()); names=SortedTreeDirectory(directory.get(),store.fd(),enumeration,true); }
        auto e=entry(source.get(),path,true,enumeration,store.fd(),is_directory ? &names : nullptr);
        for(const auto& key:observed.getMemberNames())require(json(observed[key])==json(e[key]),"stale-source","Tree entry changed before its planned listing was inspected");
        require(bytes<=UINT64_MAX-e["bytes"].asUInt64(),"size-limit","Tree byte count overflows"); bytes+=e["bytes"].asUInt64();
        if(e["kind"]=="file") {
            const auto key=std::make_pair(e["device"].asUInt64(),e["inode"].asUInt64());
            if(e["links"].asUInt64()>1 && hardlinks.contains(key))e["hardlink_hex"]=hex(hardlinks[key]);
            else { if(e["links"].asUInt64()>1) { link_budget+=path.size()+192; require(link_budget<=64*1024*1024,"size-limit","Hardlink index exceeds 64 MiB"); hardlinks[key]=path; } for(const auto& extent:e["extents"]) {
                require(stored<=UINT64_MAX-extent["bytes"].asUInt64(),"size-limit","Stored tree byte count overflows"); stored+=extent["bytes"].asUInt64(); } }
        }
        if(e["kind"]=="socket")++sockets;
        if(!records.empty() && (records.size()>=256 || json(records).size()+json(e).size()*2+256>page_bytes))flush();
        require(json(e).size()<page_bytes,"size-limit","Tree entry exceeds page size"); records.append(e);
        if(is_directory) { require(frontier.size()<65,"size-limit","Tree directory depth exceeds its limit"); frontier.push_back({path,sha256(json(e)),std::move(names)}); }
        else source_match(source.get(),e,enumeration,store.fd());
    };
    visit(".");
    while(!frontier.empty()) {
        std::string child;
        if(frontier.back().names.next(child)) {
            const auto& parent=frontier.back().path;
            require(parent=="." || parent.size()+1+child.size()<=4096,"size-limit","Tree path exceeds its byte limit");
            const auto path=parent=="." ? child : parent+"/"+child; visit(path);
        } else {
            const auto& finished=frontier.back();
            require(sha256(json(entry(source.get(),finished.path,false,enumeration,store.fd())))==finished.expected,
                "stale-source","Tree directory changed while its descendants were planned"); frontier.pop_back();
        }
    }
    flush(); require(json(stamp(info(source.get())))==json(initial),"stale-source","Source root changed during planning");
    require(enumeration.admitted_entries==count && enumeration.scratch==0,"stale-source","Tree enumeration did not complete its exact admitted frontier");
    plan["entries"]=count; plan["logical_bytes"]=Json::UInt64(bytes); plan["stored_data_bytes"]=Json::UInt64(stored); plan["runtime_sockets"]=sockets;
    plan["restore_requirements"]="new destination; Unix metadata support; required ownership/ACL/xattr privileges; runtime sockets recreated by services";
    plan["enumeration"]["working_memory_limit_bytes"]=Json::UInt64(TreeListingBudget::memory_limit);
    plan["enumeration"]["peak_accounted_working_bytes"]=Json::UInt64(enumeration.peak_memory);
    plan["enumeration"]["scratch_limit_bytes"]=Json::UInt64(TreeListingBudget::scratch_limit);
    plan["enumeration"]["peak_scratch_bytes"]=Json::UInt64(enumeration.peak_scratch);
    plan["enumeration"]["listing_order"]="sorted-bytewise-depth-first";
    plan["plan_sha256"]=seal(plan); store.save_record("plan.json",plan);
    held=Fd(); return operation.finish(summary(plan,"PLANNED"),true,true,"COMPLETE");
}
Value backup_tree_capture(const Root& context,const fs::path& directory,const std::string& confirmation) {
    auto store=private_directory(directory,false); root_gate(store.fd()); const auto plan=read_record(store,"plan.json"); check_plan(plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the reviewed tree plan SHA-256");
    auto source=selected(context,plan); outside(source.fd(),store.fd());
    ManagedOperation operation(operation_binding("tree.backup",plan,directory,
        operation_targets(operation_target(source.fd(),"tree-source"))));
    auto held=lock(store);
    require(json(read_record(store,"plan.json"))==json(plan),"stale-tree-plan","Tree plan changed during capture admission"); selected(context,plan);
    TreeListingBudget enumeration;
    all_entries(store,plan,[&](const Value& e) { source_match(source.fd(),e,enumeration,store.fd()); });
    Value state; state["plan_sha256"]=plan["plan_sha256"]; state["state"]="CAPTURING"; state["completed_files"]=0; store.save_record("state.json",state,true);
    unsigned completed=0,processed_entries=0; std::set<std::string> verified;
    try {
        all_entries(store,plan,[&](const Value& e) {
            ++processed_entries;
            source_match(source.fd(),e,enumeration,store.fd()); if(e["kind"]!="file")return;
            const auto blob=blob_name(e); if(verified.contains(blob))return;
            if(store.exists(blob)) {
                if(store.exists("partial.bin") && same(store.stat(blob),store.stat("partial.bin"))) {
                    const auto st=store.stat(blob); require(st.st_nlink==2 && st.st_uid==::geteuid() && (st.st_mode&07777)==0600 && S_ISREG(st.st_mode),"unsafe-tree-store","Unexpected linked partial blob");
                    require(::unlinkat(store.fd(),"partial.bin",0)==0 && ::fsync(store.fd())==0,"io-error","Cannot finish interrupted tree publication");
                }
                verified_blob(store,e);
            }
            else {
                // An interrupted private partial blob is disposable. Never overwrite a published blob.
                const auto temporary="partial.bin";
                if(store.exists(temporary)) { const auto st=store.stat(temporary); require(S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode&07777)==0600,"unsafe-tree-store","Unsafe partial tree blob"); require(::unlinkat(store.fd(),temporary,0)==0,"io-error","Cannot remove interrupted tree blob"); }
                struct statvfs space{}; require(::fstatvfs(store.fd(),&space)==0 && space.f_frsize>0,"io-error","Cannot inspect tree destination space");
                std::uint64_t needed=16*1024*1024; for(const auto& extent:e["extents"]) { require(needed<=UINT64_MAX-extent["bytes"].asUInt64(),"size-limit","Tree space budget overflows"); needed+=extent["bytes"].asUInt64(); }
                require(space.f_bavail>=needed/space.f_frsize+(needed%space.f_frsize!=0),"no-space","Insufficient tree backup space");
                auto output=store.open(temporary,O_RDWR|O_CREAT|O_EXCL,0600); auto input=open_tree(source.fd(),path_of(e),O_RDONLY|O_NONBLOCK|O_NOATIME);
                copy_extents(input.get(),output.get(),e); source_match(source.fd(),e,enumeration,store.fd());
                require(content_hash(output.get(),e["bytes"].asUInt64())==e["sha256"].asString(),"stale-source","Captured tree content differs from the plan");
                require(::fsync(output.get())==0 && ::linkat(store.fd(),temporary,store.fd(),blob.c_str(),0)==0 && ::unlinkat(store.fd(),temporary,0)==0 && ::fsync(store.fd())==0,"io-error","Cannot publish verified tree data");
            }
            cache_insert(verified,blob); state["completed_files"]=++completed; state["completed_entries"]=processed_entries; store.save_record("state.json",state,true);
        });
        all_entries(store,plan,[&](const Value& e) { source_match(source.fd(),e,enumeration,store.fd()); }); selected(context,plan);
        state["state"]="COMPLETE"; state["verified"]=true; state["completed_entries"]=plan["entries"]; store.save_record("state.json",state,true);
        held=Fd(); return operation.finish(summary(plan,"COMPLETE"),true,true);
    } catch(...) { state["state"]="INCOMPLETE"; state["verified"]=false; try { store.save_record("state.json",state,true); } catch(...) {} throw; }
}
Value backup_tree_verify(const fs::path& directory) {
    auto store=private_directory(directory,false); const auto plan=read_record(store,"plan.json"); check_plan(plan);
    const auto state=read_record(store,"state.json"); require(state["state"]=="COMPLETE" && state["verified"]==true && state["plan_sha256"]==plan["plan_sha256"],"incomplete-tree","Tree capture has not completed");
    std::set<std::string> verified;
    all_entries(store,plan,[&](const Value& e) { if(e["kind"]=="file" && cache_insert(verified,blob_name(e)))verified_blob(store,e); }); return summary(plan,"COMPLETE");
}
Value backup_tree_inspect(const fs::path& directory) {
    // Pages are immutable and state is replaced atomically. A reader can inspect
    // progress while capture owns the writer lock without opening any data blob.
    auto store=private_directory(directory,false); const auto plan=read_record(store,"plan.json"); check_plan(plan);
    all_entries(store,plan,[](const Value&) {});
    std::string phase="PLANNED"; Value progress;
    if(store.exists("state.json")) {
        const auto state=read_record(store,"state.json"); require(state["plan_sha256"]==plan["plan_sha256"] &&
            (state["state"]=="CAPTURING" || state["state"]=="INCOMPLETE" || state["state"]=="COMPLETE"),"invalid-tree","Tree state is not bound to its plan");
        phase=state["state"].asString(); progress=state["completed_entries"];
    }
    auto result=summary(plan,phase); result["verified"]=false; result["metadata_pages_verified"]=true; result["data_verified"]=false; result["completed_entries"]=progress; return result;
}
namespace {
Fd parent_of(int root,const std::string& path) {
    const auto parent=fs::path(path).parent_path().string(); return open_tree(root,parent.empty() ? "." : parent,O_RDONLY|O_DIRECTORY);
}
void apply_metadata(int root,const Value& e) {
    const auto path=path_of(e),kind=e["kind"].asString(); auto parent=parent_of(root,path);
    const auto name=path=="." ? std::string(".") : fs::path(path).filename().string();
    auto fd=open_tree(root,path,(kind=="file" || kind=="directory") ? O_RDONLY|O_NONBLOCK|(kind=="directory" ? O_DIRECTORY : 0) : O_PATH|O_NONBLOCK);
    const auto before=info(fd.get());
    if(before.st_uid!=e["uid"].asUInt() || before.st_gid!=e["gid"].asUInt())
        require(::fchownat(parent.get(),name.c_str(),static_cast<uid_t>(e["uid"].asUInt()),static_cast<gid_t>(e["gid"].asUInt()),AT_SYMLINK_NOFOLLOW)==0,"metadata-error","Cannot restore tree ownership");
    if(kind!="symlink")require(::fchmodat(parent.get(),name.c_str(),static_cast<mode_t>(e["mode"].asUInt()),0)==0,"metadata-error","Cannot restore tree permissions");
    const auto link_path=proc_path(parent.get(),name);
    // Remove inherited ACL/default/security attributes absent from the archive.
    const auto current=(kind=="file" || kind=="directory") ? attrs(fd.get()) : attrs(-1,link_path);
    for(const auto& encoded:current.getMemberNames())if(!e["xattrs"].isMember(encoded)) {
        const auto attr=unhex(Value(encoded),255);
        require(((kind=="file" || kind=="directory") ? ::fremovexattr(fd.get(),attr.c_str()) : ::lremovexattr(link_path.c_str(),attr.c_str()))==0,"metadata-error","Cannot remove inherited tree attribute");
    }
    for(const auto& encoded:e["xattrs"].getMemberNames()) {
        const auto attr=unhex(Value(encoded),255),value=unhex(e["xattrs"][encoded],65536);
        require(((kind=="file" || kind=="directory") ? ::fsetxattr(fd.get(),attr.c_str(),value.data(),value.size(),0) : ::lsetxattr(link_path.c_str(),attr.c_str(),value.data(),value.size(),0))==0,
            "metadata-error","Cannot restore complete tree attributes");
    }
    const struct timespec times[2]{{0,UTIME_OMIT},{static_cast<time_t>(e["mtime_seconds"].asInt64()),static_cast<long>(e["mtime_nanoseconds"].asInt64())}};
    require(::utimensat(parent.get(),name.c_str(),times,AT_SYMLINK_NOFOLLOW)==0,"metadata-error","Cannot restore tree modification time");
    const auto after=stamp(info(fd.get()));
    for(const auto* key:{"uid","gid","mode","mtime_seconds","mtime_nanoseconds"})require(json(after[key])==json(e[key]),"metadata-error",std::string("Tree metadata readback differs: ")+key);
    require(json((kind=="file" || kind=="directory") ? attrs(fd.get()) : attrs(-1,link_path))==json(e["xattrs"]),"metadata-error","Tree attribute readback differs");
    if(kind=="file" || kind=="directory")require(::fsync(fd.get())==0,"io-error","Cannot sync restored tree metadata");
    else require(::fsync(parent.get())==0,"io-error","Cannot sync restored special entry");
}
void verify_restored(const Root& store,const Value& plan,int root) {
    TreeListingBudget enumeration;
    struct Namespace { std::string path; Hash expected; };
    std::vector<Namespace> directories;
    const auto finish_directory=[&] {
        auto directory=open_tree(root,directories.back().path,O_RDONLY|O_DIRECTORY); Hash observed;
        SortedTreeDirectory names(directory.get(),store.fd(),enumeration); std::string name;
        while(names.next(name))observed.add(hex(name)+"\n");
        require(observed.finish()==directories.back().expected.finish(),"restore-verification-error",
            "Restored directory contains missing or unarchived entries"); directories.pop_back();
    };
    all_entries(store,plan,[&](const Value& e) {
        const auto path=path_of(e),kind=e["kind"].asString();
        if(path!=".") {
            const auto containing=fs::path(path).parent_path().string(),parent=containing.empty() ? std::string(".") : containing;
            while(!directories.empty() && directories.back().path!=parent)finish_directory();
            require(!directories.empty(),"invalid-tree","Archived restore entry has no retained parent namespace");
            if(kind!="socket")directories.back().expected.add(hex(fs::path(path).filename().string())+"\n");
        }
        if(kind=="socket") {
            auto parent=parent_of(root,path); struct stat omitted{};
            require(::fstatat(parent.get(),fs::path(path).filename().c_str(),&omitted,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT,
                "restore-verification-error","Runtime socket unexpectedly exists in the restored tree"); return;
        }
        const auto current=entry(root,path,true,enumeration,store.fd());
        if(kind=="directory")directories.push_back({path,Hash{}});
        for(const auto* key:{"kind","uid","gid","mode","bytes","rdev","mtime_seconds","mtime_nanoseconds","xattrs"})
            require(json(current[key])==json(e[key]),"restore-verification-error",std::string("Restored entry readback differs: ")+key);
        if(kind=="file") {
            require(current["sha256"]==e["sha256"],"restore-verification-error","Restored file content differs from its archived hash");
            if(e.isMember("hardlink_hex")) {
                auto first=open_tree(root,unhex(e["hardlink_hex"],4096),O_PATH|O_NONBLOCK),linked=open_tree(root,path,O_PATH|O_NONBLOCK);
                require(same(info(first.get()),info(linked.get())),"restore-verification-error","Restored hardlink identity differs");
            }
        } else if(kind=="symlink")require(current["target_hex"]==e["target_hex"],"restore-verification-error","Restored symlink target differs");
    });
    while(!directories.empty())finish_directory();
}
std::string restore_plan_name(const Value& plan) { return "restore-plan-"+plan["operation_id"].asString()+".json"; }
std::string restore_state_name(const Value& plan) { return "restore-state-"+plan["operation_id"].asString()+".json"; }
bool retained_location(const Value& identity) {
    return identity.isObject() && identity.size()==3 && identity["device"].isUInt64() &&
        identity["inode"].isUInt64() && identity["mount_id"].isUInt64();
}
void checked_restore_paths(const Root& store,const Root& parent,const fs::path& directory,const fs::path& destination) {
    auto named_store=private_directory(directory,false);
    require(json(descriptor_identity(named_store.fd()))==json(descriptor_identity(store.fd())),
        "wrong-tree-restore-journal","Restore journal pathname no longer names its retained directory");
    Root named_parent(destination.parent_path().empty() ? fs::path(".") : destination.parent_path());
    require(json(descriptor_identity(named_parent.fd()))==json(descriptor_identity(parent.fd())),
        "wrong-tree-restore-target","Restore destination parent pathname no longer names its retained directory");
}
void save_restore_state(const Root& store,Value& state,const std::string& phase,bool replace=true) {
    state["state"]=phase; state["updated_at"]=utc(); state["state_sha256"]=seal(state,"state_sha256");
    store.save_record(restore_state_name(state),state,replace);
}
Value checked_restore_state(const Root& store,const Value& plan) {
    const auto state=read_record(store,restore_state_name(plan));
    require(state["schema"]==1 && state["operation_id"]==plan["operation_id"] &&
        state["restore_plan_sha256"]==plan["plan_sha256"] && state["backup_plan_sha256"]==plan["backup_plan_sha256"] &&
        state["state_sha256"].isString() && hash_valid(state["state_sha256"].asString()) &&
        state["state_sha256"].asString()==seal(state,"state_sha256") && state["tree_descendants_created"]==false &&
        state["tree_mounts_created"]==false && state["state"].isString() && state["private_record"]==true &&
        (state["stage_identity"].isNull() || retained_location(state["stage_identity"])),
        "invalid-tree-restore-state","Restore state is not bound to its sealed plan and native operation lifetime");
    const auto phase=state["state"].asString();
    require(phase=="PREPARED" || phase=="STAGED" || phase=="PUBLISHING" || phase=="COMPLETE" || phase=="CANCELLED_SAFE",
        "invalid-tree-restore-state","Unknown tree restore recovery phase");
    require((phase!="STAGED" && phase!="PUBLISHING" && phase!="COMPLETE") || retained_location(state["stage_identity"]),
        "invalid-tree-restore-state","A staged or published restore lacks its captured directory identity"); return state;
}
Value checked_restore_plan(const Root& store,const Root& parent,const fs::path& directory,const fs::path& destination,
                           const std::string& name,const Value& backup) {
    checked_restore_paths(store,parent,directory,destination);
    require(components(name).size()==1 && name!="." && name!="..","invalid-tree-restore-plan","Select one private restore plan record");
    const auto plan=read_record(store,name);
    require(plan["schema"]==1 && plan["operation"]=="tree.restore" && plan["operation_id"].isString() &&
        identifier(plan["operation_id"].asString()) && name==restore_plan_name(plan) &&
        plan["plan_sha256"].isString() && hash_valid(plan["plan_sha256"].asString()) && plan["plan_sha256"].asString()==seal(plan) &&
        plan["backup_plan_sha256"]==backup["plan_sha256"] && plan["backup_operation_id"]==backup["operation_id"] &&
        plan["restore_state_record"]==restore_state_name(plan) && plan["staging_name"]==".ure-tree-restore-"+plan["operation_id"].asString() &&
        plan["private_record"]==true && plan["recovery_available"]==true,
        "invalid-tree-restore-plan","Invalid sealed recoverable tree restore plan or original backup binding");
    const auto final_name=destination.filename().string();
    require(components(final_name).size()==1 && final_name!="." && final_name!=".." && plan["destination_name"]==final_name &&
        plan["destination"]==fs::absolute(destination).lexically_normal().string() &&
        retained_location(plan["destination_parent"]) && json(plan["destination_parent"])==json(descriptor_identity(parent.fd())),
        "wrong-tree-restore-target","Restore recovery destination or its retained parent differs from the sealed plan");
    require(plan["operation_journal"]==fs::absolute(directory).lexically_normal().string() &&
        json(plan["journal_identity"])==json(descriptor_identity(store.fd())) &&
        json(plan["operation_targets"])==json(operation_targets(operation_target(parent.fd(),"tree-restore-parent"))),
        "wrong-tree-restore-journal","Restore journal or target descriptor identity changed");
    const auto binding=operation_binding("tree.restore",plan,directory,plan["operation_targets"]);
    require(json(plan["operation_journal_binding"])==json(binding.journal),"wrong-tree-restore-journal","Restore journal parent binding changed");
    const auto backup_state=read_record(store,"state.json");
    require(backup_state["state"]=="COMPLETE" && backup_state["verified"]==true && backup_state["plan_sha256"]==backup["plan_sha256"],
        "incomplete-tree","Restore recovery requires the original completed backup"); return plan;
}
bool tree_entry_exists(int parent,const std::string& name,struct stat& stat) {
    if(::fstatat(parent,name.c_str(),&stat,AT_SYMLINK_NOFOLLOW)==0)return true;
    require(errno==ENOENT,"tree-restore-observation-unavailable","Cannot inspect a restore destination or staging entry"); return false;
}
Value restore_observation(const Root& parent,const Value& plan,const Value& state) {
    struct stat destination_entry{},stage{}; const auto final_name=plan["destination_name"].asString(),staging=plan["staging_name"].asString();
    const bool published=tree_entry_exists(parent.fd(),final_name,destination_entry),staged=tree_entry_exists(parent.fd(),staging,stage);
    Value out; out["destination_exists"]=published; out["staging_exists"]=staged; out["captured_stage_identity"]=state["stage_identity"];
    out["destination_identity_verified"]=false; out["staging_identity_verified"]=false; out["published_bytes_verified"]=false;
    if(published && S_ISDIR(destination_entry.st_mode)) {
        auto current=parent.open(final_name,O_RDONLY|O_DIRECTORY); out["destination_identity"]=descriptor_identity(current.get());
        out["destination_identity_verified"]=retained_location(state["stage_identity"]) && json(out["destination_identity"])==json(state["stage_identity"]);
    }
    if(staged && S_ISDIR(stage.st_mode)) {
        auto current=parent.open(staging,O_RDONLY|O_DIRECTORY); out["staging_identity"]=descriptor_identity(current.get());
        out["staging_identity_verified"]=retained_location(state["stage_identity"]) && json(out["staging_identity"])==json(state["stage_identity"]);
    }
    out["current_state"]=published ? (out["destination_identity_verified"]!=true ? "DESTINATION_UNRECOGNIZED" :
        staged ? "PUBLISHED_WITH_UNEXPECTED_STAGE" : "PUBLISHED_CAPTURED_STAGE") :
        staged ? (retained_location(state["stage_identity"]) ? (out["staging_identity_verified"]==true ? "UNPUBLISHED_CAPTURED_STAGE" : "STAGE_DIVERGED") :
            "UNPUBLISHED_UNVERIFIED_STAGE") : "UNPUBLISHED_NO_STAGE";
    return out;
}
Value restore_binding_value(const OperationBinding& binding) {
    Value out; out["operation"]=binding.operation; out["operation_id"]=binding.operation_id; out["plan_sha256"]=binding.plan_sha256;
    out["targets"]=binding.targets; out["journal"]=binding.journal; return out;
}
Value restore_inspection(const Root& parent,const Value& plan,const Value& state,const OperationBinding& binding) {
    auto out=restore_observation(parent,plan,state); out["schema"]=1; out["operation_id"]=plan["operation_id"];
    out["restore_plan_record"]=restore_plan_name(plan); out["restore_state_record"]=restore_state_name(plan);
    out["restore_plan_sha256"]=plan["plan_sha256"]; out["backup_plan_sha256"]=plan["backup_plan_sha256"];
    out["last_confirmed_state"]=state["state"]; out["read_only"]=true; out["private_record"]=true; out["physical_test_record"]=false;
    const auto status=operation_lease_status(); out["coordinator_available"]=status["available"]==true;
    out["active_operation"]=status["active_exclusion"]==true; out["operation_owner_retained"]=status["retained_owner"]==true;
    out["owner_plan_sha256"]=status["owner"]["binding"]["plan_sha256"];
    out["owner_matches_restore_plan"]=status["retained_owner"]==true && json(status["owner"]["binding"])==json(restore_binding_value(binding));
    out["recovery_actions"]=Value(Json::arrayValue); out["recovery_available"]=true;
    if(status["available"]!=true)out["coordinator_error_code"]=status["code"];
    if(out["owner_matches_restore_plan"]==true && out["active_operation"]==false) {
        const auto observed=out["current_state"].asString();
        if(observed=="PUBLISHED_CAPTURED_STAGE")out["recovery_actions"].append("verify-published");
        else if(observed=="UNPUBLISHED_NO_STAGE" || observed=="UNPUBLISHED_CAPTURED_STAGE" || observed=="UNPUBLISHED_UNVERIFIED_STAGE")
            out["recovery_actions"].append("cancel-unpublished");
    }
    out["unverified_staging_policy"]="Preserve any unpublished staging entry without changing its contents or metadata. An uncaptured entry is not proof of ownership; absent destination plus exclusive native lifetime proves only that this restore published no installed target.";
    return out;
}
}
Value backup_tree_restore(const fs::path& directory,const fs::path& destination,const std::string& confirmation) {
    auto store=private_directory(directory,false); const auto plan=read_record(store,"plan.json"); check_plan(plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact tree backup plan SHA-256");
    const auto final_name=destination.filename().string(); require(components(final_name).size()==1 && final_name!="." && final_name!="..","invalid-path","Choose a new named restore directory");
    Root parent(destination.parent_path().empty() ? fs::path(".") : destination.parent_path()); root_gate(parent.fd()); outside(store.fd(),parent.fd());
    Value restore_plan; restore_plan["schema"]=1; restore_plan["operation"]="tree.restore"; restore_plan["operation_id"]=operation_id();
    restore_plan["backup_plan_sha256"]=plan["plan_sha256"]; restore_plan["backup_operation_id"]=plan["operation_id"];
    restore_plan["destination_parent"]=descriptor_identity(parent.fd()); restore_plan["destination_name"]=final_name;
    restore_plan["destination"]=fs::absolute(destination).lexically_normal().string();
    restore_plan["staging_name"]=".ure-tree-restore-"+restore_plan["operation_id"].asString();
    restore_plan["operation_targets"]=operation_targets(operation_target(parent.fd(),"tree-restore-parent"));
    restore_plan["operation_journal"]=fs::absolute(directory).lexically_normal().string();
    restore_plan["journal_identity"]=descriptor_identity(store.fd()); restore_plan["restore_state_record"]=restore_state_name(restore_plan);
    restore_plan["operation_journal_binding"]=operation_binding("tree.restore",restore_plan,directory,restore_plan["operation_targets"]).journal;
    restore_plan["recovery_available"]=true; restore_plan["private_record"]=true; restore_plan["plan_sha256"]=seal(restore_plan);
    ManagedOperation operation(operation_binding("tree.restore",restore_plan,directory,
        restore_plan["operation_targets"]));
    checked_restore_paths(store,parent,directory,destination);
    auto held=lock(store);
    checked_restore_paths(store,parent,directory,destination);
    require(json(read_record(store,"plan.json"))==json(plan),"stale-tree-plan","Tree plan changed during restore admission");
    const auto state=read_record(store,"state.json"); require(state["state"]=="COMPLETE" && state["verified"]==true && state["plan_sha256"]==plan["plan_sha256"],"incomplete-tree","Restore requires a completed tree backup");
    std::set<std::string> verified;
    all_entries(store,plan,[&](const Value& e) {
        if(e["kind"]=="file" && cache_insert(verified,blob_name(e)))verified_blob(store,e);
        require(::geteuid()==0 || (e["uid"].asUInt()==::geteuid() && e["gid"].asUInt()==::getegid() && e["kind"]!="char" && e["kind"]!="block"),
            "ownership-required","Restoring recorded owners or device nodes requires root privileges");
    });
    struct stat existing{}; require(::fstatat(parent.fd(),final_name.c_str(),&existing,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT,"existing-target","Tree restore never overwrites an existing destination");
    struct statvfs space{}; require(::fstatvfs(parent.fd(),&space)==0 && space.f_frsize>0,"io-error","Cannot inspect restore space");
    const auto needed=plan["stored_data_bytes"].asUInt64(); require(needed<=UINT64_MAX-32*1024*1024 && space.f_bavail>=(needed+32*1024*1024)/space.f_frsize+1,"no-space","Insufficient tree restore space");
    const auto staging_name=restore_plan["staging_name"].asString(),restore_record=restore_plan_name(restore_plan);
    store.save_record(restore_record,restore_plan);
    Value restore_state; restore_state["schema"]=1; restore_state["operation_id"]=restore_plan["operation_id"];
    restore_state["restore_plan_sha256"]=restore_plan["plan_sha256"]; restore_state["backup_plan_sha256"]=plan["plan_sha256"];
    restore_state["stage_identity"]=Value(); restore_state["tree_descendants_created"]=false; restore_state["tree_mounts_created"]=false;
    restore_state["private_record"]=true; restore_state["target_contents_verified"]=false; restore_state["cleanup_complete"]=false;
    // PREPARED is durable before intent, so a kill after mkdir but before the
    // inode record still has an exact-plan, absent-destination cancellation.
    save_restore_state(store,restore_state,"PREPARED",false);
    operation.begin("TREE_RESTORE_STAGING");
    require(::mkdirat(parent.fd(),staging_name.c_str(),0700)==0 && ::fsync(parent.fd())==0,"io-error","Cannot create private restore staging");
    Root staging(parent.open(staging_name,O_RDONLY|O_DIRECTORY));
    restore_state["stage_identity"]=descriptor_identity(staging.fd()); save_restore_state(store,restore_state,"STAGED");
    // Preserve restrictive directory metadata only after all children exist.
    bool published=false;
    try {
        all_entries(store,plan,[&](const Value& e) {
            const auto path=path_of(e),kind=e["kind"].asString();
            if(path==".")return;
            auto output_parent=parent_of(staging.fd(),path); const auto name=fs::path(path).filename().string();
            if(kind=="directory") {
                require(::mkdirat(output_parent.get(),name.c_str(),0700)==0,"restore-error","Cannot create restored directory");
            } else if(kind=="file") {
                if(e.isMember("hardlink_hex")) {
                    const auto link=unhex(e["hardlink_hex"],4096); auto link_parent=parent_of(staging.fd(),link);
                    require(::linkat(link_parent.get(),fs::path(link).filename().c_str(),output_parent.get(),name.c_str(),0)==0,"restore-error","Cannot restore hardlink relationship");
                } else {
                    auto input=store.open(blob_name(e),O_RDONLY|O_NONBLOCK); auto output=open_tree(staging.fd(),path,O_RDWR|O_CREAT|O_EXCL,0600);
                    copy_extents(input.get(),output.get(),e); require(content_hash(output.get(),e["bytes"].asUInt64())==e["sha256"].asString() && ::fsync(output.get())==0,"restore-error","Restored file fails content readback");
                }
                apply_metadata(staging.fd(),e);
            } else if(kind=="symlink") {
                const auto target=unhex(e["target_hex"],4096); require(::symlinkat(target.c_str(),output_parent.get(),name.c_str())==0,"restore-error","Cannot restore symlink"); apply_metadata(staging.fd(),e);
            } else if(kind!="socket") {
                const mode_t type=kind=="fifo" ? S_IFIFO : kind=="char" ? S_IFCHR : S_IFBLK;
                require(::mknodat(output_parent.get(),name.c_str(),type|0600,static_cast<dev_t>(e["rdev"].asUInt64()))==0,"restore-error","Cannot restore special entry"); apply_metadata(staging.fd(),e);
            }
            require(::fsync(output_parent.get())==0,"io-error","Cannot sync restored directory entry");
        });
        // Reverse pages keep only one bounded metadata page resident.
        for(unsigned index=plan["pages"].size();index>0;--index) {
            const auto records=page(store,plan,index-1);
            for(auto i=records.end();i!=records.begin();) { --i; if((*i)["kind"]=="directory")apply_metadata(staging.fd(),*i); }
        }
        require(::fsync(staging.fd())==0,"io-error","Cannot sync completed restore tree");
        Root at_stage(parent.open(staging_name,O_RDONLY|O_DIRECTORY));
        require(json(descriptor_identity(at_stage.fd()))==json(restore_state["stage_identity"]) &&
            json(descriptor_identity(staging.fd()))==json(restore_state["stage_identity"]),"changed-tree-restore-stage","Restore staging entry changed before publication");
        checked_restore_paths(store,parent,directory,destination);
        save_restore_state(store,restore_state,"PUBLISHING");
        operation.begin("TREE_RESTORE_PUBLISHING");
        require(::syscall(SYS_renameat2,parent.fd(),staging_name.c_str(),parent.fd(),final_name.c_str(),1U)==0,"restore-publish-error","Cannot publish restore without replacing an existing destination");
        published=true;
        require(::fsync(parent.fd())==0,"io-error","Cannot sync restored tree publication");
        Root installed(parent.open(final_name,O_RDONLY|O_DIRECTORY));
        require(same(info(installed.fd()),info(staging.fd())),"restore-verification-error","Published restore directory identity differs");
        verify_restored(store,plan,installed.fd());
        require(::fsync(installed.fd())==0 && ::fsync(parent.fd())==0,"io-error","Cannot sync independently verified restore publication");
        restore_state["target_contents_verified"]=true; restore_state["cleanup_complete"]=true;
        restore_state["published_identity"]=descriptor_identity(installed.fd()); save_restore_state(store,restore_state,"COMPLETE");
        require(restore_observation(parent,restore_plan,restore_state)["current_state"]=="PUBLISHED_CAPTURED_STAGE",
            "unrecognized-tree-restore-target","Published restore changed before ownership release");
        checked_restore_paths(store,parent,directory,destination);
    } catch(const Error& e) {
        if(published)throw Error(e.code,std::string(e.what())+"; published destination requires exact-inode verification; retained restore plan: "+restore_record);
        throw Error(e.code,std::string(e.what())+"; unpublished staging is preserved without further changes: "+staging_name+"; retained restore plan: "+restore_record);
    }
    Value result=summary(plan,"RESTORED"); result["verified"]=true; result["destination"]=fs::absolute(destination).lexically_normal().string();
    result["runtime_sockets_omitted"]=plan["runtime_sockets"]; result["existing_files_overwritten"]=false;
    result["restore_plan_record"]=restore_record; result["restore_state_record"]=restore_state_name(restore_plan);
    result["restore_plan_sha256"]=restore_plan["plan_sha256"]; result["recovery_available"]=true;
    held=Fd(); return operation.finish(result,true,true,"COMPLETE");
}
Value backup_tree_recover(const fs::path& directory,const fs::path& destination,const std::string& restore_record,
                          const std::string& action,const std::string& confirmation) {
    require(action=="inspect" || action=="verify-published" || action=="cancel-unpublished","unknown-tree-recovery-action","Select inspect, verify-published or cancel-unpublished");
    auto store=private_directory(directory,false); root_gate(store.fd()); const auto backup=read_record(store,"plan.json"); check_plan(backup);
    Root parent(destination.parent_path().empty() ? fs::path(".") : destination.parent_path()); root_gate(parent.fd()); outside(store.fd(),parent.fd());
    const auto plan=checked_restore_plan(store,parent,directory,destination,restore_record,backup);
    const auto binding=operation_binding("tree.restore",plan,directory,plan["operation_targets"]);
    auto state=checked_restore_state(store,plan);
    if(action=="inspect")return restore_inspection(parent,plan,state,binding);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact persisted tree restore plan SHA-256");
    ManagedOperation operation(binding,true);
    require(operation.token().has_retained_intent(),"operation-owner-missing","Tree restore recovery requires its exact unresolved retained owner");
    checked_restore_paths(store,parent,directory,destination);
    auto held=lock(store);
    const auto current_backup=read_record(store,"plan.json"); check_plan(current_backup);
    require(json(current_backup)==json(backup) && json(checked_restore_plan(store,parent,directory,destination,restore_record,current_backup))==json(plan),
        "changed-tree-restore-plan","Backup or restore plan changed during recovery admission");
    state=checked_restore_state(store,plan); const auto observed=restore_observation(parent,plan,state);
    Value result; result["schema"]=1; result["restore_plan_record"]=restore_record; result["restore_state_record"]=restore_state_name(plan);
    result["restore_plan_sha256"]=plan["plan_sha256"]; result["backup_plan_sha256"]=backup["plan_sha256"];
    result["destination"]=plan["destination"]; result["private_record"]=true; result["physical_test_record"]=false;
    result["tree_descendants_created"]=false; result["tree_mounts_created"]=false;
    if(action=="verify-published") {
        require(observed["current_state"]=="PUBLISHED_CAPTURED_STAGE","unrecognized-tree-restore-target","Published restore must have the exact captured staging inode, with no surviving staging entry");
        Root installed(parent.open(plan["destination_name"].asString(),O_RDONLY|O_DIRECTORY));
        require(json(descriptor_identity(installed.fd()))==json(state["stage_identity"]),"unrecognized-tree-restore-target","Published restore identity changed during verification");
        std::set<std::string> verified;
        all_entries(store,backup,[&](const Value& e) { if(e["kind"]=="file" && cache_insert(verified,blob_name(e)))verified_blob(store,e); });
        verify_restored(store,backup,installed.fd());
        require(::fsync(installed.fd())==0 && ::fsync(parent.fd())==0,"tree-restore-cleanup-unverified","Cannot sync independently verified restore publication");
        require(restore_observation(parent,plan,state)["current_state"]=="PUBLISHED_CAPTURED_STAGE","unrecognized-tree-restore-target","Published restore changed before terminal verification");
        state["target_contents_verified"]=true; state["published_identity"]=descriptor_identity(installed.fd());
        state["cleanup_complete"]=true; save_restore_state(store,state,"COMPLETE");
        require(restore_observation(parent,plan,state)["current_state"]=="PUBLISHED_CAPTURED_STAGE","unrecognized-tree-restore-target","Published restore changed after terminal journal publication");
        result["state"]="COMPLETE"; result["verified"]=true; result["target_contents_verified"]=true;
    } else {
        require(observed["destination_exists"]==false,"published-tree-restore","Unpublished cancellation requires the final destination to be absent");
        require(observed["current_state"]!="STAGE_DIVERGED","changed-tree-restore-stage","Recorded staging inode changed; cancellation preserves the owner for explicit inspection");
        // Tree restore creates neither children nor mounts. Exclusive admission
        // proves its writer has ended; forensic staging is deliberately retained.
        // An inode-less stage cannot authorize publication, but need not be
        // removed or modified to prove that the installed destination is absent.
        state["target_contents_verified"]=false; state["destination_absent"]=true; state["cleanup_complete"]=true;
        state["staging_artifact_preserved"]=observed["staging_exists"];
        state["staging_artifact_identity_verified"]=observed["staging_identity_verified"];
        state["staging_artifact_name"]=observed["staging_exists"]==true ? plan["staging_name"] : Value();
        save_restore_state(store,state,"CANCELLED_SAFE");
        const auto terminal_observed=restore_observation(parent,plan,state);
        require(terminal_observed["destination_exists"]==false,"published-tree-restore","Destination appeared before cancellation ownership release");
        require(terminal_observed["current_state"]!="STAGE_DIVERGED","changed-tree-restore-stage","Captured staging inode changed before cancellation ownership release");
        result["state"]="CANCELLED_SAFE"; result["verified"]=true; result["target_contents_verified"]=false;
        result["staging_artifact_preserved"]=state["staging_artifact_preserved"];
        result["staging_artifact_identity_verified"]=state["staging_artifact_identity_verified"];
        result["staging_artifact_name"]=state["staging_artifact_name"];
        result["verification_scope"]="Final destination absent and native tree writer ended; no tree descendants or mounts exist. Preserved staging contents and uncaptured entries are unverified and were not changed.";
    }
    checked_restore_paths(store,parent,directory,destination);
    result["cleanup_complete"]=true; held=Fd(); return operation.finish(result,true,true);
}
} // namespace ure
