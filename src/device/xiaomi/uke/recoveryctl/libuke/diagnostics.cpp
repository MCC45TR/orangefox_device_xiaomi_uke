// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "../../ure-device-identity.hpp"
#include <algorithm>
#include <map>
#include <sstream>

namespace ure {
static Value declared_identity() {
    Value result;
    result["device_name"]=ure_identity::device_name;
    result["codename"]=ure_identity::codename;
    result["soc_name"]=ure_identity::soc_name;
    result["soc_model"]=ure_identity::soc_model;
    result["soc_manufacturer"]=ure_identity::soc_manufacturer;
    result["origin"]="recovery-build-declaration";
    result["hardware_verified"]=false;
    return result;
}
static std::string read_optional(const Root& root, const std::string& path, std::size_t limit=65536) {
    try { return root.read(path,limit); } catch(const Error&) { return {}; }
}
static Value discovered_classes(const Root& system, const std::string& category, const std::vector<std::string>& fields) {
    Value values(Json::arrayValue); const auto base="sys/class/"+category;
    if(!system.exists(base))return values;
    for(const auto& name : system.list(base,256)) {
        if(!identifier(name))continue;
        std::string entry=base+"/"+name;
        try {
            const auto target=(fs::path(base)/system.link(entry)).lexically_normal().generic_string();
            require(target.starts_with("sys/devices/"),"invalid-sysfs","Class link is outside sys/devices"); entry=target;
        } catch(const Error& error) { if(error.code!="path-unavailable")continue; }
        Value item; item["name"]=name;
        for(const auto& field : fields) {
            const auto text=read_optional(system,entry+"/"+field,16384);
            if(!text.empty())item[field]=redact(text);
        }
        values.append(item);
    }
    return values;
}
Value diagnose(const Root& system, const std::string& scope) {
    static const std::vector<std::string> accepted{"all","recovery","kernel","display","touch","usb","storage","boot","power","thermal","network","android"};
    require(std::find(accepted.begin(),accepted.end(),scope)!=accepted.end(),"invalid-scope","Unknown diagnostics scope");
    Value output; output["scope"]=scope; output["read_only"]=true; output["timestamp_utc"]=utc();
    output["monotonic_ms"]=Json::UInt64(monotonic_ms()); output["observations"]=Value(Json::objectValue);
    auto& data=output["observations"];
    if(scope=="all" || scope=="recovery" || scope=="android")data["recovery_identity"]=declared_identity();
    if(scope=="all" || scope=="kernel" || scope=="recovery" || scope=="boot") {
        data["kernel_release"]=read_optional(system,"proc/sys/kernel/osrelease");
        data["modules"]=redact(read_optional(system,"proc/modules"));
        data["filesystems"]=read_optional(system,"proc/filesystems");
        data["cmdline"]=redact(read_optional(system,"proc/cmdline"));
        data["tainted"]=read_optional(system,"proc/sys/kernel/tainted");
        data["stages"]=read_optional(system,"tmp/ure-stages.jsonl");
        data["pstore"]=Value(Json::arrayValue);
        if(system.exists("sys/fs/pstore"))for(const auto& name : system.list("sys/fs/pstore",128)) {
            Value entry; entry["name"]=name; entry["text"]=redact(read_optional(system,"sys/fs/pstore/"+name,1024*1024));
            data["pstore"].append(entry);
        }
        data["pstore_cleared"]=false;
    }
    if(scope=="all" || scope=="storage")data["storage"]=storage_graph(system);
    if(scope=="all" || scope=="display") {
        data["drm"]=discovered_classes(system,"drm",{"status","enabled","modes"});
        data["backlight"]=discovered_classes(system,"backlight",{"brightness","actual_brightness","max_brightness","type"});
        data["framebuffers"]=discovered_classes(system,"graphics",{"name","virtual_size","bits_per_pixel"});
    }
    if(scope=="all" || scope=="touch")data["input"]=discovered_classes(system,"input",{"name","phys","properties","capabilities/ev","capabilities/abs","capabilities/key"});
    if(scope=="all" || scope=="usb" || scope=="network") {
        data["usb_udc"]=discovered_classes(system,"udc",{"state","current_speed","maximum_speed"});
        data["interfaces"]=discovered_classes(system,"net",{"operstate","mtu","carrier"});
        data["usb_role"]=discovered_classes(system,"usb_role",{"role"});
    }
    if(scope=="all" || scope=="power" || scope=="thermal") {
        data["power_supplies"]=discovered_classes(system,"power_supply",{"type","status","online","capacity","voltage_now","current_now","temp"});
        data["thermal"]=discovered_classes(system,"thermal",{"type","temp","policy"});
    }
    if(scope=="all" || scope=="android") {
        const auto properties=read_optional(system,"prop.default",65536);
        std::istringstream lines(properties); std::string line;
        for(;std::getline(lines,line);) {
            const auto equal=line.find('='); if(equal==line.npos)continue;
            const auto key=line.substr(0,equal);
            if(key=="ro.product.device" || key=="ro.product.model" || key=="ro.soc.manufacturer" || key=="ro.soc.model" || key=="ro.ure.soc.name" || key=="ro.build.version.release" || key=="ro.boot.slot_suffix" || key=="ro.boot.flash.locked" || key=="ro.boot.vbmeta.device_state")data["android"][key]=line.substr(equal+1);
        }
        data["android"]["fbe_state"]="BLOCKED: installed-firmware KeyMint/TEE trust not accepted";
    }
    output["root_cause_proven"]=false; output["physical_test_record"]=false; output["private_record"]=true;
    return output;
}
Value public_report(const Root& system) {
    // Raw crash records, command lines, mounts, UUIDs and installed-file paths
    // belong to the private diagnostic view. Public export is an allowlist,
    // not a promise that pattern replacement can scrub arbitrary private logs.
    const auto private_view=diagnose(system,"all");
    const auto& source=private_view["observations"];
    Value result; result["schema"]=1; result["timestamp_utc"]=utc();
    result["public_scrubbed"]=true; result["physical_test_record"]=false;
    result["root_cause_proven"]=false; result["capabilities"]=capabilities(system);
    result["kernel_release"]=redact(source["kernel_release"].asString());
    result["filesystems"]=source["filesystems"]; result["tainted"]=source["tainted"];
    result["pstore_record_count"]=source["pstore"].size(); result["pstore_cleared"]=false;
    result["android"]=source["android"];
    result["recovery_identity"]=source["recovery_identity"];
    for(const auto* category:{"drm","backlight","framebuffers","input","usb_udc","usb_role","interfaces","power_supplies","thermal"})result["class_counts"][category]=source[category].size();
    result["storage"]=Value(Json::arrayValue);
    for(const auto& object:source["storage"]["objects"]) {
        Value summary;
        for(const auto* key:{"owner","partition","bytes","logical_sector_bytes","read_only_state","write_policy"})if(object.isMember(key))summary[key]=object[key];
        summary["mount_count"]=object["mounts"].size(); result["storage"].append(summary);
    }
    result["omitted_private_fields"]="Raw pstore, command line, module addresses, stage text, device identifiers, UUIDs, mount paths and class text";
    return result;
}
Value capabilities(const Root& system) {
    const auto filesystems=read_optional(system,"proc/filesystems");
    Value result; result["schema"]=1; result["physical_test_record"]=false; result["capabilities"]=Value(Json::arrayValue);
    result["recovery_identity"]=declared_identity();
    struct Entry { const char* id; const char* name; const char* tool; const char* filesystem; const char* state; };
    static const std::vector<Entry> entries{
        {"URE-C01","Storage Graph and JSON API","","","SOURCE_PRESENT"},
        {"URE-C02","File/raw-image transactions, verified backups, interrupted resume and rollback","","","SOURCE_PRESENT"},
        {"URE-C03","Read-only diagnostics and redacted reports","","","SOURCE_PRESENT"},
        {"URE-C04","Linux distribution and package-db discovery","","","SOURCE_PRESENT"},
        {"URE-C05","Kernel/initramfs/module/BLS consistency","","","SOURCE_PRESENT"},
        {"URE-C06","Atomic editor backend and config validators","","","SOURCE_PRESENT"},
        {"URE-C07","File metadata/search and Linux/home tree backup/isolated restore","","","SOURCE_PRESENT"},
        {"URE-C08","Native distribution-aware chroot and guided repair","","","SOURCE_PRESENT"},
        {"URE-C09","Expanded recovery kernel","","","PLANNED"},
        {"URE-C10","LUKS","cryptsetup","","TOOL_REQUIRED"},
        {"URE-C11","BitLocker (deferred by owner)","","","DEFERRED_BY_OWNER"},
        {"URE-C12","Native Btrfs subvolumes, snapshots and retained-original rollback","","btrfs","KERNEL_REQUIRED"},
        {"URE-C13","Native Btrfs scrub/balance and verified send streams","","btrfs","PARTIAL"},
        {"URE-C14","GPT image transactions and staged filesystem format/repair/resize","","","SOURCE_PRESENT"},
        {"URE-C15","Android advanced manager/FBE","lpdump","","PARTIAL"},
        {"URE-C16","UEFI inventory, exact one-shot plans and fixture consumption","","","BLOCKED_DEVICE_BACKEND"},
        {"URE-C17","Correlated fixture history and preserved-default fallback","","","FIXTURE_ONLY"},
        {"URE-C18","ADB shell/exec and streamed host backups","","","PARTIAL"},
        {"URE-C19","USB network, SSH/SFTP and Wi-Fi (deferred by owner)","","","DEFERRED_BY_OWNER"},
        {"URE-C20","Windows discovery and WIM rescue","wimlib-imagex","","PARTIAL"},
        {"URE-C21","Multi-OS partition designer","","","PARTIAL"},
        {"URE-C22","Profiles/provenance and safe recovery update","","","PARTIAL"},
        {"URE-C23","UI/session/operation management","","","PARTIAL"},
        {"URE-C24","Optional extensions and forensics","","","PLANNED"}
    };
    for(const auto& definition : entries) {
        Value item; item["id"]=definition.id; item["name"]=definition.name; item["implementation_state"]=definition.state;
        item["device_support"]="UNTESTED";
        if(*definition.tool) { item["required_tool"]=definition.tool; item["tool_present"]=tool_available(definition.tool); }
        if(*definition.filesystem) {
            item["required_filesystem"]=definition.filesystem;
            item["kernel_available"]=filesystems.find(std::string("\t")+definition.filesystem+"\n")!=filesystems.npos || filesystems.starts_with(std::string(definition.filesystem)+"\n");
        }
        result["capabilities"].append(item);
    }
    result["filesystem_management"]=filesystem_capabilities();
    result["partition_management"]=partition_capabilities();
    result["platform_admission"]=platform_capabilities(system,"global-os3.0.303.0");
    result["live_storage_writer_accepted"]=false; result["btrfs_receive_implemented"]=false;
    result["device_profile_admission"]["accepted_live_profile_count"]=0;
    result["device_profile_admission"]["live_plan_allowed"]=false;
    result["device_profile_admission"]["code"]="device-profile-unaccepted";
    result["device_profile_admission"]["reason"]="Commercial model/SKU/capacity and installed firmware require separate six-LUN geometry, unit-bound GUID backups and whole boot-stack acceptance";
    result["active_scope"]["remote_transport"]="adb-only";
    result["active_scope"]["bitlocker"]=false; result["active_scope"]["ssh_sftp"]=false; result["active_scope"]["network_rescue"]=false;
    result["active_scope"]["delivery_order"]="boot manager; LUKS/Windows; ADB/backups; files/sessions; distribution/diagnostics";
    return result;
}
} // namespace ure
