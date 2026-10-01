// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <functional>
#include <linux/magic.h>
#include <map>
#include <set>
#include <sstream>
#include <sys/statfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace ure {
namespace {
// Stock GKI fs/configfs/mount.c; this constant is absent from the
// pinned Bionic UAPI header. It is a filesystem identity, not a device value.
constexpr long configfs_magic=0x62656570L;
bool digits(const std::string& text) { return !text.empty() && text.size()<=16 && std::all_of(text.begin(),text.end(),[](char c){return c>='0' && c<='9';}); }
bool device_number(const std::string& text) { const auto colon=text.find(':'); return colon!=text.npos && digits(text.substr(0,colon)) && digits(text.substr(colon+1)); }
std::string device(dev_t value) { return std::to_string(major(value))+":"+std::to_string(minor(value)); }
std::string decode(const std::string& text) {
    std::string result;
    for(std::size_t i=0;i<text.size();++i) {
        if(text[i]!='\\') { result+=text[i]; continue; }
        require(i+3<text.size() && text[i+1]>='0' && text[i+1]<='7' && text[i+2]>='0' && text[i+2]<='7' && text[i+3]>='0' && text[i+3]<='7',
            "invalid-usage","Malformed kernel path escape");
        const auto code=(text[i+1]-'0')*64+(text[i+2]-'0')*8+text[i+3]-'0';
        require(code>0 && code<=255,"invalid-usage","Invalid kernel path byte"); result+=static_cast<char>(code); i+=3;
    }
    return result;
}
Value path_owner(const Root& system,const std::string& encoded) {
    const auto path=decode(encoded); require(path.starts_with("/"),"invalid-usage","Kernel backing path must be absolute"); components(path.substr(1));
    // Kernel-managed swap/gadget paths may use by-name links. This is metadata
    // inspection only; no descriptor is opened for data or write access.
    struct stat st{};
    require(::fstatat(system.fd(),path.substr(1).c_str(),&st,0)==0,"usage-unavailable","Cannot resolve kernel backing metadata");
    Value item; item["device_number"]=device(S_ISBLK(st.st_mode) ? st.st_rdev : st.st_dev);
    item["kind"]=S_ISBLK(st.st_mode) ? "block" : S_ISREG(st.st_mode) ? "file" : "other";
    item["inode"]=Json::UInt64(st.st_ino); return item;
}
void mount_owners(const Root& system,const std::string& path,Value& observations,const std::string& pid) {
    std::istringstream input(system.read(path,4*1024*1024)); std::string line;
    while(std::getline(input,line)) {
        std::istringstream row(line); std::vector<std::string> fields; std::string field;
        while(row>>field)fields.push_back(field);
        const auto separator=std::find(fields.begin(),fields.end(),"-");
        require(fields.size()>=10 && separator-fields.begin()>=6 && fields.end()-separator==4 && digits(fields[0]) && device_number(fields[2]),
            "invalid-usage","Malformed mountinfo observation");
        Value item; item["device_number"]=fields[2]; item["mount_id"]=fields[0]; item["pid"]=pid;
        item["path"]=decode(fields[4]); item["options"]=fields[5]; item["filesystem"]=*(separator+1); item["super_options"]=*(separator+3); observations["mounts"].append(item);
        require(observations["mounts"].size()<=32768,"size-limit","Mount observation budget exceeded");
    }
}
Value observe(const Root& system) {
    Value data; data["coverage"]["mounts"]=false; data["coverage"]["processes"]=false; data["coverage"]["swaps"]=false; data["coverage"]["usb"]=false;
    for(const auto* key:{"mounts","open_users","swaps","usb","errors"})data[key]=Value(Json::arrayValue);
    try { mount_owners(system,"proc/"+std::to_string(::getpid())+"/mountinfo",data,"self"); data["coverage"]["mounts"]=true; }
    catch(const Error& error) { data["errors"].append(error.code); }
    try {
        std::istringstream input(system.read("proc/swaps",1024*1024)); std::string line;
        require(static_cast<bool>(std::getline(input,line)) && line.starts_with("Filename"),"invalid-usage","Swap table header is unavailable");
        while(std::getline(input,line)) {
            std::istringstream row(line); std::string path,type,size,used,priority,extra;
            require(static_cast<bool>(row>>path>>type>>size>>used>>priority) && !(row>>extra),"invalid-usage","Malformed swap observation");
            auto item=path_owner(system,path); item["active"]=true; data["swaps"].append(item);
        }
        data["coverage"]["swaps"]=true;
    } catch(const Error& error) { data["errors"].append(error.code); }
    try {
        auto proc=system.open("proc",O_RDONLY|O_DIRECTORY); struct statfs fs{};
        require(::fstatfs(proc.get(),&fs)==0 && fs.f_type==PROC_SUPER_MAGIC,"usage-unavailable","Process inspection requires the kernel proc filesystem");
        bool complete=true; std::size_t total=0;
        for(const auto& pid:system.list("proc",16384))if(digits(pid)) {
            try {
                // Every visible process namespace is checked, not only self.
                mount_owners(system,"proc/"+pid+"/mountinfo",data,pid);
                Root fds(system.open("proc/"+pid+"/fd",O_RDONLY|O_DIRECTORY));
                for(const auto& number:fds.list(".",4096)) {
                    require(digits(number) && ++total<=65536,"size-limit","Process descriptor budget exceeded"); struct stat st{};
                    // Following this final procfs magic link performs only stat;
                    // opening it would duplicate a foreign process descriptor.
                    if(::fstatat(fds.fd(),number.c_str(),&st,0)!=0) {
                        if(errno==ENOENT)continue;
                        complete=false; continue;
                    }
                    if(!S_ISREG(st.st_mode) && !S_ISBLK(st.st_mode) && !S_ISDIR(st.st_mode))continue;
                    const auto info=system.read("proc/"+pid+"/fdinfo/"+number,65536); std::istringstream fields(info); std::string row; std::uint64_t flags=0,inode=0; bool known=false,inode_known=false;
                    while(std::getline(fields,row))if(row.starts_with("flags:") || row.starts_with("ino:")) {
                        const bool is_flags=row.starts_with("flags:"); auto text=row.substr(is_flags ? 6 : 4);
                        const auto first=text.find_first_not_of(" \t"); require(first!=text.npos,"invalid-usage","Missing descriptor field"); text.erase(0,first);
                        auto& parsed_value=is_flags ? flags : inode; auto& parsed_known=is_flags ? known : inode_known;
                        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),parsed_value,is_flags ? 8 : 10);
                        require(parsed.ec==std::errc() && parsed.ptr==text.data()+text.size() && !parsed_known,"invalid-usage","Malformed descriptor field"); parsed_known=true;
                    }
                    struct stat current{};
                    require(known && inode_known && inode==st.st_ino && ::fstatat(fds.fd(),number.c_str(),&current,0)==0 &&
                        current.st_dev==st.st_dev && current.st_ino==st.st_ino && current.st_rdev==st.st_rdev,"usage-unavailable","Descriptor was reused or its access mode is unavailable");
                    Value item; item["pid"]=pid; item["fd"]=number; item["device_number"]=device(S_ISBLK(st.st_mode) ? st.st_rdev : st.st_dev);
                    item["write_access"]=(flags&O_ACCMODE)!=O_RDONLY; item["raw_block"]=S_ISBLK(st.st_mode); data["open_users"].append(item);
                }
            } catch(const Error& error) {
                struct stat st{}; if(::fstatat(proc.get(),pid.c_str(),&st,AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT)continue;
                complete=false; if(data["errors"].size()<64)data["errors"].append(error.code);
            }
        }
        for(const auto& mount:data["mounts"])if(mount["filesystem"]=="proc" && mount["super_options"].asString().find("hidepid=")!=std::string::npos)complete=false;
        data["coverage"]["processes"]=complete;
    } catch(const Error& error) { data["errors"].append(error.code); }
    try {
        bool found=false;
        for(const auto* base:{"sys/kernel/config/usb_gadget","config/usb_gadget"})if(system.exists(base)) {
            auto directory=system.open(base,O_RDONLY|O_DIRECTORY); struct statfs config{};
            require(::fstatfs(directory.get(),&config)==0 && config.f_type==configfs_magic,"usage-unavailable","USB observations require actual configfs");
            found=true;
            for(const auto& gadget:system.list(base,64)) {
                const auto functions=std::string(base)+"/"+gadget+"/functions";
                if(!system.exists(functions))continue;
                for(const auto& function:system.list(functions,128))if(function.starts_with("mass_storage.")) {
                    const auto path=functions+"/"+function;
                    for(const auto& lun:system.list(path,64))if(lun.starts_with("lun.")) {
                        auto backing=system.read(path+"/"+lun+"/file",4096);
                        while(!backing.empty() && (backing.back()=='\n' || backing.back()=='\r'))backing.pop_back();
                        if(backing.empty())continue;
                        auto item=path_owner(system,backing);
                        item["origin"]="configfs-mass-storage"; data["usb"].append(item);
                    }
                }
            }
        }
        // Absence of configfs does not prove that a legacy or firmware-owned
        // gadget cannot export storage. The managed gadget backend must settle it.
        data["coverage"]["usb"]=found && !system.exists("sys/class/android_usb");
    } catch(const Error& error) { data["errors"].append(error.code); }
    return data;
}
}
Value storage_usage_policy(const Value& graph,const Value& observations,const std::string& stable_id) {
    require(graph["objects"].isArray() && graph["objects"].size()<=4096 && observations["coverage"].isObject(),"invalid-usage","Malformed storage usage observations");
    for(const auto* category:{"mounts","swaps","usb","open_users"})require(observations[category].isArray() && observations[category].size()<=65536,"invalid-usage","Missing or oversized ownership observation array");
    std::map<std::string,Value> objects; std::set<std::string> numbers,ids; Value selected;
    for(const auto& item:graph["objects"]) {
        require(item["kernel_name"].isString() && identifier(item["kernel_name"].asString()) && item["stable_id"].isString() &&
            item["device_number"].isString() && device_number(item["device_number"].asString()) &&
            item["slaves"].isArray() && item["holders"].isArray() && item["mounts"].isArray() &&
            item["slaves"].size()<=128 && item["holders"].size()<=128 && objects.emplace(item["kernel_name"].asString(),item).second &&
            ids.insert(item["stable_id"].asString()).second && numbers.insert(item["device_number"].asString()).second,
            "ambiguous-identity","Storage graph contains duplicated object identities");
        if(item["stable_id"]==stable_id)selected=item;
    }
    require(selected.isObject(),"identity-unavailable","Usage target was not found uniquely");
    std::set<std::string> related{selected["kernel_name"].asString()};
    if(selected["partition"]==true)related.insert(selected["parent_lun_name"].asString());
    else for(const auto& pair:objects)if(pair.second["parent_lun_sysfs"]==selected["sysfs_path"])related.insert(pair.first);
    bool changed=true;
    while(changed) {
        changed=false;
        for(const auto& pair:objects) {
            const auto& object=pair.second;
            for(const auto& slave:object["slaves"])if(related.count(slave.asString())!=0)changed=related.insert(pair.first).second || changed;
            if(related.count(pair.first)!=0)for(const auto& holder:object["holders"])changed=related.insert(holder.asString()).second || changed;
        }
        require(related.size()<=4096,"size-limit","Storage dependency closure exceeded budget");
    }
    Value result; result["schema"]=1; result["read_only"]=true; result["private_record"]=true; result["stable_id"]=stable_id;
    result["coverage"]=observations["coverage"]; result["related_objects"]=Value(Json::arrayValue); result["blockers"]=Value(Json::arrayValue);
    result["related_device_numbers"]=Value(Json::arrayValue);
    result["blocker_count"]=0;
    auto block=[&](const std::string& code,const Value& detail) {
        result["blocker_count"]=result["blocker_count"].asUInt()+1;
        if(result["blockers"].size()<128) { Value item; item["code"]=code; item["detail"]=detail; result["blockers"].append(item); }
    };
    std::set<std::string> relevant;
    // Follow only the slave direction for cycle detection: a reciprocal
    // slave/holder pair is normal sysfs topology, not a dependency cycle.
    std::map<std::string,unsigned> color;
    std::function<bool(const std::string&,unsigned)> acyclic=[&](const std::string& name,unsigned depth) {
        if(objects.count(name)==0)return true;
        if(depth>128 || color[name]==1)return false;
        if(color[name]==2)return true;
        color[name]=1;
        for(const auto& slave:objects.at(name)["slaves"]) {
            require(slave.isString() && identifier(slave.asString()),"invalid-usage","Malformed dependency identity");
            if(!acyclic(slave.asString(),depth+1))return false;
        }
        color[name]=2; return true;
    };
    for(const auto& name:related) {
        if(objects.count(name)==0) { block("unresolved-holder",name); continue; }
        const auto& object=objects.at(name); relevant.insert(object["device_number"].asString()); result["related_objects"].append(object["stable_id"]);
        if(object["dependencies_available"]!=true)block("dependencies-unavailable",object["stable_id"]);
        if(!acyclic(name,0))block("dependency-cycle-or-depth",object["stable_id"]);
        for(const auto& mount:object["mounts"])block("mounted",mount);
        for(const auto& slave:object["slaves"])if(objects.count(slave.asString())==0)block("unresolved-dependency",slave);
    }
    for(const auto* name:{"mounts","swaps","processes","usb"})if(observations["coverage"][name]!=true)block("usage-unavailable",name);
    for(const auto* category:{"mounts","swaps","usb","open_users"})for(const auto& owner:observations[category]) {
        require(owner["device_number"].isString() && device_number(owner["device_number"].asString()),"invalid-usage","Owner device number is unavailable");
        if(relevant.count(owner["device_number"].asString())!=0 && (std::string_view(category)!="open_users" || owner["write_access"]!=false))block(category,owner);
    }
    for(const auto& number:relevant)result["related_device_numbers"].append(number);
    result["quiescent_observed"]=result["blocker_count"]==0; result["blockers_truncated"]=result["blocker_count"].asUInt()>result["blockers"].size(); result["atomic_snapshot"]=false;
    result["scope"]="visible proc namespaces, sysfs dependencies, swaps and configfs storage exports; unrelated writers are not globally locked";
    return result;
}
Value storage_usage(const Root& system,const std::string& stable_id) { return storage_usage_policy(storage_graph(system),observe(system),stable_id); }
} // namespace ure
