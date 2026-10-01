// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
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
std::vector<std::string> names(int fd) {
    Fd copy(::openat(fd,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
    require(copy.get()>=0,"io-error","Cannot retain directory listing");
    DIR* stream=::fdopendir(::dup(copy.get())); require(stream,"io-error","Cannot enumerate tree directory");
    std::unique_ptr<DIR,int(*)(DIR*)> guard(stream,closedir);
    std::vector<std::string> out;
    while(true) {
        errno=0; auto* entry=::readdir(stream); if(!entry) { require(errno==0,"io-error","Tree listing failed"); break; }
        const std::string name=entry->d_name; if(name=="." || name=="..")continue;
        require(out.size()<100000,"size-limit","A tree directory exceeds 100000 children"); out.push_back(name);
    }
    std::sort(out.begin(),out.end()); return out;
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
Value entry(int root,const std::string& path,bool content) {
    auto fd=open_tree(root,path,O_PATH|O_NONBLOCK); const auto st=info(fd.get()); Value out=stamp(st); out["path_hex"]=hex(path);
    require(out["kind"]!="unknown","unsupported-tree-entry","Unknown file type in source tree");
    if(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) {
        auto data=open_tree(root,path,O_RDONLY|O_NONBLOCK|O_NOATIME|(S_ISDIR(st.st_mode) ? O_DIRECTORY : 0));
        require(json(stamp(info(data.get())))==json(stamp(st)),"stale-source","Tree entry changed while opening"); out["xattrs"]=attrs(data.get());
        if(S_ISREG(st.st_mode) && content) { require(st.st_size>=0,"invalid-file","Tree files require a finite size"); out["sha256"]=content_hash(data.get(),out["bytes"].asUInt64()); out["extents"]=extents(data.get(),out["bytes"].asUInt64()); }
        if(S_ISDIR(st.st_mode)) { Hash listing; for(const auto& name:names(data.get()))listing.add(hex(name)+"\n"); out["children_sha256"]=listing.finish(); }
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
void source_match(int root,const Value& e) {
    auto expected=e; expected.removeMember("hardlink_hex");
    expected.removeMember("sha256"); expected.removeMember("extents");
    auto current=entry(root,path_of(e),false);
    require(json(current)==json(expected),"stale-source","Tree namespace, content or metadata changed since planning");
}
}
Value backup_tree_plan(const Root& context,const std::string& relative,const std::string& profile,const fs::path& destination) {
    require(identifier(profile),"invalid-profile","A firmware profile is required");
    auto source=context.open(relative,O_RDONLY|O_DIRECTORY); root_gate(source.get());
    Root parent(destination.parent_path().empty() ? fs::path(".") : destination.parent_path()); root_gate(parent.fd()); outside(source.get(),parent.fd());
    const auto initial=stamp(info(source.get())); auto store=private_directory(destination,true); auto held=lock(store);
    Value plan; plan["schema"]=1; plan["format"]="ure-tree-backup"; plan["operation_id"]=operation_id(); plan["created_utc"]=utc();
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
    std::function<void(const std::string&)> walk=[&](const std::string& path) {
        auto current=open_tree(source.get(),path,O_PATH|O_NONBLOCK);
        require(!same(info(current.get()),info(store.fd())),"recursive-backup","Source traversal reached its own backup directory");
        if(S_ISDIR(info(current.get()).st_mode)) { auto directory=open_tree(source.get(),path,O_RDONLY|O_DIRECTORY); root_gate(directory.get()); }
        auto e=entry(source.get(),path,true); require(++count<=entry_limit,"size-limit","Tree exceeds one million entries");
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
        if(e["kind"]=="directory") { auto directory=open_tree(source.get(),path,O_RDONLY|O_DIRECTORY); for(const auto& child:names(directory.get()))walk(path=="." ? child : path+"/"+child); }
        source_match(source.get(),e);
    };
    walk("."); flush(); require(json(stamp(info(source.get())))==json(initial),"stale-source","Source root changed during planning");
    plan["entries"]=count; plan["logical_bytes"]=Json::UInt64(bytes); plan["stored_data_bytes"]=Json::UInt64(stored); plan["runtime_sockets"]=sockets;
    plan["restore_requirements"]="new destination; Unix metadata support; required ownership/ACL/xattr privileges; runtime sockets recreated by services";
    plan["plan_sha256"]=seal(plan); store.save_record("plan.json",plan); return summary(plan,"PLANNED");
}
Value backup_tree_capture(const Root& context,const fs::path& directory,const std::string& confirmation) {
    auto store=private_directory(directory,false); root_gate(store.fd()); auto held=lock(store); const auto plan=read_record(store,"plan.json"); check_plan(plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the reviewed tree plan SHA-256");
    auto source=selected(context,plan); outside(source.fd(),store.fd());
    all_entries(store,plan,[&](const Value& e) { source_match(source.fd(),e); });
    Value state; state["plan_sha256"]=plan["plan_sha256"]; state["state"]="CAPTURING"; state["completed_files"]=0; store.save_record("state.json",state,true);
    unsigned completed=0,processed_entries=0; std::set<std::string> verified;
    try {
        all_entries(store,plan,[&](const Value& e) {
            ++processed_entries;
            source_match(source.fd(),e); if(e["kind"]!="file")return;
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
                copy_extents(input.get(),output.get(),e); source_match(source.fd(),e);
                require(content_hash(output.get(),e["bytes"].asUInt64())==e["sha256"].asString(),"stale-source","Captured tree content differs from the plan");
                require(::fsync(output.get())==0 && ::linkat(store.fd(),temporary,store.fd(),blob.c_str(),0)==0 && ::unlinkat(store.fd(),temporary,0)==0 && ::fsync(store.fd())==0,"io-error","Cannot publish verified tree data");
            }
            cache_insert(verified,blob); state["completed_files"]=++completed; state["completed_entries"]=processed_entries; store.save_record("state.json",state,true);
        });
        all_entries(store,plan,[&](const Value& e) { source_match(source.fd(),e); }); selected(context,plan);
        state["state"]="COMPLETE"; state["verified"]=true; state["completed_entries"]=plan["entries"]; store.save_record("state.json",state,true); return summary(plan,"COMPLETE");
    } catch(...) { state["state"]="INCOMPLETE"; state["verified"]=false; try { store.save_record("state.json",state,true); } catch(...) {} throw; }
}
Value backup_tree_verify(const fs::path& directory) {
    auto store=private_directory(directory,false); auto held=lock(store); const auto plan=read_record(store,"plan.json"); check_plan(plan);
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
}
Value backup_tree_restore(const fs::path& directory,const fs::path& destination,const std::string& confirmation) {
    auto store=private_directory(directory,false); auto held=lock(store); const auto plan=read_record(store,"plan.json"); check_plan(plan);
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact tree backup plan SHA-256");
    const auto state=read_record(store,"state.json"); require(state["state"]=="COMPLETE" && state["verified"]==true && state["plan_sha256"]==plan["plan_sha256"],"incomplete-tree","Restore requires a completed tree backup");
    std::set<std::string> verified;
    all_entries(store,plan,[&](const Value& e) {
        if(e["kind"]=="file" && cache_insert(verified,blob_name(e)))verified_blob(store,e);
        require(::geteuid()==0 || (e["uid"].asUInt()==::geteuid() && e["gid"].asUInt()==::getegid() && e["kind"]!="char" && e["kind"]!="block"),
            "ownership-required","Restoring recorded owners or device nodes requires root privileges");
    });
    const auto final_name=destination.filename().string(); require(components(final_name).size()==1 && final_name!="." && final_name!="..","invalid-path","Choose a new named restore directory");
    Root parent(destination.parent_path().empty() ? fs::path(".") : destination.parent_path()); root_gate(parent.fd()); outside(store.fd(),parent.fd());
    struct stat existing{}; require(::fstatat(parent.fd(),final_name.c_str(),&existing,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT,"existing-target","Tree restore never overwrites an existing destination");
    struct statvfs space{}; require(::fstatvfs(parent.fd(),&space)==0 && space.f_frsize>0,"io-error","Cannot inspect restore space");
    const auto needed=plan["stored_data_bytes"].asUInt64(); require(needed<=UINT64_MAX-32*1024*1024 && space.f_bavail>=(needed+32*1024*1024)/space.f_frsize+1,"no-space","Insufficient tree restore space");
    const auto staging_name=".ure-tree-restore-"+operation_id();
    require(::mkdirat(parent.fd(),staging_name.c_str(),0700)==0 && ::fsync(parent.fd())==0,"io-error","Cannot create private restore staging");
    Root staging(parent.open(staging_name,O_RDONLY|O_DIRECTORY));
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
        require(::syscall(SYS_renameat2,parent.fd(),staging_name.c_str(),parent.fd(),final_name.c_str(),1U)==0,"restore-publish-error","Cannot publish restore without replacing an existing destination");
        published=true;
        require(::fsync(parent.fd())==0,"io-error","Cannot sync restored tree publication");
    } catch(const Error& e) {
        if(published)throw Error(e.code,std::string(e.what())+"; the new destination was published but directory durability is unverified");
        const bool private_staging=::fchmod(staging.fd(),0700)==0 && ::fsync(staging.fd())==0;
        throw Error(e.code,std::string(e.what())+(private_staging ? "; private restore staging remains: " : "; restore staging permissions/durability are unverified: ")+staging_name);
    }
    Value result=summary(plan,"RESTORED"); result["verified"]=true; result["destination"]=fs::absolute(destination).lexically_normal().string();
    result["runtime_sockets_omitted"]=plan["runtime_sockets"]; result["existing_files_overwritten"]=false; return result;
}
} // namespace ure
