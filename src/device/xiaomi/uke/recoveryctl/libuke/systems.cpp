// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>

namespace ure {
static std::string trim(std::string text) {
    const auto first=text.find_first_not_of(" \t\r\n\0",0,5), last=text.find_last_not_of(" \t\r\n\0",std::string::npos,5);
    return first==text.npos ? "" : text.substr(first,last-first+1);
}
static std::map<std::string,std::string> parse_os_release(const std::string& text) {
    std::map<std::string,std::string> fields;
    std::istringstream lines(text); std::string line;
    while(std::getline(lines,line)) {
        require(line.size()<=4096, "size-limit", "os-release line exceeds limit");
        line=trim(line); if(line.empty() || line.front()=='#')continue;
        const auto equal=line.find('=');
        require(equal!=line.npos && equal>0, "invalid-os-release", "Malformed os-release assignment");
        const auto key=line.substr(0,equal);
        require(std::all_of(key.begin(),key.end(),[](char c){return (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_';}),
            "invalid-os-release", "Invalid os-release key");
        auto value=trim(line.substr(equal+1));
        if(!value.empty() && (value.front()=='"' || value.front()=='\'')) {
            require(value.size()>=2 && value.back()==value.front(), "invalid-os-release", "Unclosed os-release quote");
            value=value.substr(1,value.size()-2);
        }
        require(!fields.contains(key), "invalid-os-release", "Duplicate os-release key");
        fields[key]=value;
    }
    return fields;
}
static std::vector<std::string> listing(const Root& root, const std::string& directory) {
    return root.exists(directory) ? root.list(directory,1024) : std::vector<std::string>{};
}
static bool boot_file(const Root& root, const std::string& value) {
    if(value.empty())return false;
    std::string relative=value.front()=='/' ? value.substr(1) : value;
    try { components(relative); } catch(const Error&) { return false; }
    return root.exists(relative) || root.exists("boot/"+relative);
}
static Value entries(const Root& root, const std::string& directory) {
    Value result(Json::arrayValue);
    for(const auto& name : listing(root,directory)) {
        if(!name.ends_with(".conf") || !identifier(name))continue;
        Value entry; entry["id"]=name.substr(0,name.size()-5); entry["path"]=directory+"/"+name;
        entry["initrd"]=Value(Json::arrayValue); entry["warnings"]=Value(Json::arrayValue);
        std::istringstream lines(root.read(directory+"/"+name,65536)); std::string line;
        std::set<std::string> singleton;
        while(std::getline(lines,line)) {
            line=trim(line); if(line.empty() || line.front()=='#')continue;
            const auto space=line.find_first_of(" \t"); if(space==line.npos)continue;
            const auto key=line.substr(0,space),value=trim(line.substr(space+1));
            if(key=="initrd")entry["initrd"].append(value);
            else if(key=="linux" || key=="efi" || key=="devicetree" || key=="options" || key=="title" || key=="version") {
                require(singleton.insert(key).second,"invalid-bls","Duplicate boot-entry field"); entry[key]=value;
            }
        }
        const auto image=entry.isMember("linux") ? entry["linux"].asString() : entry.get("efi","").asString();
        entry["image_exists"]=boot_file(root,image);
        bool initramfs_ok=true;
        for(const auto& initrd : entry["initrd"])if(!boot_file(root,initrd.asString()))initramfs_ok=false;
        entry["initramfs_exists"]=initramfs_ok;
        entry["dtb_exists"]=!entry.isMember("devicetree") || boot_file(root,entry["devicetree"].asString());
        entry["components_present"]=entry["image_exists"].asBool() && initramfs_ok && entry["dtb_exists"].asBool();
        entry["boot_validated"]=false;
        result.append(entry);
    }
    return result;
}
Value linux_detect(const Root& root, const Root* esp) {
    Value result; result["detected"]=false; result["read_only"]=true;
    result["warnings"]=Value(Json::arrayValue);
    std::string release;
    if(root.exists_resolved("etc/os-release"))release="etc/os-release";
    else if(root.exists_resolved("usr/lib/os-release"))release="usr/lib/os-release";
    if(!release.empty()) {
        const auto fields=parse_os_release(root.read_resolved(release,65536));
        result["detected"]=fields.contains("ID") || fields.contains("NAME"); result["os_release_source"]=release;
        for(const auto& key : {"ID","ID_LIKE","NAME","PRETTY_NAME","VERSION","VERSION_ID","VARIANT_ID","BUILD_ID"}) {
            const auto found=fields.find(key); if(found!=fields.end())result["distribution"][key]=found->second;
        }
    } else {
        for(const auto& fallback : {"etc/fedora-release","etc/debian_version","etc/arch-release","etc/alpine-release","etc/gentoo-release"}) {
            if(root.exists(fallback)) { result["detected"]=true; result["fallback_source"]=fallback; result["warnings"].append("Distribution identity is partial; os-release is absent"); break; }
        }
    }
    result["package_databases"]=Value(Json::arrayValue);
    for(const auto& candidate : std::vector<std::pair<std::string,std::string>>{{"rpm","usr/lib/sysimage/rpm"},{"rpm","var/lib/rpm"},{"dpkg","var/lib/dpkg/status"},{"pacman","var/lib/pacman/local"},{"apk","lib/apk/db/installed"}}) {
        if(root.exists(candidate.second)) { Value item; item["type"]=candidate.first; item["path"]=candidate.second; result["package_databases"].append(item); }
    }
    std::set<std::string> releases;
    for(const auto& directory : {"usr/lib/modules","lib/modules"}) for(const auto& name : listing(root,directory)) if(identifier(name))releases.insert(name);
    for(const auto& name : listing(root,"boot")) if(name.starts_with("vmlinuz-") && identifier(name.substr(8)))releases.insert(name.substr(8));
    result["kernels"]=Value(Json::arrayValue);
    for(const auto& version : releases) {
        Value item; item["release"]=version;
        item["image"]=root.exists("boot/vmlinuz-"+version) || root.exists("boot/Image-"+version);
        item["initramfs"]=root.exists("boot/initramfs-"+version+".img") || root.exists("boot/initrd.img-"+version) || root.exists("boot/initrd-"+version);
        item["modules"]=root.exists("usr/lib/modules/"+version) || root.exists("lib/modules/"+version);
        item["components_present"]=item["image"].asBool() && item["initramfs"].asBool() && item["modules"].asBool();
        item["boot_validated"]=false; result["kernels"].append(item);
    }
    result["boot_entries"]=entries(root,"boot/loader/entries");
    if(esp)for(const auto& item : entries(*esp,"loader/entries"))result["boot_entries"].append(item);
    result["ukis"]=Value(Json::arrayValue);
    if(esp)for(const auto& name : listing(*esp,"EFI/Linux"))if(name.ends_with(".efi") && identifier(name))result["ukis"].append(name);
    result["recovery_kernel_is_installed_kernel"]=false;
    result["architecture"]="unknown";
    for(const auto& shell : {"usr/bin/bash","bin/bash","usr/bin/sh"}) {
        if(!root.exists_resolved(shell))continue;
        auto binary=root.open_resolved(shell,O_RDONLY); unsigned char header[20]{};
        if(::pread(binary.get(),header,sizeof(header),0)==static_cast<ssize_t>(sizeof(header)) && header[4]==2 && header[5]==1 &&
            header[17]==0 && (header[16]==2 || header[16]==3) && std::equal(header,header+4,reinterpret_cast<const unsigned char*>("\x7f" "ELF"))) {
            const auto machine=static_cast<unsigned>(header[18]) | static_cast<unsigned>(header[19])<<8;
            if(header[5]==1)result["architecture"]=machine==183 ? "aarch64" : machine==62 ? "x86_64" : "other";
            break;
        }
    }
    if(result["architecture"]=="unknown")result["warnings"].append("Architecture could not be identified from a regular ELF executable");
    return result;
}
Value windows_detect(const Root& root, const Root* esp) {
    Value result; result["read_only"]=true; result["edition"]="unknown"; result["build"]="unknown";
    result["system32"]=root.exists("Windows/System32");
    result["registry_system"]=root.exists("Windows/System32/config/SYSTEM");
    result["registry_software"]=root.exists("Windows/System32/config/SOFTWARE");
    result["detected"]=result["system32"].asBool() && (result["registry_system"].asBool() || result["registry_software"].asBool());
    result["recovery_environment"]=root.exists("Windows/System32/Recovery");
    result["efi_loader"]=esp && esp->exists("EFI/Microsoft/Boot/bootmgfw.efi");
    result["bcd"]=esp && esp->exists("EFI/Microsoft/Boot/BCD");
    result["boot_validated"]=false; result["warnings"]=Value(Json::arrayValue);
    if(result["detected"].asBool() && !result["efi_loader"].asBool())result["warnings"].append("Microsoft EFI loader is missing or ESP was not supplied");
    return result;
}
Value config_validate(const Root& root, const std::string& file, const std::string& kind) {
    const auto text=root.read(file,1024*1024);
    Value result; result["kind"]=kind; result["path"]=file; result["issues"]=Value(Json::arrayValue);
    require(kind=="fstab" || kind=="crypttab" || kind=="bls" || kind=="json","unsupported-validator","Unknown configuration validator");
    if(kind=="json") { parse_json(text); result["syntax_valid"]=true; result["storage_references_validated"]=false; return result; }
    std::istringstream lines(text); std::string line; std::set<std::string> seen; unsigned line_number=0;
    while(std::getline(lines,line)) {
        ++line_number; line=trim(line); if(line.empty() || line.front()=='#')continue;
        std::istringstream fields(line); std::vector<std::string> words; std::string word;
        while(fields>>word)words.push_back(word);
        std::string error;
        if(kind=="fstab") {
            if(words.size()<4 || words.size()>6)error="Expected four to six fstab fields";
            else if(!seen.insert(words[1]).second)error="Duplicate mount point";
            else if(words[0].starts_with("PARTUUID=") && !uuid(words[0].substr(9)))error="Malformed PARTUUID";
            else if(words[1].front()!='/' && words[2]!="swap")error="Mount point must be absolute";
        } else if(kind=="crypttab") {
            if(words.size()<2 || words.size()>4)error="Expected two to four crypttab fields";
            else if(!identifier(words[0]) || !seen.insert(words[0]).second)error="Invalid or duplicate mapper name";
        } else if(kind=="json") {
            parse_json(text); break;
        } else if(kind=="bls") {
            if(words.size()<2)error="Boot entry needs a key and value";
            else if((words[0]=="linux" || words[0]=="initrd" || words[0]=="devicetree") && !boot_file(root,words[1]))error="Referenced boot component is absent";
        } else throw Error("unsupported-validator","Validator must be fstab, crypttab, bls or json");
        if(!error.empty()) { Value item; item["line"]=line_number; item["message"]=error; result["issues"].append(item); }
    }
    result["syntax_valid"]=result["issues"].empty(); result["storage_references_validated"]=false;
    return result;
}
Value files_list(const Root& root, const std::string& directory) {
    Value result(Json::arrayValue);
    for(const auto& name : root.list(directory)) {
        Value item; item["name"]=name; const auto relative=(fs::path(directory)/name).generic_string();
        try {
            const auto st=root.stat(relative); item["uid"]=static_cast<Json::UInt>(st.st_uid); item["gid"]=static_cast<Json::UInt>(st.st_gid);
            item["mode"]=static_cast<Json::UInt>(st.st_mode & 07777); item["bytes"]=Json::Int64(st.st_size);
            item["directory"]=S_ISDIR(st.st_mode); item["regular"]=S_ISREG(st.st_mode);
        } catch(const Error&) {
            try { item["symlink"]=true; item["target"]=root.link(relative); }
            catch(const Error&) { item["unavailable"]=true; }
        }
        result.append(item);
    }
    return result;
}
Value files_search(const Root& root, const std::string& directory, const std::string& pattern) {
    require(!pattern.empty() && pattern.size()<=256,"invalid-search","Use a bounded non-empty search pattern");
    Value result; result["matches"]=Value(Json::arrayValue); std::size_t visited=0;
    std::vector<std::pair<std::string,unsigned>> pending{{directory,0}};
    while(!pending.empty() && visited<10000) {
        auto [parent,depth]=pending.back(); pending.pop_back();
        for(const auto& name : root.list(parent)) {
            if(++visited>10000)break;
            const auto relative=(fs::path(parent)/name).generic_string();
            if(name.find(pattern)!=name.npos)result["matches"].append(relative);
            try { const auto st=root.stat(relative); if(S_ISDIR(st.st_mode) && depth<8)pending.emplace_back(relative,depth+1); }
            catch(const Error&) {}
            if(result["matches"].size()>=1024) { result["truncated"]=true; return result; }
        }
    }
    result["visited"]=Json::UInt64(visited); result["truncated"]=visited>=10000; return result;
}
Value boot_targets(const Root& root, const Root* esp) {
    Value result; result["targets"]=Value(Json::arrayValue);
    const auto linux_info=linux_detect(root,esp);
    for(const auto& entry : linux_info["boot_entries"]) {
        Value target; target["target"]="linux"; target["entry"]=entry["id"]; target["components_present"]=entry["components_present"];
        target["backend_validated"]=false; result["targets"].append(target);
    }
    const auto windows=windows_detect(root,esp);
    if(windows["detected"].asBool()) { Value target; target["target"]="windows"; target["entry"]="windows"; target["components_present"]=windows["efi_loader"].asBool() && windows["bcd"].asBool(); target["backend_validated"]=false; result["targets"].append(target); }
    result["backend_state"]="BLOCKED: no accepted Uke stock/EFI/Aloha routing backend";
    return result;
}
Value boot_request(const Root& root, const Root& esp, const std::string& target, const std::string& entry) {
    require((target=="linux" || target=="windows") && identifier(entry), "invalid-boot-target", "Select an exact detected Linux or Windows entry");
    const auto targets=boot_targets(root,&esp); bool matched=false;
    for(const auto& candidate : targets["targets"])if(candidate["target"]==target && candidate["entry"]==entry && candidate["components_present"].asBool())matched=true;
    require(matched,"invalid-boot-target","Boot entry is absent or required components are missing");
    Value request; request["schema"]=1; request["target"]=target; request["entry"]=entry; request["one_shot"]=true;
    request["request_id"]=operation_id(); request["created_utc"]=utc(); request["state"]="VALIDATED_COMPONENTS_ONLY";
    request["backend_validated"]=false; request["execute_allowed"]=false;
    return request;
}
} // namespace ure
