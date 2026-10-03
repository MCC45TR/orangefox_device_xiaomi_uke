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
        "display preview FRAMEBUFFER_WIDTH FRAMEBUFFER_HEIGHT THEME_WIDTH THEME_HEIGHT PERCENT",
        "display settings-load DIRECTORY", "display settings-save DIRECTORY PERCENT (dedicated private storage; no mounts)",
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
        "linux audit --root ROOT [--esp ESP] [--output PRIVATE_JSON]",
        "linux rescue-plan REQUEST --root ROOT [--esp ESP] --output PLAN",
        "linux rescue-execute PLAN --root ROOT [--esp ESP] --journal NEW_DIR --confirm SHA256",
        "linux rescue-inspect JOURNAL",
        "storage preflight --image IMAGE|--object ID --profile PROFILE [--sector-size 512/4096]",
        "filesystem capabilities",
        "filesystem plan REQUEST --image IMAGE|--object ID --profile PROFILE --output PLAN",
        "filesystem execute PLAN --image IMAGE --journal NEW_DIR --confirm SHA256",
        "filesystem inspect-journal JOURNAL --image IMAGE",
        "filesystem resume|rollback|cancel JOURNAL --image IMAGE --confirm SHA256",
        "partition job-plan REQUEST --image IMAGE|--object WHOLE_DISK_ID --profile PROFILE --output PLAN",
        "partition job-execute PLAN --image IMAGE --journal NEW_DIR --confirm SHA256 (prepare filesystems, then userdata/GPT; live writes blocked)",
        "stock image-inspect IMAGE (verify raw or Android sparse logical content; no writes)",
        "stock job-plan REQUEST --output PLAN (all six image LUNs, declared model/SKU and pinned stock OS payloads)",
        "stock job-execute PLAN --journal NEW_DIR --confirm SHA256 (prepare every original/replacement before any write; live writes blocked)",
        "stock job-inspect JOURNAL | stock job-resume|job-rollback|job-cancel JOURNAL --confirm SHA256",
        "partition job-inspect JOURNAL --image IMAGE",
        "partition job-resume|job-rollback|job-cancel JOURNAL --image IMAGE --confirm SHA256",
        "btrfs plan REQUEST --root ROOT --profile PROFILE --output PLAN",
        "btrfs execute PLAN --root ROOT --journal DIR --confirm SHA256",
        "btrfs snapshot-plan SOURCE PARENT NAME --root ROOT --profile PROFILE --output NEW_STORE",
        "btrfs snapshot-execute STORE --root ROOT --confirm SHA256",
        "btrfs send-plan SNAPSHOT [PARENT_SNAPSHOT] --root ROOT --profile PROFILE --output NEW_STORE",
        "btrfs send-capture STORE --root ROOT --confirm SHA256",
        "btrfs send-verify|backup-inspect STORE; btrfs stream-check FILE",
        "btrfs subvolume-info RELATIVE --root ROOT",
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
        "backup store-export DIR --chunk INDEX (verified stored chunk; binary stdout, JSON errors on stderr)",
        "backup tree-plan RELATIVE --root ROOT --profile PROFILE --output NEW_STORE",
        "backup tree-capture STORE --root ROOT --confirm PLAN_SHA256 (also resumes verified files)",
        "backup tree-verify STORE (offline namespace, metadata and data verification)",
        "backup tree-inspect STORE (review metadata pages and capture state; does not verify data)",
        "backup tree-restore STORE --output NEW_DIRECTORY --confirm PLAN_SHA256",
        "restore plan BACKUP_DIR --image IMAGE|--object STABLE_ID --profile PROFILE --output PLAN [--sector-size 4096]",
        "restore execute PLAN --image IMAGE|--object STABLE_ID --journal DIR --confirm SHA256",
        "restore inspect JOURNAL --image IMAGE|--object STABLE_ID",
        "restore resume|rollback|cancel JOURNAL --image IMAGE|--object STABLE_ID --confirm SHA256",
        "restore stream-plan MANIFEST --image IMAGE|--object STABLE_ID --profile PROFILE --output PLAN",
        "restore stream-backup-plan PLAN --output BEFORE_PLAN",
        "restore host-receipt PLAN --before BEFORE_STORE --after AFTER_STORE --output RECEIPT",
        "restore stream-begin PLAN --receipt FILE|- --image IMAGE|--object STABLE_ID --journal DIR --confirm SHA256",
        "restore stream-status JOURNAL --image IMAGE|--object STABLE_ID",
        "restore stream-chunk JOURNAL --chunk INDEX --image IMAGE|--object STABLE_ID --confirm SHA256 [--packet FILE] (exact before+after bytes on stdin)",
        "restore stream-finish|stream-rollback|stream-cancel JOURNAL --image IMAGE|--object STABLE_ID --confirm SHA256",
        "boot plan linux|windows ENTRY --root ROOT --esp ESP --output REQUEST",
        "boot route-inventory --esp ESP --variables DIR (read-only UEFI options/default inventory)",
        "boot route-plan REQUEST --esp ESP --variables DIR --output PLAN",
        "boot route-execute PLAN --esp ESP --variables DIR --journal NEW_DIR --confirm SHA256 (private EFI file fixtures only; no reboot)",
        "boot route-history JOURNAL; boot route-inspect JOURNAL --esp ESP --variables DIR",
        "boot route-recover|route-cancel|route-consume-fixture|route-fallback-fixture JOURNAL --esp ESP --variables DIR --confirm SHA256",
        "boot route-ack-fixture JOURNAL --esp ESP --variables DIR --receipt PRIVATE_JSON --confirm SHA256 (simulated result; never physical boot proof)",
        "diagnose all|recovery|kernel|display|touch|usb|storage|boot|power|thermal|network|android",
        "gpt stock-preview INPUTS --capacity-bytes BYTES --lun 0..5 --profile PROFILE --output DIRECTORY",
        "gpt map --image IMAGE --sector-size 512/4096 or --object WHOLE_DISK_ID",
        "gpt layout-preview REQUEST --image IMAGE|--object WHOLE_DISK_ID --profile PROFILE [--output PRIVATE_JSON]",
        "gpt layout-plan REQUEST --image IMAGE|--object WHOLE_DISK_ID --profile PROFILE --output PLAN (GPT metadata only; no filesystem move/resize/format)",
        "gpt stock-plan INPUTS --image IMAGE --lun 0..5 --profile PROFILE [--identity-backup ORIGINAL_GPT] --output PLAN",
        "report --output REPORT.json", "crypto detect|info --image IMAGE", "btrfs capabilities|subvolumes|usage|scrub-status|balance-status|device-stats --root ROOT",
        "wim info|verify --image IMAGE", "ntfs info --image IMAGE", "btrfs check --image IMAGE",
        "android info|slots|super", "network status", "help"
    })result["commands"].append(command);
    result["notes"]="Commands emit versioned JSON. Runtime dependencies and device acceptance remain separate.";
    return result;
}
int dispatch(std::vector<std::string> args) {
    const bool binary_output=args.size()>1 && args[0]=="backup" && (args[1]=="export" || args[1]=="store-export");
    try {
        if(management_command(args)) {
            const auto data=management_dispatch(std::move(args)); auto result=envelope(data);
            const bool failed=data.isObject() && data.isMember("successful") && !data["successful"].asBool();
            if(failed)result["result"]="operation-error";
            std::cout<<json(result); return failed ? 2 : 0;
        }
        auto root_option=option(args,"--root"), system_option=option(args,"--system-root"), esp_option=option(args,"--esp");
        const auto image_option=option(args,"--image"), output_option=option(args,"--output"), sector_option=option(args,"--sector-size");
        const auto content_option=option(args,"--content-file"), profile_option=option(args,"--profile"), journal_option=option(args,"--journal"), confirm_option=option(args,"--confirm");
        const auto chunk_size=option(args,"--chunk-size"), chunk_index=option(args,"--chunk");
        const auto object_option=option(args,"--object");
        const auto before_option=option(args,"--before"),after_option=option(args,"--after"),receipt_option=option(args,"--receipt"),packet_option=option(args,"--packet");
        const auto lun_option=option(args,"--lun"),capacity_option=option(args,"--capacity-bytes"),identity_backup=option(args,"--identity-backup");
        require(std::count(args.begin(),args.end(),"--json")<=1,"invalid-options","Duplicate --json option");
        args.erase(std::remove(args.begin(),args.end(),"--json"),args.end());
        require(!args.empty(),"usage","A command is required");
        const auto operation=args.size()>1 ? args[1] : std::string();
        if(args[0]=="display")require(!root_option && !system_option && !esp_option && !image_option && !object_option && !output_option &&
            !sector_option && !content_option && !profile_option && !journal_option && !confirm_option && !chunk_size && !chunk_index &&
            !before_option && !after_option && !receipt_option && !packet_option && !lun_option && !capacity_option && !identity_backup,
            "invalid-options","Display commands accept only their explicit positional arguments");
        const bool stock_preview=args[0]=="gpt" && operation=="stock-preview",stock_plan=args[0]=="gpt" && operation=="stock-plan";
        const bool layout=args[0]=="gpt" && (operation=="layout-preview" || operation=="layout-plan");
        require(!layout || (!root_option && !esp_option && !journal_option && !confirm_option && !chunk_size && !chunk_index),
            "invalid-options","Layout preview and planning select only a whole storage object, firmware profile and optional private output");
        require(!lun_option || stock_preview || stock_plan,"invalid-options","LUN applies only to stock GPT reconstruction");
        require(!capacity_option || stock_preview,"invalid-options","Explicit capacity applies only to a template preview");
        require(!identity_backup || stock_plan,"invalid-options","Original GPT identities apply only to a stock restore plan");
        require(!stock_preview || (!image_option && !object_option && !sector_option && !system_option && !root_option && !esp_option),
            "invalid-options","A template preview does not select a target or system context");
        require(!stock_plan || (!root_option && !esp_option),"invalid-options","Stock GPT planning selects a whole storage object");
        const bool storage_backup=args[0]=="backup" && operation=="storage-plan";
        const bool tree_backup=args[0]=="backup" && (operation=="tree-plan" || operation=="tree-capture" || operation=="tree-verify" || operation=="tree-restore" || operation=="tree-inspect");
        if(tree_backup)require(!system_option && !esp_option && !image_option && !object_option && !sector_option && !content_option &&
            !journal_option && !chunk_size && !chunk_index && !before_option && !after_option && !receipt_option && !packet_option &&
            ((operation!="tree-verify" && operation!="tree-restore" && operation!="tree-inspect") || !root_option),"invalid-options","Tree backups select only their filesystem source and private store");
        const bool restore=args[0]=="restore";
        const bool stream_plan=restore && operation=="stream-plan",host_receipt=restore && operation=="host-receipt";
        const bool stream_begin=restore && operation=="stream-begin",stream_chunk=restore && operation=="stream-chunk";
        const bool stream_metadata=host_receipt || (restore && operation=="stream-backup-plan");
        const bool stream_mutation=stream_begin || stream_chunk || (restore && (operation=="stream-finish" || operation=="stream-rollback" || operation=="stream-cancel"));
        require(!restore || (!root_option && !esp_option),"invalid-options","Raw restore selects a storage object, not a filesystem root or ESP");
        require(!restore || !output_option || operation=="plan" || stream_plan || stream_metadata,"invalid-options","Restore output applies only to plans or host receipts");
        require(!stream_metadata || (!image_option && !object_option && !system_option),"invalid-options","Stream metadata commands do not select or access a target");
        require((!before_option && !after_option) || host_receipt,"invalid-options","Host stores apply only to receipt verification");
        require(!receipt_option || stream_begin,"invalid-options","A host receipt applies only to stream begin");
        require(!packet_option || stream_chunk,"invalid-options","A binary packet applies only to stream chunk input");
        require(!(args[0]=="backup" && operation=="store-export") || (!root_option && !system_option && !esp_option),"invalid-options","Stored chunk export does not open a source or system context");
        require(!sector_option || ((args[0]=="gpt" || storage_backup || restore) && image_option && operation!="verify"),"invalid-options","Sector size applies only to storage image operations");
        require(!object_option || ((args[0]=="gpt" || storage_backup || restore) && !image_option && operation!="verify"),"invalid-options","Select one storage image or live object");
        require(!content_option || (args[0]=="editor" && operation=="plan"),"invalid-options","Content file applies only to editor plans");
        require(!profile_option || (tree_backup && operation=="tree-plan") || storage_backup || stream_plan || ((args[0]=="editor" || args[0]=="backup" || restore) && operation=="plan") ||
            (args[0]=="gpt" && (operation=="backup" || operation=="compare" || operation=="repair-plan" || operation=="restore-plan" || operation=="layout-preview" || operation=="layout-plan" || stock_preview || stock_plan)),"invalid-options","Profile does not apply to this operation");
        require(!journal_option || stream_begin || ((args[0]=="transaction" || args[0]=="gpt" || restore) && operation=="execute") || (args[0]=="backup" && operation=="capture"),"invalid-options","Journal applies only to transaction execution or backup capture");
        require(!confirm_option || (tree_backup && (operation=="tree-capture" || operation=="tree-restore")) || (args[0]=="transaction" && (operation=="execute" || operation=="rollback" || operation=="resume" || operation=="cancel")) ||
            (args[0]=="gpt" && (operation=="execute" || operation=="rollback" || operation=="resume")) ||
            stream_mutation || (restore && (operation=="execute" || operation=="rollback" || operation=="resume" || operation=="cancel")),"invalid-options","Confirmation applies only to transaction mutation");
        require(!chunk_size || storage_backup || (args[0]=="backup" && operation=="plan"),"invalid-options","Chunk size applies only to backup planning");
        require(!chunk_index || binary_output || stream_chunk,"invalid-options","Chunk index applies only to backup export or stream restore input");
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
        if(command=="display" && operation=="preview" && args.size()==7) {
            const auto width=number(args[2]),height=number(args[3]),theme_width=number(args[4]),theme_height=number(args[5]);
            require(width>=320 && height>=320 && width<=16384 && height<=16384 && theme_width>=320 && theme_height>=320 &&
                theme_width<=32768 && theme_height<=32768,"invalid-display","Invalid preview geometry");
            const auto layout=display_layout(static_cast<int>(width),static_cast<int>(height),static_cast<double>(width)/static_cast<double>(theme_width),
                static_cast<double>(height)/static_cast<double>(theme_height),display_scale_parse(args[6]));
            data["scale_percent"]=layout.percent; data["density"]=layout.density; data["canvas_width"]=layout.canvas_width; data["canvas_height"]=layout.canvas_height;
            data["uniform_density"]=true; data["physical_test_record"]=false; data["gui_rendering"]=false;
        } else if(command=="display" && operation=="settings-load" && args.size()==3)data=display_settings_load(args[2]);
        else if(command=="display" && operation=="settings-save" && args.size()==4)data=display_settings_save(args[2],display_scale_parse(args[3]));
        else if(command=="help" || command=="--help")data=usage();
        else if(command=="capabilities" && args.size()==1)data=capabilities(system);
        else if(command=="device" && args.size()==2 && (args[1]=="info" || args[1]=="firmware"))data=diagnose(system,"android");
        else if(command=="storage" && args.size()==2 && (args[1]=="inventory" || args[1]=="graph" || args[1]=="mounts" || args[1]=="health"))data=storage_graph(system);
        else if(command=="storage" && args.size()==3 && args[1]=="usage")data=storage_usage(system,args[2]);
        else if(command=="storage" && args.size()==3 && args[1]=="inspect") {
            auto selected=storage_select(system,args[2]); data["identity"]=selected.identity;
            data["content"]=selected.identity["partition"]==true ? filesystem_probe(selected.descriptor.get()) :
                gpt_inspect(selected.descriptor.get(),selected.identity["logical_sector_bytes"].asUInt());
            data["read_only"]=true; data["private_record"]=true;
        } else if(stream_metadata && args.size()==3 && output_option) {
            const auto plan=json_file(args[2]);
            if(host_receipt && before_option && after_option)data=restore_host_receipt(plan,*before_option,*after_option);
            else if(operation=="stream-backup-plan")data=restore_stream_backup_plan(plan);
            else throw Error("unknown-command","Both complete host stores are required for attestation");
            if(operation=="stream-backup-plan" && fs::exists(*output_option))
                require(json(json_file(*output_option))==json(data),"wrong-backup","Existing source backup plan differs; select a new output path");
            else save_json(*output_option,data);
        } else if(restore && args.size()==3 && (image_option || object_option)) {
            const auto sector=sector_option.value_or("4096");
            require(sector=="512" || sector=="4096","invalid-sector","Image sector size must be 512 or 4096");
            const bool write=operation=="execute" || operation=="resume" || operation=="rollback" || stream_begin || stream_chunk || operation=="stream-rollback";
            auto selected=image_option ? storage_image(*image_option,sector=="512" ? 512U : 4096U,write) : storage_select(system,*object_option,true);
            if(operation=="plan" && profile_option && output_option) { data=restore_plan(system,selected,args[2],*profile_option); save_json(*output_option,data); }
            else if(operation=="execute" && journal_option && confirm_option)data=restore_execute(system,selected,json_file(args[2]),*journal_option,*confirm_option);
            else if(operation=="inspect")data=restore_inspect(system,selected,args[2]);
            else if(operation=="resume" && confirm_option)data=restore_resume(system,selected,args[2],*confirm_option);
            else if(operation=="rollback" && confirm_option)data=restore_rollback(system,selected,args[2],*confirm_option);
            else if(operation=="cancel" && confirm_option)data=restore_cancel(system,selected,args[2],*confirm_option);
            else if(stream_plan && profile_option && output_option) { data=restore_stream_plan(system,selected,json_file(args[2]),*profile_option); save_json(*output_option,data); }
            else if(stream_begin && receipt_option && journal_option && confirm_option) {
                const auto receipt=*receipt_option=="-" ? restore_receipt_input(STDIN_FILENO) : json_file(*receipt_option);
                data=restore_stream_begin(system,selected,json_file(args[2]),receipt,*journal_option,*confirm_option);
            } else if(operation=="stream-status")data=restore_stream_status(system,selected,args[2]);
            else if(stream_chunk && chunk_index && confirm_option) {
                Fd packet; if(packet_option)packet=image(*packet_option);
                data=restore_stream_chunk(system,selected,args[2],number(*chunk_index),packet_option ? packet.get() : STDIN_FILENO,*confirm_option);
            } else if(operation=="stream-finish" && confirm_option)data=restore_stream_finish(system,selected,args[2],*confirm_option);
            else if(operation=="stream-rollback" && confirm_option)data=restore_stream_rollback(system,selected,args[2],*confirm_option);
            else if(operation=="stream-cancel" && confirm_option)data=restore_stream_cancel(system,selected,args[2],*confirm_option);
            else throw Error("unknown-command","Incomplete or unsupported raw restore command");
        } else if(stock_preview && args.size()==3 && lun_option && capacity_option && profile_option && output_option) {
            const auto lun=number(*lun_option); require(lun<6,"invalid-lun","Select UFS LUN 0 through 5");
            data=gpt_stock_preview(args[2],number(*capacity_option),static_cast<unsigned>(lun),*profile_option,*output_option);
        } else if(command=="gpt" && operation=="verify" && args.size()==3 && !image_option && !object_option)data=gpt_backup_verify(args[2]);
        else if(command=="gpt" && (image_option || object_option)) {
            const auto sector=sector_option.value_or("4096");
            require(sector=="512" || sector=="4096","invalid-sector","Image sector size must be 512 or 4096");
            auto selected=image_option ? storage_image(*image_option,sector=="512" ? 512U : 4096U,operation=="execute" || operation=="rollback") : storage_select(system,*object_option);
            if(operation=="inspect" && args.size()==2)data=gpt_inspect(selected.descriptor.get(),selected.identity["logical_sector_bytes"].asUInt());
            else if(operation=="map" && args.size()==2) { data=partition_map(selected,&system); if(output_option)save_json(*output_option,data); }
            else if(operation=="layout-preview" && args.size()==3 && profile_option) {
                data=partition_layout(selected,json_file(args[2]),*profile_option,&system); if(output_option)save_json(*output_option,data);
            } else if(operation=="layout-plan" && args.size()==3 && profile_option && output_option) {
                data=gpt_layout_plan(selected,json_file(args[2]),*profile_option,&system); save_json(*output_option,data);
            }
            else if(operation=="backup" && args.size()==2 && profile_option && output_option)data=gpt_backup(selected,*output_option,*profile_option,&system);
            else if(operation=="compare" && args.size()==3 && profile_option)data=gpt_compare(selected,args[2],*profile_option,&system);
            else if(operation=="repair-plan" && args.size()==2 && profile_option && output_option) {
                data=gpt_plan(selected,"gpt.repair",*profile_option,{},&system); save_json(*output_option,data);
            } else if(operation=="restore-plan" && args.size()==3 && profile_option && output_option) {
                data=gpt_plan(selected,"gpt.restore",*profile_option,args[2],&system); save_json(*output_option,data);
            } else if(stock_plan && args.size()==3 && profile_option && output_option && lun_option) {
                const auto lun=number(*lun_option); require(lun<6,"invalid-lun","Select UFS LUN 0 through 5");
                data=gpt_stock_plan(selected,args[2],static_cast<unsigned>(lun),*profile_option,identity_backup.value_or(""),&system); save_json(*output_option,data);
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
        else if(tree_backup && args.size()==3) {
            if(operation=="tree-plan") {
                require(root_option && profile_option && output_option && !confirm_option,"invalid-options","Tree planning requires source, profile and a new store");
                data=backup_tree_plan(root,args[2],*profile_option,*output_option);
            } else if(operation=="tree-capture") {
                require(root_option && confirm_option && !profile_option && !output_option,"invalid-options","Tree capture requires an explicit source root and plan confirmation");
                data=backup_tree_capture(root,args[2],*confirm_option);
            } else if(operation=="tree-restore") {
                require(output_option && confirm_option && !profile_option,"invalid-options","Tree restore requires a new destination and exact plan confirmation");
                data=backup_tree_restore(args[2],*output_option,*confirm_option);
            } else {
                require(!profile_option && !output_option && !confirm_option,"invalid-options","Offline tree review/verification uses only the backup store");
                data=operation=="tree-inspect" ? backup_tree_inspect(args[2]) : backup_tree_verify(args[2]);
            }
        } else if(command=="backup" && args.size()==3 && args[1]=="file" && root_option && output_option) {
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
            if(operation=="store-export")backup_store_export(args[2],number(*chunk_index),STDOUT_FILENO);
            else { const auto plan=json_file(args[2]); backup_export(backup_context(plan),plan,number(*chunk_index),STDOUT_FILENO); }
            return 0;
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
