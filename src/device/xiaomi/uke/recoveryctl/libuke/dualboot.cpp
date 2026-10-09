// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <istream>
#include <map>
#include <ostream>
#include <set>
#include <sstream>

namespace ure {
namespace {
constexpr std::uint64_t mib=1048576;
void fields(const Value& value,const std::set<std::string>& permitted) {
    require(value.isObject(),"invalid-dualboot-request","Expected a dualboot settings object");
    for(const auto& name:value.getMemberNames())require(permitted.contains(name),"invalid-dualboot-request","Unknown dualboot setting: "+name);
}
Value allocation(const Value& input,const char* name,bool enabled,const char* filesystem,bool selectable=false) {
    Value row; row["role"]=name; row["filesystem"]=filesystem;
    if(!enabled) {
        require(!input.isMember(name),"disabled-dualboot-allocation","Do not supply a size for a disabled partition");
        row["size"]="0"; row["unit"]="MiB"; return row;
    }
    require(input.isMember(name),"missing-dualboot-allocation","Supply the size and unit for every selected partition");
    const auto& choice=input[name]; fields(choice,selectable ? std::set<std::string>{"size","unit","filesystem"} : std::set<std::string>{"size","unit"});
    require(choice["size"].isString() && choice["unit"].isString(),"invalid-dualboot-size","Sizes and units must be explicit strings");
    row["size"]=choice["size"]; row["unit"]=choice["unit"];
    require(row["unit"]=="MB" || row["unit"]=="MiB" || row["unit"]=="GB" || row["unit"]=="GiB" || row["unit"]=="%",
        "invalid-layout-unit","Select MB, MiB, GB, GiB or %");
    require(layout_size_bytes(row["size"].asString(),row["unit"].asString(),1024*mib)>0,"empty-dualboot-allocation","Selected partitions need a positive size");
    if(selectable && choice.isMember("filesystem")) {
        require(choice["filesystem"]=="ext4" || choice["filesystem"]=="btrfs" || choice["filesystem"]=="f2fs","invalid-layout-filesystem","Select ext4, Btrfs or F2FS for Linux");
        row["filesystem"]=choice["filesystem"];
    }
    return row;
}
std::string sealed(Value value) { value.removeMember("plan_sha256"); return sha256(json(value)); }
void validate(const Value& plan) {
    require(plan["schema"]==1 && plan["operation"]=="dualboot.setup" && hash_valid(plan["plan_sha256"].asString()) && sealed(plan)==plan["plan_sha256"].asString(),
        "invalid-dualboot-plan","Invalid or changed dualboot plan");
    require(plan["gpt"]["layout"]["placement"]=="after_userdata" && plan["gpt"]["layout"]["advanced_record_edits"].empty() &&
        plan["gpt"]["layout"]["pool"]["source"]=="ORIGINAL_USERDATA_ONLY" && plan["writes_other_partition_payloads"]==false,
        "invalid-dualboot-plan","Dualboot setup is restricted to original userdata and necessary GPT metadata");
    const auto canonical=dualboot_layout_request(plan["request"]);
    for(const auto& row:plan["gpt"]["layout"]["rows"])if(row["role"]=="esp" && row["enabled"]==true)
        require(row["bytes"].isUInt64() && row["bytes"].asUInt64()>=512000000ULL,"esp-too-small","An ESP requires at least 512 MB after alignment; 512 MiB is recommended");
    auto normalized=plan["gpt"]["layout"]["request"];
    require(normalized["record_edits"].isArray() && normalized["record_edits"].empty(),"invalid-dualboot-plan","Setup does not accept other GPT record edits");
    normalized.removeMember("record_edits");
    for(auto& row:normalized["rows"])row.removeMember("partuuid");
    require(json(canonical)==json(normalized),"invalid-dualboot-plan","The selected setup choices differ from the GPT request");
    const auto& userdata=plan["gpt"]["layout"]["rows"][0];
    require(userdata["role"]=="userdata" && userdata["partuuid"]==userdata["previous"]["partuuid"] && userdata["start_lba"]==userdata["previous"]["start_lba"],
        "invalid-dualboot-plan","Setup must preserve the original userdata start and GUID");
    const bool erase=canonical["userdata_policy"]=="recreate";
    require(plan["gpt"]["layout"]["userdata_policy"]==canonical["userdata_policy"] && plan["data_loss"]==erase &&
        plan["confirmation_phrase"]==(erase ? "ERASE USERDATA" : "PRESERVE USERDATA"),"invalid-dualboot-plan","Userdata policy and consent must match the actual operation");
}
}
void dualboot_validate_plan(const Value& plan) { validate(plan); }
Value dualboot_layout_request(const Value& input) {
    fields(input,{"schema","format","linux_enabled","windows_enabled","esp_enabled","separate_linux_boot","userdata_policy","userdata_filesystem","esp","linux_boot","linux","windows"});
    require(input["schema"]==1 && input["format"]=="uke-dualboot-request","invalid-dualboot-request","Select version 1 dualboot settings");
    for(const auto* name:{"linux_enabled","windows_enabled","separate_linux_boot"})
        require(input[name].isBool(),"invalid-dualboot-selection","Linux, Windows and separate boot choices must be explicit booleans");
    const bool linux_selected=input["linux_enabled"].asBool(),windows=input["windows_enabled"].asBool(),boot=input["separate_linux_boot"].asBool();
    require(!input.isMember("esp_enabled") || input["esp_enabled"].isBool(),"invalid-dualboot-selection","ESP selection must be an explicit boolean");
    const bool esp=input.get("esp_enabled",true).asBool();
    require(!windows || esp,"windows-requires-esp","Windows requires an ESP; select ESP or disable Windows");
    require(linux_selected || windows,"no-dualboot-system","Select Linux, Windows or both");
    require(!boot || linux_selected,"linux-boot-without-linux","A separate Linux boot partition requires Linux");
    require(input["userdata_policy"]=="preserve" || input["userdata_policy"]=="recreate","invalid-userdata-policy","Explicitly choose userdata shrink or erase and recreate");
    require(input["userdata_filesystem"]=="f2fs" || input["userdata_filesystem"]=="ext4","invalid-layout-filesystem","Select the installed supported userdata filesystem");
    Value request; request["schema"]=1; request["format"]="ure-layout-request"; request["placement"]="after_userdata";
    request["mode"]=input["userdata_policy"]=="recreate" ? "advanced" : "standard"; request["userdata_policy"]=input["userdata_policy"];
    request["rows"]=Value(Json::arrayValue);
    Value data; data["role"]="userdata"; data["size"]=""; data["unit"]="remaining"; data["filesystem"]=input["userdata_filesystem"]; request["rows"].append(data);
    request["rows"].append(allocation(input,"esp",esp,"fat32"));
    if(boot)request["rows"].append(allocation(input,"linux_boot",true,"ext4"));
    else require(!input.isMember("linux_boot"),"disabled-dualboot-allocation","Do not supply separate boot sizes when it is disabled");
    request["rows"].append(allocation(input,"linux",linux_selected,"ext4",true));
    request["rows"].append(allocation(input,"windows",windows,"ntfs")); return request;
}
Value dualboot_plan(const Root& system,const StorageTarget& target,const Value& input,const std::string& profile) {
    const auto request=dualboot_layout_request(input);
    const bool image=target.identity["kind"]=="regular-image";
    require(image || (input["userdata_policy"]=="recreate" && input["userdata_filesystem"]=="f2fs"),
        "unsupported-live-dualboot-policy","Live setup supports explicit F2FS userdata erase and recreate only; preserve/shrink and ext4 userdata are available only for regular images");
    const auto image_job=image ? partition_job_plan(system,target,request,profile) : Value();
    const auto gpt=image ? image_job["gpt"] : gpt_layout_plan(target,request,profile,&system); const auto& layout=gpt["layout"];
    for(const auto& row:layout["rows"])if(row["role"]=="esp" && row["enabled"]==true)
        require(row["bytes"].asUInt64()>=512000000ULL,"esp-too-small","An ESP requires at least 512 MB (default: 512 MiB) after alignment");
    const auto capabilities=filesystem_capabilities();
    for(const auto& row:layout["rows"])if(row["enabled"]==true) {
        require(row["bytes"].asUInt64()>=32*mib,"unsupported-filesystem-size","Every selected partition and remaining userdata needs at least 32 MiB");
        if(row["filesystem"]=="btrfs")require(row["bytes"].asUInt64()>=128*mib,"unsupported-filesystem-size","Btrfs requires at least 128 MiB in this setup workflow");
        Value available; const auto type=row["filesystem"]=="fat32" ? "vfat" : row["filesystem"].asString();
        for(const auto& entry:capabilities["filesystems"])if(entry["filesystem"]==type)available=entry;
        require(available["format_available"]==true && available["check_available"]==true,"missing-tool","A selected filesystem formatter or independent checker is unavailable");
    }
    Value plan; plan["schema"]=1; plan["operation"]="dualboot.setup"; plan["operation_id"]=operation_id(); plan["created_utc"]=utc();
    plan["firmware_profile"]=profile; plan["target_identity"]=target.identity; plan["request"]=input; plan["gpt"]=gpt;
    if(image)plan["image_job"]=image_job;
    plan["writes_other_partition_payloads"]=false; plan["data_loss"]=input["userdata_policy"]=="recreate";
    plan["physical_test_record"]=false; plan["private_record"]=true; plan["read_only"]=true;
    plan["execution_backend"]=image ? "regular-image-partition-job" : "live-uke-f2fs-recreate-requires-preflight";
    plan["live_backend_implemented"]=!image;
    plan["live_device_acceptance"]=false;
    plan["live_write_admission"]=image ? "not-applicable" : "current-device-preflight-and-external-USB-journal-required";
    plan["commands"]=Value(Json::arrayValue);
    Value inspect; inspect["operation"]="verify-original-userdata-and-GPT"; inspect["writes"]=false; plan["commands"].append(inspect);
    Value stage; stage["operation"]="prepare-filesystems-and-durable-journal"; stage["writes"]=false;
    stage["scope"]=image ? "external-staging-only" : "external-USB-GPT-journal"; plan["commands"].append(stage);
    if(!image) {
        plan["device_commands"]=dualboot_device_commands(plan);
        for(const auto& command:plan["device_commands"])plan["commands"].append(command);
    }
    for(const auto& row:layout["rows"])if(image && row["enabled"]==true) {
        Value command; command["role"]=row["role"]; command["filesystem"]=row["filesystem"]; command["offset"]=row["offset"]; command["bytes"]=row["bytes"];
        command["operation"]=row["role"]=="userdata" && input["userdata_policy"]=="preserve" ? "verified-offline-shrink" : "erase-and-format";
        command["writes"]=true; command["scope"]="original-userdata-only"; plan["commands"].append(command);
        if(image && command["operation"]=="erase-and-format") {
            const auto role=row["role"].asString(),type=row["filesystem"]=="fat32" ? "vfat" : row["filesystem"].asString();
            auto formatter=filesystem_format_command(type,role=="userdata" ? "USERDATA" : role=="linux_boot" ? "URE_BOOT" : "URE_"+role,"<owned-staging-fd>");
            formatter["operation"]="format-private-stage"; formatter["role"]=row["role"]; formatter["scope"]="external-staging-only";
            formatter["destination_placeholder"]="An inherited descriptor for the private staged filesystem; never the whole LUN";
            plan["commands"].append(formatter);
        }
    }
    std::vector<Value> metadata; for(const auto& range:gpt["after"])metadata.push_back(range);
    std::sort(metadata.begin(),metadata.end(),[](const Value& left,const Value& right) {
        const auto rank=[](const Value& value) { const auto name=value["name"].asString();
            return name=="backup_table" ? 0 : name=="backup_header" ? 1 : name=="primary_table" ? 2 : name=="primary_header" ? 3 : 4; };
        return rank(left)<rank(right);
    });
    for(const auto& range:metadata) {
        Value command; command["operation"]="write-reviewed-GPT-region"; command["region"]=range["name"]; command["offset"]=range["offset"];
        command["bytes"]=range["bytes"]; command["sha256"]=range["sha256"]; command["writes"]=true; plan["commands"].append(command);
    }
    Value check; check["operation"]="readback-filesystems-GPT-and-protected-ranges"; check["writes"]=false; plan["commands"].append(check);
    plan["command_representation"]="Native bounded operations; no executable shell command strings";
    plan["confirmation_phrase"]=plan["data_loss"]==true ? "ERASE USERDATA" : "PRESERVE USERDATA";
    plan["warnings"]=layout["warnings"];
    plan["warnings"].append("The selected OS sizes use the original userdata capacity. Userdata receives the aligned remainder; no other partition supplies space.");
    plan["warnings"].append("A partition layout does not install an OS or a bootloader. Physical device acceptance remains separate from source, image and VM tests.");
    if(!input.get("esp_enabled",true).asBool())plan["warnings"].append("No ESP will be created. Linux needs an existing compatible ESP or a separately configured non-UEFI boot route.");
    if(image)plan["warnings"].append("The image executor needs up to three original-userdata copies plus 64 MiB in a private journal.");
    else {
        plan["warnings"].append("Live application supports explicit F2FS userdata erase and recreate only. The installed encryption map must already be opened and idle; F2FS metadata must already be mounted read-only with norecovery. Setup never opens encryption or changes metadata.");
        plan["warnings"].append("Use a private journal on external USB storage. GPT restoration cannot recover erased userdata. After application or GPT restoration, reboot recovery before accessing any partition nodes.");
        plan["warnings"].append("Runtime locks coordinate recovery components and do not prevent privileged raw shell writes. Forced restart recovery is not yet physically accepted.");
    }
    plan["layout_bar"]=partition_layout_bar(layout,80); plan["plan_sha256"]=sealed(plan); validate(plan); return plan;
}
std::string dualboot_preview(const Value& plan) {
    validate(plan); std::ostringstream out; out<<"Dualboot setup\n";
    out<<(plan["data_loss"]==true ? "DATA LOSS: existing Android userdata will be erased.\n" : "Preserve mode: only a verified supported unencrypted shrink is available.\n");
    out<<"Allocation source: original userdata only. Other partition payloads are protected.\n";
    out<<"Backend: "<<plan["execution_backend"].asString()<<"\n\n";
    std::string bar(80,' '); const std::array<char,5> symbols{'D','E','B','L','W'};
    for(const auto& segment:plan["layout_bar"]) {
        const auto role=segment["role"].asString(); char symbol='.';
        if(role=="userdata")symbol=symbols[0]; else if(role=="esp")symbol=symbols[1]; else if(role=="linux_boot")symbol=symbols[2]; else if(role=="linux")symbol=symbols[3]; else if(role=="windows")symbol=symbols[4];
        const auto begin=segment["x"].asUInt(),width=segment["width"].asUInt(); require(begin<=80 && width<=80-begin,"invalid-dualboot-plan","Preview segment exceeds its bar");
        for(unsigned x=begin;x<begin+width;++x)bar[x]=symbol;
    }
    out<<'['<<bar<<"]\nD=userdata E=ESP B=linux_boot L=Linux W=Windows\n\n";
    out<<std::fixed<<std::setprecision(2);
    for(const auto& row:plan["gpt"]["layout"]["rows"])if(row["enabled"]==true) {
        out<<row["role"].asString()<<"  "<<row["filesystem"].asString()<<"  "<<static_cast<double>(row["bytes"].asUInt64())/static_cast<double>(mib)<<" MiB  [LBA "
           <<row["start_lba"].asUInt64()<<".."<<row["end_lba"].asUInt64()<<"]\n";
    }
    out<<"\nOperations to review:\n";
    unsigned number=0; for(const auto& command:plan["commands"]) {
        out<<++number<<". "<<command["operation"].asString();
        if(command.isMember("role"))out<<" / "<<command["role"].asString()<<" / "<<command["filesystem"].asString();
        if(command.isMember("region"))out<<" / "<<command["region"].asString();
        if(command.isMember("offset"))out<<" / offset "<<command["offset"].asUInt64()<<" / bytes "<<command["bytes"].asUInt64();
        if(command.isMember("tool")) { out<<" / "<<command["tool"].asString(); for(const auto& arg:command["argv"])out<<' '<<std::quoted(arg.asString()); }
        out<<'\n';
    }
    out<<"\nReview notes:\n";
    for(const auto& warning:plan["warnings"])out<<"- "<<warning.asString()<<'\n';
    out<<"\nNo changes have been applied.\nPlan: "<<plan["plan_sha256"].asString()<<"\nRequired data policy: "<<plan["confirmation_phrase"].asString()<<"\n"; return out.str();
}
Value dualboot_image_execute(const Root& system,StorageTarget& target,const Value& plan,const fs::path& journal,const std::string& confirmation,const std::string& data_policy) {
    validate(plan);
    require(confirmation==plan["plan_sha256"].asString() && data_policy==plan["confirmation_phrase"].asString(),"confirmation-required","Confirm the exact preview hash and userdata data-loss policy");
    require(target.identity["kind"]=="regular-image","live-repartition-unavailable","A live dualboot writer and installed Android userdata policy are not accepted yet");
    require(json(target.identity)==json(plan["target_identity"]),"stale-device","The reviewed dualboot target has changed");
    const auto current=gpt_layout_plan(target,plan["gpt"]["layout"]["request"],plan["firmware_profile"].asString(),&system);
    for(const auto* field:{"before","after","current_table","desired_table","layout"})
        require(json(current[field])==json(plan["gpt"][field]),"stale-plan","Current geometry differs from the dualboot preview");
    const auto& job=plan["image_job"];
    require(job["operation"]=="partition.apply-layout" && json(job["gpt"] )==json(plan["gpt"]) && json(job["target_identity"])==json(plan["target_identity"]),
        "invalid-dualboot-plan","The reviewed filesystem job must match the exact setup target and GPT plan");
    auto result=partition_job_execute_reviewed(system,target,job,journal,job["plan_sha256"].asString(),plan);
    result["dualboot_plan_sha256"]=plan["plan_sha256"]; return result;
}

int dualboot_shell(const std::vector<std::string>& args,std::istream& input,std::ostream& output) {
    std::map<std::string,std::string> options;
    const std::set<std::string> permitted{"--image","--sector-size","--profile","--output","--journal"};
    for(std::size_t at=0;at<args.size();at+=2) {
        require(permitted.contains(args[at]) && at+1<args.size() && !options.contains(args[at]) && !args[at+1].empty(),"invalid-options","Dualboot setup accepts --image, --sector-size, --profile, --output and --journal with explicit values");
        options.emplace(args[at],args[at+1]);
    }
    const bool image=options.contains("--image");
#ifndef __ANDROID__
    require(image,"fixture-only-command","Host interactive setup requires an explicit regular image; it never selects a PC disk");
#endif
    require(image || !options.contains("--sector-size"),"invalid-options","Live sector size comes from the kernel");
    const auto sector=options.contains("--sector-size") ? options.at("--sector-size") : "4096";
    require(sector=="512" || sector=="4096","invalid-sector","Select 512 or 4096-byte sectors");
    Root system("/");
    std::string stable;
    if(!image) {
        const auto graph=storage_graph(system); std::size_t found=0;
        for(const auto& object:graph["objects"])if(object["partition"]==true && object["label"]=="userdata") {
            ++found; stable="sysfs:"+object["parent_lun_sysfs"].asString();
        }
        require(found==1,"ambiguous-userdata","Setup requires exactly one current userdata partition and its kernel-reported parent LUN");
    }
    auto selected=image ? storage_image(options.at("--image"),sector=="4096" ? 4096U : 512U) : storage_select(system,stable,false);
    output<<"Dualboot setup\nOnly the current userdata area can supply space. All other partition payloads remain protected.\n"
          <<"1. Linux\n2. Linux with separate boot\n3. Windows\n4. Linux and Windows\n5. Linux with separate boot and Windows\n";
    auto read=[&](const std::string& prompt) {
        output<<prompt<<std::flush; std::string value;
        require(static_cast<bool>(std::getline(input,value)),"setup-cancelled","Input ended; no further action was authorized");
        require(value.size()<=128,"invalid-options","Setup input is limited to 128 characters per answer");
        return value;
    };
    const auto choice=read("Select layout [1-5]: ");
    require(choice=="1" || choice=="2" || choice=="3" || choice=="4" || choice=="5","invalid-dualboot-selection","Select one of the five listed layouts");
    const bool linux_selected=choice!="3",windows=choice=="3" || choice=="4" || choice=="5",boot=choice=="2" || choice=="5";
    Value settings; settings["schema"]=1; settings["format"]="uke-dualboot-request";
    settings["linux_enabled"]=linux_selected; settings["windows_enabled"]=windows; settings["separate_linux_boot"]=boot;
    const auto esp=windows ? "yes" : read("Create ESP [yes/no; required for new UEFI boot files]: ");
    require(esp=="yes" || esp=="no","invalid-dualboot-selection","Choose yes or no for ESP"); settings["esp_enabled"]=esp=="yes";
    output<<"1. Erase and recreate userdata (all Android user files are lost)\n";
    if(image)output<<"2. Preserve supported unencrypted userdata by offline shrink\n";
    else output<<"Live setup requires F2FS erase and recreate. Preserve/shrink is unavailable.\n";
    const auto policy=read(image ? "Select data policy [1-2]: " : "Select data policy [1]: ");
    require(policy=="1" || (image && policy=="2"),"invalid-userdata-policy","Select a listed data policy");
    settings["userdata_policy"]=policy=="1" ? "recreate" : "preserve";
    settings["userdata_filesystem"]=image ? read("Userdata filesystem [f2fs/ext4]: ") : "f2fs";
    const auto linuxfs=linux_selected ? read("Linux filesystem [ext4/btrfs/f2fs]: ") : "ext4";
    auto size=[&](const char* role) {
        const auto value=read(std::string(role)+" size and unit (example: 512 MiB, 64 GiB or 20 %): ");
        std::istringstream fields(value); std::string amount,unit,extra;
        require(static_cast<bool>(fields>>amount>>unit) && !(fields>>extra),"invalid-layout-size","Enter a decimal size followed by MB, MiB, GB, GiB or %");
        Value allocation; allocation["size"]=amount; allocation["unit"]=unit; settings[role]=allocation;
    };
    if(esp=="yes")size("esp");
    if(boot)size("linux_boot");
    if(linux_selected) { size("linux"); settings["linux"]["filesystem"]=linuxfs; }
    if(windows)size("windows");
    const auto profile=options.contains("--profile") ? options.at("--profile") : "uke-userdata-layout-v1";
    const auto plan=dualboot_plan(system,selected,settings,profile);
    const fs::path destination=options.contains("--output") ? fs::path(options.at("--output")) : fs::path("/tmp")/("uke-dualboot-"+plan["operation_id"].asString()+".json");
    save_json(destination,plan); output<<'\n'<<dualboot_preview(plan)<<"\nPrivate plan saved: "<<destination.string()<<'\n';
    if(!image) {
        const auto admission=dualboot_device_preflight(system,selected,plan);
        output<<"\nCurrent read-only device preflight:\n"<<json(admission)<<'\n';
        if(admission["eligible"]!=true) { output<<"Application is blocked by the listed device prerequisites. No changes have been applied.\n"; return 0; }
    }
    if(!options.contains("--journal")) { output<<"No application journal was selected. Review this plan before an explicit execute command.\n"; return 0; }
    if(read("Type APPLY to continue, or anything else to leave the disk unchanged: ")!="APPLY") { output<<"Cancelled; the disk is unchanged.\n"; return 0; }
    const auto hash=read("Copy the complete plan SHA-256: ");
    const auto consent=read("Type the required data policy exactly: ");
    if(!image) {
        const auto result=dualboot_device_execute(system,plan,options.at("--journal"),hash,consent);
        output<<json(result)<<'\n'; return 0;
    }
    auto writer=storage_image(options.at("--image"),sector=="4096" ? 4096U : 512U,true);
    const auto result=dualboot_image_execute(system,writer,plan,options.at("--journal"),hash,consent);
    output<<json(result)<<'\n'; return 0;
}
} // namespace ure
