// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <charconv>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <unistd.h>

namespace ure {
static std::optional<std::string> option(std::vector<std::string>& args, const std::string& key) {
    std::optional<std::string> value;
    for(auto it=args.begin();it!=args.end();) {
        if(*it!=key) { ++it; continue; }
        require(!value && it+1!=args.end(),"invalid-options","Duplicate or incomplete option: "+key);
        value=*(it+1); it=args.erase(it,it+2);
    }
    return value;
}
static Fd image(const std::string& path) {
    Fd fd(::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK)); struct stat info{};
    require(fd.get()>=0 && ::fstat(fd.get(),&info)==0 && S_ISREG(info.st_mode),
        "invalid-image","Image operations accept regular files, not a live block path");
    return fd;
}
static std::uint64_t number(const std::string& text) {
    std::uint64_t value=0; const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    require(!text.empty() && result.ec==std::errc() && result.ptr==text.data()+text.size(),"invalid-number","Expected an unsigned decimal integer");
    return value;
}
static Value usage() {
    Value result; result["schema"]=1;
    result["commands"]=Value(Json::arrayValue);
    for(const auto* command : {
        "capabilities", "device info|firmware", "storage inventory|graph|mounts|health",
        "gpt inspect --image IMAGE --sector-size 4096", "filesystem inspect|check --image IMAGE",
        "storage inspect|usage STABLE_ID (read-only live block identity or ownership observations)",
        "gpt inspect --object STABLE_ID", "gpt backup --image IMAGE|--object STABLE_ID --profile PROFILE --output DIR",
        "gpt verify DIR", "gpt compare DIR --image IMAGE|--object STABLE_ID --profile PROFILE",
        "gpt repair-plan --image IMAGE|--object STABLE_ID --profile PROFILE --output PLAN",
        "gpt restore-plan BACKUP_DIR --image IMAGE|--object STABLE_ID --profile PROFILE --output PLAN",
        "gpt execute PLAN --image IMAGE|--object STABLE_ID --journal DIR --confirm SHA256",
        "gpt rollback JOURNAL --image IMAGE|--object STABLE_ID --confirm SHA256",
        "gpt journal-inspect JOURNAL --image IMAGE|--object STABLE_ID",
        "gpt resume JOURNAL --image IMAGE|--object STABLE_ID --confirm SHA256 (readback commit only)",
        "linux detect|info|kernels|boot-entries|diagnose --root ROOT [--esp ESP]",
        "windows detect|info|boot|diagnose --root ROOT [--esp ESP]",
        "files list|search DIRECTORY [PATTERN] --root ROOT", "editor read FILE --root ROOT",
        "editor validate FILE fstab|crypttab|bls|json --root ROOT",
        "editor plan FILE --content-file CONTENT --profile PROFILE --output PLAN --root ROOT",
        "transaction validate PLAN --root ROOT", "transaction execute PLAN --journal DIR --confirm SHA256 --root ROOT",
        "transaction show JOURNAL_DIR", "transaction rollback JOURNAL_DIR --confirm SHA256 --root ROOT",
        "transaction inspect|list JOURNAL_DIR --root ROOT", "transaction resume|cancel JOURNAL_DIR --confirm SHA256 --root ROOT",
        "backup file RELATIVE --output BACKUP --root ROOT", "boot targets --root ROOT --esp ESP",
        "backup plan RELATIVE --profile PROFILE [--chunk-size BYTES] --output PLAN --root ROOT",
        "backup storage-plan --image IMAGE|--object STABLE_ID --profile PROFILE [--sector-size 4096] [--chunk-size BYTES] --output PLAN",
        "backup capture PLAN --journal DIR [--root ROOT for file plans]", "backup resume DIR [--root ROOT for file plans]", "backup verify DIR",
        "backup export PLAN --chunk INDEX [--root ROOT for file plans] (binary stdout; JSON errors on stderr)",
        "restore plan BACKUP_DIR --image IMAGE|--object STABLE_ID --profile PROFILE --output PLAN [--sector-size 4096]",
        "restore execute PLAN --image IMAGE|--object STABLE_ID --journal DIR --confirm SHA256",
        "restore inspect JOURNAL --image IMAGE|--object STABLE_ID",
        "restore resume|rollback|cancel JOURNAL --image IMAGE|--object STABLE_ID --confirm SHA256",
        "boot plan linux|windows ENTRY --root ROOT --esp ESP --output REQUEST",
        "diagnose all|recovery|kernel|display|touch|usb|storage|boot|power|thermal|network|android",
        "report --output REPORT.json", "crypto detect|info --image IMAGE", "btrfs capabilities|subvolumes|usage|scrub-status|balance-status|device-stats --root ROOT",
        "wim info|verify --image IMAGE", "ntfs info --image IMAGE", "btrfs check --image IMAGE",
        "android info|slots|super", "network status", "help"
    })result["commands"].append(command);
    result["notes"]="Commands emit versioned JSON. Runtime dependencies and device acceptance remain separate.";
    return result;
}
int dispatch(std::vector<std::string> args) {
    const bool binary_output=args.size()>1 && args[0]=="backup" && args[1]=="export";
    try {
        auto root_option=option(args,"--root"), system_option=option(args,"--system-root"), esp_option=option(args,"--esp");
        const auto image_option=option(args,"--image"), output_option=option(args,"--output"), sector_option=option(args,"--sector-size");
        const auto content_option=option(args,"--content-file"), profile_option=option(args,"--profile"), journal_option=option(args,"--journal"), confirm_option=option(args,"--confirm");
        const auto chunk_size=option(args,"--chunk-size"), chunk_index=option(args,"--chunk");
        const auto object_option=option(args,"--object");
        require(std::count(args.begin(),args.end(),"--json")<=1,"invalid-options","Duplicate --json option");
        args.erase(std::remove(args.begin(),args.end(),"--json"),args.end());
        require(!args.empty(),"usage","A command is required");
        const auto operation=args.size()>1 ? args[1] : std::string();
        const bool storage_backup=args[0]=="backup" && operation=="storage-plan";
        const bool restore=args[0]=="restore";
        require(!restore || (!root_option && !esp_option),"invalid-options","Raw restore selects a storage object, not a filesystem root or ESP");
        require(!restore || !output_option || operation=="plan","invalid-options","Restore output applies only to planning");
        require(!sector_option || ((args[0]=="gpt" || storage_backup || restore) && image_option && operation!="verify"),"invalid-options","Sector size applies only to storage image operations");
        require(!object_option || ((args[0]=="gpt" || storage_backup || restore) && !image_option && operation!="verify"),"invalid-options","Select one storage image or live object");
        require(!content_option || (args[0]=="editor" && operation=="plan"),"invalid-options","Content file applies only to editor plans");
        require(!profile_option || storage_backup || ((args[0]=="editor" || args[0]=="backup" || restore) && operation=="plan") ||
            (args[0]=="gpt" && (operation=="backup" || operation=="compare" || operation=="repair-plan" || operation=="restore-plan")),"invalid-options","Profile does not apply to this operation");
        require(!journal_option || ((args[0]=="transaction" || args[0]=="gpt" || restore) && operation=="execute") || (args[0]=="backup" && operation=="capture"),"invalid-options","Journal applies only to transaction execution or backup capture");
        require(!confirm_option || (args[0]=="transaction" && (operation=="execute" || operation=="rollback" || operation=="resume" || operation=="cancel")) ||
            (args[0]=="gpt" && (operation=="execute" || operation=="rollback" || operation=="resume")) ||
            (restore && (operation=="execute" || operation=="rollback" || operation=="resume" || operation=="cancel")),"invalid-options","Confirmation applies only to transaction mutation");
        require(!chunk_size || storage_backup || (args[0]=="backup" && operation=="plan"),"invalid-options","Chunk size applies only to backup planning");
        require(!chunk_index || binary_output,"invalid-options","Chunk index applies only to backup export");
        require(!image_option || storage_backup || restore || args[0]=="gpt" || args[0]=="filesystem" || args[0]=="crypto" || args[0]=="ntfs" || args[0]=="wim" || args[0]=="btrfs","invalid-options","Image applies only to image operations");
        if(args[0]=="btrfs" && !image_option && operation!="capabilities")require(root_option.has_value(),"root-required","Select an already mounted Btrfs root explicitly");
        Root system(system_option.value_or("/"));
        Root root(root_option.value_or("/"));
        auto backup_context=[&](const Value& plan)->const Root& {
            if(plan["source_kind"]=="regular-file") {
                require(root_option.has_value() && !system_option,"root-required","File backup requires only an explicit selected root"); return root;
            }
            require(!root_option,"invalid-options","Storage backup uses its sealed identity and system context, not a filesystem root"); return system;
        };
        std::unique_ptr<Root> esp; if(esp_option)esp=std::make_unique<Root>(*esp_option);
        Value data; const auto& command=args[0];
        if(command=="help" || command=="--help")data=usage();
        else if(command=="capabilities" && args.size()==1)data=capabilities(system);
        else if(command=="device" && args.size()==2 && (args[1]=="info" || args[1]=="firmware"))data=diagnose(system,"android");
        else if(command=="storage" && args.size()==2 && (args[1]=="inventory" || args[1]=="graph" || args[1]=="mounts" || args[1]=="health"))data=storage_graph(system);
        else if(command=="storage" && args.size()==3 && args[1]=="usage")data=storage_usage(system,args[2]);
        else if(command=="storage" && args.size()==3 && args[1]=="inspect") {
            auto selected=storage_select(system,args[2]); data["identity"]=selected.identity;
            data["content"]=selected.identity["partition"]==true ? filesystem_probe(selected.descriptor.get()) :
                gpt_inspect(selected.descriptor.get(),selected.identity["logical_sector_bytes"].asUInt());
            data["read_only"]=true; data["private_record"]=true;
        } else if(restore && args.size()==3 && (image_option || object_option)) {
            const auto sector=sector_option.value_or("4096");
            require(sector=="512" || sector=="4096","invalid-sector","Image sector size must be 512 or 4096");
            const bool write=operation=="execute" || operation=="resume" || operation=="rollback";
            auto selected=image_option ? storage_image(*image_option,sector=="512" ? 512U : 4096U,write) : storage_select(system,*object_option,true);
            if(operation=="plan" && profile_option && output_option) { data=restore_plan(system,selected,args[2],*profile_option); save_json(*output_option,data); }
            else if(operation=="execute" && journal_option && confirm_option)data=restore_execute(system,selected,json_file(args[2]),*journal_option,*confirm_option);
            else if(operation=="inspect")data=restore_inspect(system,selected,args[2]);
            else if(operation=="resume" && confirm_option)data=restore_resume(system,selected,args[2],*confirm_option);
            else if(operation=="rollback" && confirm_option)data=restore_rollback(system,selected,args[2],*confirm_option);
            else if(operation=="cancel" && confirm_option)data=restore_cancel(system,selected,args[2],*confirm_option);
            else throw Error("unknown-command","Incomplete or unsupported raw restore command");
        } else if(command=="gpt" && operation=="verify" && args.size()==3 && !image_option && !object_option)data=gpt_backup_verify(args[2]);
        else if(command=="gpt" && (image_option || object_option)) {
            const auto sector=sector_option.value_or("4096");
            require(sector=="512" || sector=="4096","invalid-sector","Image sector size must be 512 or 4096");
            auto selected=image_option ? storage_image(*image_option,sector=="512" ? 512U : 4096U,operation=="execute" || operation=="rollback") : storage_select(system,*object_option);
            if(operation=="inspect" && args.size()==2)data=gpt_inspect(selected.descriptor.get(),selected.identity["logical_sector_bytes"].asUInt());
            else if(operation=="backup" && args.size()==2 && profile_option && output_option)data=gpt_backup(selected,*output_option,*profile_option,&system);
            else if(operation=="compare" && args.size()==3 && profile_option)data=gpt_compare(selected,args[2],*profile_option,&system);
            else if(operation=="repair-plan" && args.size()==2 && profile_option && output_option) {
                data=gpt_plan(selected,"gpt.repair",*profile_option,{},&system); save_json(*output_option,data);
            } else if(operation=="restore-plan" && args.size()==3 && profile_option && output_option) {
                data=gpt_plan(selected,"gpt.restore",*profile_option,args[2],&system); save_json(*output_option,data);
            } else if(operation=="execute" && args.size()==3 && journal_option && confirm_option)data=gpt_execute(selected,json_file(args[2]),*journal_option,*confirm_option,&system);
            else if(operation=="rollback" && args.size()==3 && confirm_option)data=gpt_rollback(selected,args[2],*confirm_option,&system);
            else if(operation=="journal-inspect" && args.size()==3)data=gpt_journal_inspect(selected,args[2],&system);
            else if(operation=="resume" && args.size()==3 && confirm_option)data=gpt_resume(selected,args[2],*confirm_option,&system);
            else throw Error("unknown-command","Incomplete or unsupported GPT command");
        } else if(command=="filesystem" && args.size()==2 && args[1]=="check" && image_option) {
            auto fd=image(*image_option); data=filesystem_check(fd.get());
        } else if((command=="crypto" || command=="btrfs" || command=="wim" || command=="ntfs") && args.size()==2 &&
            (args[1]=="info" || args[1]=="verify" || args[1]=="check") && image_option) {
            auto fd=image(*image_option); data=image_tool(command,args[1],fd.get());
        } else if((command=="filesystem" || command=="crypto") && args.size()==2 && (args[1]=="inspect" || args[1]=="detect") && image_option) {
            auto fd=image(*image_option); data=filesystem_probe(fd.get());
        } else if(command=="linux" && args.size()==2) {
            require(root_option.has_value(),"root-required","Select an already mounted Linux root explicitly");
            require(args[1]=="detect" || args[1]=="info" || args[1]=="kernels" || args[1]=="boot-entries" || args[1]=="diagnose","unknown-command","Unknown Linux command");
            const auto info=linux_detect(root,esp.get());
            data=args[1]=="kernels" ? info["kernels"] : args[1]=="boot-entries" ? info["boot_entries"] : info;
        } else if(command=="windows" && args.size()==2 && (args[1]=="detect" || args[1]=="info" || args[1]=="boot" || args[1]=="diagnose")) {
            require(root_option.has_value(),"root-required","Select an already accessible Windows root explicitly"); data=windows_detect(root,esp.get());
        } else if(command=="files" && args.size()==3 && args[1]=="list" && root_option)data=files_list(root,args[2]);
        else if(command=="files" && args.size()==4 && args[1]=="search" && root_option)data=files_search(root,args[2],args[3]);
        else if(command=="editor" && args.size()==3 && args[1]=="read" && root_option) {
            const auto bytes=root.read(args[2]); require(utf8(bytes),"binary-file","Editor requires valid UTF-8 without NUL bytes");
            data["contents"]=bytes; data["sha256"]=sha256(bytes); data["private_record"]=true;
        } else if(command=="editor" && args.size()==4 && args[1]=="validate" && root_option)data=config_validate(root,args[2],args[3]);
        else if(command=="editor" && args.size()==3 && args[1]=="plan" && root_option && content_option && profile_option && output_option) {
            data=transaction_plan(root,args[2],bounded_read(*content_option),*profile_option); save_json(*output_option,data);
            data.removeMember("payload"); data["plan_saved"]=true;
        } else if(command=="transaction" && args.size()==3 && args[1]=="validate" && root_option) {
            const auto plan=json_file(args[2]); validate_plan(root,plan); data["valid"]=true; data["plan_sha256"]=plan["plan_sha256"];
        } else if(command=="transaction" && args.size()==3 && args[1]=="execute" && root_option && journal_option && confirm_option) {
            data=transaction_run(root,json_file(args[2]),*journal_option,*confirm_option);
        } else if(command=="transaction" && args.size()==3 && args[1]=="rollback" && root_option && confirm_option) {
            data=transaction_rollback(root,args[2],*confirm_option);
        } else if(command=="transaction" && args.size()==3 && args[1]=="inspect" && root_option)data=transaction_inspect(root,args[2]);
        else if(command=="transaction" && args.size()==3 && args[1]=="list" && root_option)data=transaction_list(root,args[2]);
        else if(command=="transaction" && args.size()==3 && args[1]=="resume" && root_option && confirm_option)data=transaction_resume(root,args[2],*confirm_option);
        else if(command=="transaction" && args.size()==3 && args[1]=="cancel" && root_option && confirm_option)data=transaction_cancel(root,args[2],*confirm_option);
        else if(command=="transaction" && args.size()==3 && args[1]=="show")data=json_file(fs::path(args[2])/"journal.json");
        else if(command=="backup" && args.size()==3 && args[1]=="file" && root_option && output_option) {
            data=backup_file(root,args[2],*output_option); save_json(*output_option+".manifest.json",data);
        } else if(command=="backup" && args.size()==3 && args[1]=="plan" && root_option && output_option && profile_option) {
            data=backup_plan(root,args[2],*profile_option,chunk_size ? number(*chunk_size) : 16*1024*1024); save_json(*output_option,data);
        } else if(storage_backup && args.size()==2 && (image_option || object_option) && output_option && profile_option) {
            require(!root_option,"invalid-options","Storage backup planning does not accept a filesystem root");
            const auto sector=sector_option.value_or("4096"); require(sector=="512" || sector=="4096","invalid-sector","Image sector size must be 512 or 4096");
            auto selected=image_option ? storage_image(*image_option,sector=="512" ? 512U : 4096U) : storage_select(system,*object_option,true);
            data=backup_storage_plan(system,selected,*profile_option,chunk_size ? number(*chunk_size) : 16*1024*1024); save_json(*output_option,data);
        } else if(command=="backup" && args.size()==3 && args[1]=="capture" && journal_option) {
            const auto plan=json_file(args[2]); data=backup_capture(backup_context(plan),plan,*journal_option,false);
        } else if(command=="backup" && args.size()==3 && args[1]=="resume") {
            const auto plan=json_file(fs::path(args[2])/"plan.json"); data=backup_capture(backup_context(plan),plan,args[2],true);
        } else if(command=="backup" && args.size()==3 && args[1]=="verify")data=backup_verify(args[2]);
        else if(binary_output && args.size()==3 && chunk_index) {
            const auto plan=json_file(args[2]); backup_export(backup_context(plan),plan,number(*chunk_index),STDOUT_FILENO); return 0;
        } else if(command=="boot" && args.size()==2 && args[1]=="targets" && root_option)data=boot_targets(root,esp.get());
        else if(command=="boot" && args.size()==4 && args[1]=="plan" && root_option && esp && output_option) {
            data=boot_request(root,*esp,args[2],args[3]); save_json(*output_option,data);
        } else if(command=="diagnose" && args.size()==2)data=diagnose(system,args[1]);
        else if(command=="report" && args.size()==1 && output_option) {
            data=public_report(system); save_json(*output_option,envelope(data)); data["report_saved"]=true;
        } else if(command=="network" && args.size()==2 && args[1]=="status")data=diagnose(system,"network");
        else if(command=="btrfs" && args.size()==2 && args[1]=="capabilities")data=capabilities(system);
        else if(command=="android" && args.size()==2 && args[1]=="info")data=diagnose(system,"android");
        else data=tool_operation(args,root,system);
        auto result=envelope(data);
        const bool failed=data.isObject() && data.isMember("successful") && !data["successful"].asBool();
        if(failed) { result["result"]="tool-error"; result["error"]["code"]=data.get("timed_out",false).asBool() ? "tool-timeout" : "tool-exit-status"; }
        std::cout<<json(result); return failed ? 2 : 0;
    } catch(const Error& e) {
        Value result; result["schema"]=1; result["result"]="error"; result["error"]["code"]=e.code;
        result["error"]["message"]=e.what(); (binary_output ? std::cerr : std::cout)<<json(result); return 1;
    } catch(const std::exception&) {
        Value result; result["schema"]=1; result["result"]="error"; result["error"]["code"]="internal-error";
        result["error"]["message"]="Unexpected input or runtime failure"; (binary_output ? std::cerr : std::cout)<<json(result); return 1;
    }
}
} // namespace ure
