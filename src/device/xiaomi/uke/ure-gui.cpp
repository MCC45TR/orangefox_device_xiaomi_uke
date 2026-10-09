// SPDX-License-Identifier: GPL-3.0-or-later
// Project-owned OrangeFox adapter. All storage operations use libuke directly.
#include "objects.hpp"
#include "../data.hpp"
#include "../gui.hpp"
#include "uke.h"
#include "pages.hpp"
#include "minuitwrp/minui.h"
#include "../minuitwrp/display-mirror.hpp"
#include "../ure-localization.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <fcntl.h>
#include <fcntl.h>
#include <mutex>
#include <map>
#include "gui_job.hpp"
#include "operation_lease.hpp"
#include "lifecycle_policy.hpp"
#include "recovery_services.hpp"
#include <set>
#include <thread>
#include <unistd.h>

namespace {
std::string value(const std::string& name) { std::string result; DataManager::GetValue(name,result); return result; }
struct ManagementSession {
    ure::Value variables, updates{Json::objectValue};
    std::size_t update_bytes=0;
    std::string review_locale;
    std::map<std::string,ure_locale::Message> messages;
    std::function<void(const std::string&,const ure::Root&,const ure::Value&,const std::string&)> bind_backend_control;
    std::function<void(const ure::Value&)> backend_control_result;
    int run(const std::string& command);
    void set(const std::string& name,const std::string& text) {
        ure::require(name.rfind("ure_",0)==0 && name.size()<=128 && text.size()<=128*1024,"gui-job-output-limit","GUI updates exceed their bounded publication budget; inspect the backend journal");
        const auto prior=updates.get(name,"").asString().size();
        ure::require(update_bytes-prior+text.size()<=192*1024 && (updates.isMember(name) || updates.size()<256),"gui-job-output-limit","GUI updates exceed their total publication budget; inspect the backend journal");
        update_bytes=update_bytes-prior+text.size(); updates[name]=text; variables[name]=text;
    }
    void set(const std::string& name,int number) { set(name,std::to_string(number)); }
std::unique_ptr<ure::Root> selected_root;
std::unique_ptr<ure::Editor> editor;
ure::Value pending_plan;
ure::Value pending_backup;
std::string pending_backup_directory;
std::string pending_journal;
ure::Value pending_gpt_plan;
std::string pending_gpt_journal;
std::string reviewed_gpt_journal;
ure::Value pending_stock_plan,reviewed_stock_choices;
std::string pending_stock_journal,reviewed_stock_journal;
ure::Value pending_restore_plan;
std::string pending_restore_journal,pending_restore_backup,reviewed_restore_journal;
std::string reviewed_stream_journal;
ure::Value pending_tree;
ure::Value reviewed_layout_request;
ure::Value dualboot_plan,dualboot_selection;
std::string dualboot_journal;
std::string reviewed_partition_journal;
ure::Value reviewed_partition_selection;
std::string reviewed_tree_store,reviewed_tree_root,reviewed_tree_destination;
ure::Value managed_plan,managed_selection;
std::string managed_kind,managed_journal,reviewed_filesystem_journal;
std::string edited_management_field;
ure::Value boot_plan,boot_reviewed_selection,boot_journal_selection;
std::string boot_pending_journal,boot_reviewed_journal;
std::size_t current_line=0;
std::string value(const std::string& name) const { return variables.get(name,"").asString(); }
void display_message(const std::string& variable,const ure_locale::Message& message) {
    messages[variable]=message; set(variable,message.text());
}
void publish(const ure::Value& data) {
    ure::Value view;
    if(data.isObject() && data["operation"]=="stock.restore-images") {
        // The sealed plan remains owned by this session. Display complete
        // programming extents, warnings and layout changes without repeating
        // each LUN's current, desired and reconstructed GPT tables.
        for(auto it=data.begin();it!=data.end();++it)if(it.name()!="luns")view[it.name()]=*it;
        view["gui_view_kind"]="stock-review-projection";
        view["gui_view_is_executable_plan"]=false;
        view["luns"]=ure::Value(Json::arrayValue);
        for(const auto& lun:data["luns"]) {
            ure::Value item; item["lun"]=lun["lun"]; item["identity"]=lun["identity"]; const auto& gpt=lun["gpt"];
            for(const auto* key:{"operation","plan_sha256","firmware_profile","risk","stock_lun","layout_changes","warnings"})
                if(gpt.isMember(key))item["gpt"][key]=gpt[key];
            item["gpt"]["current_table_sha256"]=ure::sha256(ure::json(gpt["current_table"]));
            item["gpt"]["desired_table_sha256"]=ure::sha256(ure::json(gpt["desired_table"]));
            for(const auto* key:{"guid_policy","original_identity_sha256","capacity","template_capacity","profile"})
                if(gpt["stock_source"].isMember(key))item["gpt"]["stock_source"][key]=gpt["stock_source"][key];
            view["luns"].append(item);
        }
    } else view=data;
    Json::StreamWriterBuilder writer; writer["indentation"]=""; writer["emitUTF8"]=true;
    set("ure_output",Json::writeString(writer,view));
}

bool choice(const std::string& name) {
    const auto selected=value(name); ure::require(selected=="0" || selected=="1","invalid-choice","Choose an explicit on/off value"); return selected=="1";
}
unsigned number(const std::string& name,unsigned maximum) {
    const auto text=value(name); unsigned result=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    ure::require(!text.empty() && parsed.ec==std::errc() && parsed.ptr==text.data()+text.size() && result<=maximum,"invalid-number","Enter a bounded whole number"); return result;
}
ure::Root os_root(const std::string& name="ure_root") {
    const auto path=value(name); ure::require(ure::fs::path(path).is_absolute() && path!="/","root-required","Select an already mounted OS directory"); return ure::Root(path);
}
std::unique_ptr<ure::Root> selected_esp() {
    const auto path=value("ure_esp"); if(path.empty())return {};
    ure::require(ure::fs::path(path).is_absolute() && path!="/","esp-required","Select an already mounted ESP directory or leave it empty"); return std::make_unique<ure::Root>(path);
}
ure::Value boot_selection() {
    ure::Value out;
    for(const auto* name:{"ure_boot_esp","ure_boot_variables","ure_boot_target","ure_boot_option","ure_boot_fallback","ure_boot_partuuid",
        "ure_boot_model","ure_boot_profile","ure_journal_parent"})out[name]=value(name);
    return out;
}
ure::Value boot_context_selection() {
    ure::Value out; out["esp"]=value("ure_boot_esp"); out["variables"]=value("ure_boot_variables"); out["journal"]=value("ure_boot_journal"); return out;
}
ure::Value boot_request_fields() {
    ure::Value out; out["schema"]=1; out["target"]=value("ure_boot_target"); out["boot_option"]=value("ure_boot_option");
    out["fallback_option"]=value("ure_boot_fallback"); out["esp_partuuid"]=value("ure_boot_partuuid");
    out["model"]=value("ure_boot_model"); out["profile"]=value("ure_boot_profile"); return out;
}
void boot_clear_review() {
    boot_plan={}; boot_reviewed_selection={}; boot_pending_journal.clear(); boot_reviewed_journal.clear(); boot_journal_selection={};
    set("ure_boot_hash",""); set("ure_boot_journal_hash",""); set("ure_boot_can_stage","0");
    for(const auto* action:{"recover","cancel","fallback-fixture"})set(std::string("ure_boot_can_")+action,"0");
}
ure::Value filesystem_request(std::uint64_t bytes) {
    ure::Value request; request["schema"]=1; request["action"]=value("ure_fs_action"); request["filesystem"]=value("ure_fs_type");
    if(request["action"]=="format") { request["erase_confirmed"]=choice("ure_fs_erase"); request["label"]=value("ure_fs_label"); }
    if(request["action"]=="resize")request["target_bytes"]=Json::UInt64(ure::layout_size_bytes(value("ure_fs_size"),value("ure_fs_unit"),bytes));
    return request;
}
ure::Value rescue_request() {
    ure::Value request; request["schema"]=1; request["action"]=value("ure_rescue_action"); request["write"]=choice("ure_rescue_write"); request["network"]=false;
    request["timeout_seconds"]=number("ure_rescue_timeout",7200);
    if(request["action"]=="shell") { ure::require(!value("ure_rescue_command").empty(),"command-required","Enter the explicit command for this isolated session"); request["shell_input"]=value("ure_rescue_command")+"\n"; }
    if(request["action"]=="module-index" || request["action"]=="initramfs-rebuild")request["kernel_release"]=value("ure_rescue_kernel");
    return request;
}
ure::Value btrfs_request(const ure::Root& root) {
    ure::Value request; request["schema"]=1; const auto action=value("ure_btrfs_action"); request["action"]=action;
    if(action=="create" || action=="snapshot" || action=="readonly" || action=="delete" || action=="rollback")request["path"]=value("ure_btrfs_path");
    if(action=="snapshot") { request["source"]=value("ure_btrfs_source"); request["read_only"]=choice("ure_btrfs_readonly"); }
    if(action=="readonly")request["read_only"]=choice("ure_btrfs_readonly");
    if(action=="delete")request["backup_snapshot"]=value("ure_btrfs_backup");
    if(action=="rollback") { request["snapshot"]=value("ure_btrfs_backup"); request["saved_path"]=value("ure_btrfs_saved"); }
    if(action=="scrub") { request["device_id"]=Json::UInt64(number("ure_btrfs_device",4096)); request["repair"]=choice("ure_btrfs_repair"); }
    if(action=="balance") { request["usage_percent"]=number("ure_btrfs_usage",90); request["chunk_limit"]=number("ure_btrfs_limit",128); }
    if(action=="resize") {
        const auto devices=ure::btrfs_native_info(root,"device-stats"); ure::require(devices["device_stats"].size()==1,"unsupported-btrfs-size","Select a single-device filesystem");
        request["target_bytes"]=Json::UInt64(ure::layout_size_bytes(value("ure_btrfs_size"),value("ure_btrfs_unit"),devices["device_stats"][0]["total_bytes"].asUInt64()));
    } return request;
}
ure::Value management_selection(const std::string& kind,const ure::Value& request={}) {
    ure::Value out; out["request"]=request; out["journal_parent"]=value("ure_journal_parent");
    if(kind=="filesystem")for(const auto* name:{"ure_raw_kind","ure_raw_source","ure_raw_sector"})out[name]=value(name);
    else if(kind=="rescue") { out["root"]=value("ure_root"); out["esp"]=value("ure_esp"); }
    else for(const auto* name:{"ure_btrfs_root","ure_btrfs_store","ure_btrfs_source","ure_btrfs_parent","ure_btrfs_name","ure_btrfs_incremental_parent"})out[name]=value(name);
    return out;
}
void review_management(const std::string& kind,ure::Value plan,const ure::Value& selection) {
    managed_kind=kind; managed_plan=std::move(plan); managed_selection=selection;
    ure::Root parent(value("ure_journal_parent"));
    managed_journal=kind=="btrfs-snapshot" || kind=="btrfs-send" ? value("ure_btrfs_store") :
        (ure::fs::path(value("ure_journal_parent"))/("ure-managed-"+managed_plan["operation_id"].asString())).string();
    auto review=managed_plan; review["journal_directory"]=managed_journal; publish(review);
    set("ure_manage_hash",managed_plan["plan_sha256"].asString());
    set("ure_manage_can_apply",kind!="filesystem" || managed_plan["target_identity"]["kind"]=="regular-image" ? "1" : "0");
    ure_locale::Message summary;
    if(kind=="filesystem") {
        const auto& request=managed_plan["request"]; summary.data(request["action"].asString()).data(" / ").data(request["filesystem"].asString()).prose(" on ").data(value("ure_raw_source")).data("\n");
        if(request["action"]=="resize")summary.prose("Requested filesystem size: ").data(std::to_string(request["target_bytes"].asUInt64()/1048576)).data(" MiB\n");
        summary.prose("Required journal space: ").data(std::to_string(managed_plan["estimated_max_journal_bytes"].asUInt64()/1048576)).data(" MiB\n");
        summary.prose("Partition boundaries stay unchanged. ").prose(managed_plan["risk"].asString());
        if(managed_plan["target_identity"]["kind"]!="regular-image")summary.prose("\nLive application is blocked by the current storage preflight.");
    } else if(kind=="rescue") {
        summary.prose("System: ").data(managed_plan["distribution_family"].asString()).prose("; action: ").data(managed_plan["request"]["action"].asString()).data("\n");
        summary.prose("Timeout: ").data(std::to_string(managed_plan["request"]["timeout_seconds"].asUInt())).prose(" seconds; automatic connections: ").data(std::to_string(managed_plan["connections"].size())).data("\n");
        summary.prose(managed_plan["risk"].asString());
    } else summary.prose(managed_plan.get("risk",managed_plan.get("coherence","Review the selected read-only snapshot, incremental parent and backup store")).asString());
    summary.prose("\nJournal: ").data(managed_journal); display_message("ure_manage_summary",summary);
    set("ure_status","Review the selected target, action, required space, warnings and journal before confirming");
}
void reviewed_management(const std::string& kind,const ure::Value& selection) {
    ure::require(managed_kind==kind && managed_plan.isObject() && managed_plan["plan_sha256"].asString()==value("ure_manage_hash"),"review-required","Review this operation before applying it");
    ure::require(ure::json(selection)==ure::json(managed_selection),"stale-plan","Selections changed; calculate and review a new operation");
}
ure::StorageTarget backup_target(const ure::Root& system,bool writable=false) {
    if(value("ure_raw_kind")=="live") {
        ure::require(!writable,"firmware-unverified","Live restore awaits the firmware/slot/snapshot and ownership backend");
        return ure::storage_select(system,value("ure_raw_source"),true);
    }
    ure::require(value("ure_raw_kind")=="image","invalid-target","Select an image or a Storage Graph identity");
    const auto sector=value("ure_raw_sector");
    ure::require(sector=="512" || sector=="4096","invalid-sector","Image sector size must be 512 or 4096");
    ure::require(ure::fs::path(value("ure_raw_source")).is_absolute(),"invalid-path","Select an absolute storage image path");
    return ure::storage_image(value("ure_raw_source"),sector=="512" ? 512U : 4096U,writable);
}
void review_backup() {
    const auto manifest="/tmp/ure-backup-plan-"+pending_backup["operation_id"].asString()+".json";
    ure::save_json(manifest,pending_backup);
    auto review=pending_backup; review["chunk_count"]=review["chunks"].size(); review.removeMember("chunks");
    review["manifest_path"]=manifest; review["local_backup_directory"]=pending_backup_directory; publish(review);
    set("ure_backup_hash",pending_backup["plan_sha256"].asString());
    set("ure_status","Review source identity, coherence and destination before capture or host transfer");
}
void validate_backup_selection(const ure::Value& plan) {
    const bool live=plan["source_kind"]=="live-block";
    ure::require(live || plan["source_kind"]=="storage-image","wrong-source","Select a storage backup manifest");
    ure::require(value("ure_raw_kind")== (live ? "live" : "image") &&
        value("ure_raw_source")==plan["source_identity"][live ? "stable_id" : "path"].asString(),"stale-plan","Source selection changed; review its own backup plan");
    if(!live)ure::require(value("ure_raw_sector")==std::to_string(plan["source_identity"]["logical_sector_bytes"].asUInt()),"stale-plan","Image sector size changed");
}
ure::StorageTarget gpt_target(const ure::Root& system,bool writable=false) {
    if(value("ure_gpt_kind")=="live") {
        ure::require(!writable,"firmware-unverified","Live GPT writes require the unfinished firmware/slot/snapshot backend");
        return ure::storage_select(system,value("ure_gpt_source"));
    }
    ure::require(value("ure_gpt_kind")=="image","invalid-target","Select an image or a live Storage Graph identity");
    const auto sector=value("ure_gpt_sector");
    ure::require(sector=="512" || sector=="4096","invalid-sector","Image sector size must be 512 or 4096");
    ure::require(ure::fs::path(value("ure_gpt_source")).is_absolute(),"invalid-path","Select an absolute GPT image path");
    return ure::storage_image(value("ure_gpt_source"),sector=="512" ? 512U : 4096U,writable);
}
void clear_gpt_review() {
    pending_gpt_plan=ure::Value(); pending_gpt_journal.clear(); reviewed_gpt_journal.clear();
    reviewed_partition_journal.clear(); reviewed_partition_selection=ure::Value();
    reviewed_layout_request=ure::Value();
    set("ure_layout_graph",""); set("ure_layout_review","Calculate and review the current selections before applying");
    for(const auto* name:{"ure_gpt_plan_hash","ure_gpt_journal_hash","ure_gpt_can_execute","ure_gpt_can_rollback","ure_gpt_can_resume",
        "ure_partition_journal_hash","ure_partition_can_resume","ure_partition_can_rollback","ure_partition_can_cancel"})set(name,"");
}
void dualboot_clear() {
    dualboot_plan={}; dualboot_selection={}; dualboot_journal.clear();
    set("ure_db_hash",""); set("ure_db_can_apply","0"); set("ure_db_erase_ack","0");
    set("ure_db_confirmation",""); set("ure_layout_graph","");
    set("ure_db_summary","Review the selected systems and sizes before applying.");
}
ure::Value dualboot_request() {
    ure::Value request; request["schema"]=1; request["format"]="uke-dualboot-request";
    request["linux_enabled"]=choice("ure_db_linux"); request["windows_enabled"]=choice("ure_db_windows");
    request["esp_enabled"]=choice("ure_db_esp"); request["separate_linux_boot"]=choice("ure_db_boot");
    request["userdata_policy"]="recreate"; request["userdata_filesystem"]=value("ure_db_userdata_fs");
    for(const auto* role:{"esp","linux_boot","linux","windows"}) {
        const bool enabled=std::string_view(role)=="esp" ? request["esp_enabled"].asBool() :
            std::string_view(role)=="linux_boot" ? request["separate_linux_boot"].asBool() :
            std::string_view(role)=="linux" ? request["linux_enabled"].asBool() : request["windows_enabled"].asBool();
        if(!enabled)continue;
        const auto prefix="ure_db_"+std::string(role)+"_";
        request[role]["size"]=value(prefix+"size"); request[role]["unit"]=value(prefix+"unit");
        if(std::string_view(role)=="linux")request[role]["filesystem"]=value("ure_db_linux_fs");
    }
    return request;
}
ure::Value dualboot_choices() {
    ure::Value selection; selection["request"]=dualboot_request();
    for(const auto* key:{"ure_gpt_kind","ure_gpt_source","ure_gpt_sector","ure_journal_parent"})selection[key]=value(key);
    return selection;
}
ure::Value partition_selection() {
    ure::Value selected; for(const auto* key:{"ure_gpt_kind","ure_gpt_source","ure_gpt_sector","ure_partition_journal"})selected[key]=value(key); return selected;
}
void clear_stock_review() {
    pending_stock_plan=ure::Value(); reviewed_stock_choices=ure::Value(); pending_stock_journal.clear(); reviewed_stock_journal.clear();
    for(const auto* name:{"ure_stock_job_hash","ure_stock_job_journal_hash","ure_stock_job_summary","ure_stock_job_can_execute",
        "ure_stock_job_can_resume","ure_stock_job_can_rollback","ure_stock_job_can_cancel"})set(name,"");
}
ure::Value stock_selection() {
    ure::Value selected;
    for(const auto* key:{"ure_stock_inputs","ure_stock_job_images","ure_stock_job_originals","ure_stock_job_model","ure_stock_job_sku",
        "ure_stock_job_boot","ure_stock_job_slots","ure_stock_job_super","ure_stock_job_reset","ure_stock_job_zero","ure_stock_job_whole_boot","ure_journal_parent"})selected[key]=value(key);
    return selected;
}
ure::Value stock_request() {
    const ure::fs::path images(value("ure_stock_job_images")),originals(value("ure_stock_job_originals"));
    ure::require(images.is_absolute() && (originals.empty() || originals.is_absolute()),"invalid-path","Select absolute image and optional original GPT directories");
    ure::Value request; request["schema"]=1; request["format"]="ure-stock-job-request"; request["firmware_profile"]="global-os3.0.303.0";
    request["stock_inputs_directory"]=value("ure_stock_inputs"); request["model"]=value("ure_stock_job_model"); request["sku"]=value("ure_stock_job_sku");
    request["erase_android_data"]=choice("ure_stock_job_reset"); request["zero_sparse_holes"]=choice("ure_stock_job_zero");
    request["boot_payload_layout"]=choice("ure_stock_job_whole_boot") ? "reviewed-whole-partition" : "preserve-tail";
    request["luns"]=ure::Value(Json::arrayValue); request["payloads"]=ure::Value(Json::arrayValue);
    for(unsigned lun=0;lun<6;++lun) {
        ure::Value row; row["lun"]=lun; row["image"]=(images/("lun"+std::to_string(lun)+".img")).lexically_normal().string();
        if(!originals.empty())row["identity_backup"]=(originals/("lun"+std::to_string(lun))).lexically_normal().string();
        request["luns"].append(row);
    }
    auto add=[&](unsigned lun,const std::string& name,const std::string& file) { ure::Value row; row["lun"]=lun; row["label"]=name; row["filename"]=file; request["payloads"].append(row); };
    const auto slots=value("ure_stock_job_slots"); ure::require(slots=="a" || slots=="b" || slots=="both","invalid-choice","Choose slot A, B or both explicitly");
    if(choice("ure_stock_job_boot"))for(const auto* slot:{"a","b"})if(slots==slot || slots=="both") {
        for(const auto* name:{"boot","dtbo","init_boot","recovery","vendor_boot","vbmeta"})add(4,std::string(name)+"_"+slot,std::string(name)+".img");
        add(0,"vbmeta_system_"+std::string(slot),"vbmeta_system.img");
    }
    if(choice("ure_stock_job_super"))add(0,"super","super.img");
    if(request["erase_android_data"].asBool()) { add(0,"metadata","metadata.img"); add(0,"userdata","userdata.img"); }
    return request;
}
ure_locale::Message stock_summary(const ure::Value& plan,const std::string& journal) {
    ure_locale::Message text;
    text.prose("Declared model: ").data(plan["request"]["model"].asString()).prose("; SKU: ").data(plan["request"]["sku"].asString()).prose("\nImage workflow; tablet identity and boot acceptance pending.\n");
    for(const auto& lun:plan["luns"])text.prose("LUN ").data(std::to_string(lun["lun"].asUInt())).data(": ").data(std::to_string(lun["identity"]["bytes"].asUInt64()/1048576)).data(" MiB\n");
    text.prose("Selected OS payloads: ").data(std::to_string(plan["request"]["payloads"].size())).prose("\nRequired journal space: ").data(std::to_string(plan["estimated_journal_bytes"].asUInt64()/1048576)).prose(" MiB\nJournal: ").data(journal).data("\n");
    text.prose("Boot programming policy: ").data(plan["request"]["boot_payload_layout"].asString()).data("\n");
    for(const auto& row:plan["regions"])if(row["role"]=="payload")text.data(row["name"].asString()).prose(": program ").data(std::to_string(row["bytes"].asUInt64()/1048576)).prose(" MiB; preserve tail ").data(std::to_string((row["destination_capacity"].asUInt64()-row["bytes"].asUInt64())/1048576)).data(" MiB\n");
    for(const auto& warning:plan["warnings"])text.prose(warning.asString()).data("\n");
    return text;
}
ure::Value layout_request() {
    ure::Value request; request["schema"]=1; request["format"]="ure-layout-request"; request["rows"]=ure::Value(Json::arrayValue);
    request["mode"]=value("ure_layout_mode"); request["placement"]=value("ure_layout_placement"); request["userdata_policy"]=value("ure_layout_userdata_policy");
    const auto edits=value("ure_layout_record_edits"); ure::require(edits.size()<=65536,"size-limit","Advanced edit list exceeds its limit");
    request["record_edits"]=ure::parse_json(edits);
    for(const auto* role:{"esp","linux","windows","userdata"}) {
        const std::string prefix="ure_layout_"+std::string(role); ure::Value row; row["role"]=role;
        row["size"]=value(prefix+"_size"); row["unit"]=value(prefix+"_unit"); row["filesystem"]=value(prefix+"_filesystem");
        const auto guid=value(prefix+"_guid"); if(!guid.empty())row["partuuid"]=guid;
        request["rows"].append(row);
    } return request;
}
void clear_restore_review() {
    pending_restore_plan=ure::Value(); pending_restore_journal.clear(); pending_restore_backup.clear(); reviewed_restore_journal.clear();
    for(const auto* name:{"ure_restore_plan_hash","ure_restore_journal_hash","ure_restore_can_execute","ure_restore_can_resume","ure_restore_can_rollback","ure_restore_can_cancel"})
        set(name,"");
}
void clear_stream_review() {
    reviewed_stream_journal.clear();
    for(const auto* name:{"ure_stream_journal_hash","ure_stream_can_rollback","ure_stream_can_finish","ure_stream_can_cancel"})set(name,"");
}
void invalidate_reviews() {
    dualboot_clear();
    pending_plan={}; pending_backup={}; pending_gpt_plan={}; pending_stock_plan={}; pending_restore_plan={}; pending_tree={}; managed_plan={}; boot_plan={};
    reviewed_filesystem_journal.clear(); reviewed_gpt_journal.clear(); reviewed_stock_journal.clear(); reviewed_restore_journal.clear(); reviewed_stream_journal.clear();
    reviewed_partition_journal.clear(); boot_reviewed_journal.clear();
    for(const auto* key:{"ure_manage_hash","ure_plan_hash","ure_backup_hash","ure_tree_hash","ure_gpt_plan_hash","ure_gpt_journal_hash",
        "ure_partition_journal_hash","ure_restore_plan_hash","ure_restore_journal_hash","ure_stream_journal_hash","ure_boot_hash","ure_boot_journal_hash",
        "ure_stock_job_hash","ure_stock_job_journal_hash","ure_fs_journal_hash","ure_journal_hash"})set(key,"");
    for(const auto* key:{"ure_manage_can_apply","ure_gpt_can_execute","ure_restore_can_execute","ure_stock_job_can_execute","ure_boot_can_stage"})set(key,"0");
    for(const auto* prefix:{"ure_fs","ure_gpt","ure_partition","ure_restore","ure_stream","ure_stock_job"})
        for(const auto* action:{"resume","rollback","cancel","finish"})set(std::string(prefix)+"_can_"+action,"0");
}
static std::string text_prefix(const std::string& text,std::size_t limit) {
    auto size=std::min(text.size(),limit);
    // Preserve complete UTF-8 scalars at the preview boundary. Editor input
    // is already validated; binary console output remains a private record.
    for(unsigned tail=0;tail<4 && !ure::utf8(std::string_view(text.data(),size));++tail)if(size)--size;
    ure::require(ure::utf8(std::string_view(text.data(),size)),"gui-binary-output","This output is binary; inspect its private record through ADB");
    return text.substr(0,size);
}
void refresh_editor() {
    const auto rows=editor->lines();
    current_line=std::min(current_line,rows.size()-1);
    set("ure_line",rows[current_line]);
    set("ure_line_number",std::to_string(current_line+1)+" / "+std::to_string(rows.size()));
    set("ure_preview",text_prefix(editor->text(),65536));
    pending_plan=ure::Value(); pending_journal.clear(); set("ure_plan_hash","");
}
};
}
// Render-thread-owned sample. Only its two font references change while a size
// is selected; the theme and every real hit rectangle remain untouched.
class UreScalePreview : public GUIObject, public RenderObject {
    FontResource* source_font_=nullptr;
    FontResource* source_description_=nullptr;
    void* font_=nullptr;
    void* description_=nullptr;
    COLOR background_,foreground_,secondary_,accent_;
    int selected_=0,applied_=0;
    void release() {
        if(font_)twrpTruetype::gr_ttf_freeFont(font_);
        if(description_)twrpTruetype::gr_ttf_freeFont(description_);
        font_=description_=nullptr;
    }
    bool refresh() {
        int selected=0,applied=0;
        try {
            selected=ure::display_scale_parse(value("ure_scale_choice"));
            applied=ure::display_scale_parse(value("ure_ui_scale_applied"));
        } catch(const ure::Error&) {
            if(!selected_ && !applied_)return false;
            selected_=applied_=0; release();
            DataManager::SetValue("ure_scale_warning","Enter a whole percentage from 50 to 100.");
            return true;
        }
        if(selected==selected_ && applied==applied_)return false;
        release(); selected_=selected; applied_=applied;
        if(source_font_)font_=twrpTruetype::gr_ttf_scaleFont(source_font_->GetResource(),selected,applied);
        if(source_description_)description_=twrpTruetype::gr_ttf_scaleFont(source_description_->GetResource(),selected,applied);
        DataManager::SetValue("ure_scale_warning",selected<=70 ?
            "Small controls at 70% or below may be hard to tap. A mouse can help." :
            "Preview only. The full interface changes after Apply.");
        return true;
    }
    static void color(const COLOR& c) { gr_color(c.red,c.green,c.blue,c.alpha); }
public:
    explicit UreScalePreview(xml_node<>* node):GUIObject(node) {
        LoadPlacement(FindNode(node,"placement"),&mRenderX,&mRenderY,&mRenderW,&mRenderH);
        auto* style=FindNode(node,"sample");
        source_font_=LoadAttrFont(style,"font"); source_description_=LoadAttrFont(style,"secondaryfont");
        background_=LoadAttrColor(style,"background"); foreground_=LoadAttrColor(style,"color");
        secondary_=LoadAttrColor(style,"secondarycolor"); accent_=LoadAttrColor(style,"accent");
    }
    ~UreScalePreview() override { release(); }
    int Update() override { return refresh() ? 2 : 0; }
    int Render() override {
        if(!isConditionTrue())return 0;
        refresh(); color(background_); gr_fill(mRenderX,mRenderY,mRenderW,mRenderH);
        if(!font_ || !description_ || applied_<=0 || mRenderW<=0 || mRenderH<=0)return 0;
        const auto size=[&](int units) { return std::max(1,scale_theme_min(units)*selected_/applied_); };
        const int pad=std::max(1,scale_theme_min(28));
        const int icon=size(72),gap=size(24),stroke=size(4),title_h=twrpTruetype::gr_ttf_getMaxFontHeight(font_);
        const int detail_h=twrpTruetype::gr_ttf_getMaxFontHeight(description_);
        const int row_h=std::max(icon,title_h+detail_h+size(12));
        const int button_h=size(128),button_y=mRenderY+pad+row_h+size(28);
        if(button_y+button_h+pad>mRenderY+mRenderH || icon+gap+pad*2>=mRenderW)return 0;
        const int x=mRenderX+pad,y=mRenderY+pad,tx=x+icon+gap;
        color(foreground_);
        // A folder outline demonstrates the same target size and icon gap.
        gr_fill(x,y+icon/4,icon,stroke); gr_fill(x,y+icon-stroke,icon,stroke);
        gr_fill(x,y+icon/4,stroke,icon*3/4); gr_fill(x+icon-stroke,y+icon/4,stroke,icon*3/4);
        gr_fill(x,y+icon/8,icon/2,stroke); gr_fill(x,y+icon/8,stroke,icon/8);
        const auto localize=[](const std::string& key,const std::string& fallback) { return gui_lookup(key,fallback); };
        gr_textEx_scaleW(tx,y,ure_locale::translate("Files and folders",localize).c_str(),font_,mRenderX+mRenderW-pad-tx,0,0);
        color(secondary_);
        gr_textEx_scaleW(tx,y+title_h+size(12),ure_locale::translate("Example menu description",localize).c_str(),description_,mRenderX+mRenderW-pad-tx,0,0);
        color(accent_); gr_fill(x,button_y,mRenderW-pad*2,button_h);
        gr_color(255,255,255,255);
        gr_textEx_scaleW(x+size(24),button_y+(button_h-title_h)/2,ure_locale::translate("Sample button",localize).c_str(),font_,mRenderW-pad*2-size(48),0,0);
        return 0;
    }
};
void ure_create_scale_preview(xml_node<>* node,GUIObject*& object,RenderObject*& render) {
    auto* preview=new UreScalePreview(node); object=preview; render=preview;
}
// Render-thread-owned graph cache. Action threads publish a bounded JSON value;
// they never retain a widget pointer or alter scanout resources.
class UrePartitionMap : public GUIObject, public RenderObject {
    std::string cached_;
    ure::Value segments_;
    int cached_width_=-1;
    void refresh() {
        const auto data=value("ure_layout_graph"); if(data==cached_ && cached_width_==mRenderW)return;
        cached_=data; cached_width_=mRenderW; segments_=ure::Value();
        try { ure::require(data.size()<=65536,"size-limit","Layout graph exceeds its limit");
            if(!data.empty() && mRenderW>0)segments_=ure::partition_layout_bar(ure::parse_json(data),static_cast<unsigned>(mRenderW));
        } catch(const ure::Error&) { /* An invalid or absent preview paints only the neutral bar. */ }
    }
public:
    explicit UrePartitionMap(xml_node<>* node):GUIObject(node) { LoadPlacement(FindNode(node,"placement"),&mRenderX,&mRenderY,&mRenderW,&mRenderH); }
    int Update() override { const auto before=cached_; const auto width=cached_width_; refresh(); return before==cached_ && width==cached_width_ ? 0 : 2; }
    int Render() override {
        if(!isConditionTrue())return 0;
        refresh(); gr_color(96,96,96,255); gr_fill(mRenderX,mRenderY,mRenderW,mRenderH);
        for(const auto& segment:segments_) {
            const auto name=segment["role"].asString();
            if(name=="esp")gr_color(210,160,64,255);
            else if(name=="linux")gr_color(80,172,116,255);
            else if(name=="linux_boot")gr_color(64,164,176,255);
            else if(name=="windows")gr_color(76,140,216,255);
            else if(name=="userdata")gr_color(164,116,208,255);
            else gr_color(96,96,96,255);
            gr_fill(mRenderX+segment["x"].asInt(),mRenderY,segment["width"].asInt(),mRenderH);
        } return 0;
    }
};
void ure_create_partition_map(xml_node<>* node,GUIObject*& object,RenderObject*& render) {
    auto* graph=new UrePartitionMap(node); object=graph; render=graph;
}
// Called only while the render thread rebuilds theme resources. Existing input
// events and object hit rectangles remain in framebuffer coordinates.
void ure_gui_density(float& scale_w,float& scale_h,int width,int height) {
    static bool initialized=false;
    try {
        if(!initialized) {
            initialized=true;
            int percent=75;
            try { percent=ure::display_settings_load("/mnt/uke-settings")["scale_percent"].asInt(); }
            catch(const ure::Error&) { /* Absent/unavailable storage uses the tablet default. */ }
            DataManager::SetValue("ure_ui_scale_percent",percent);
            DataManager::SetValue("ure_scale_directory","/mnt/uke-settings");
            DataManager::SetValue("ure_scale_status","Choose a scale; save to dedicated mounted storage for reuse");
        }
        const int percent=ure::display_scale_parse(value("ure_ui_scale_percent"));
        const auto layout=ure::display_layout(width,height,scale_w,scale_h,percent);
        scale_w=layout.density; scale_h=layout.density;
        DataManager::SetValue("ure_canvas_width",layout.canvas_width);
        DataManager::SetValue("ure_canvas_height",layout.canvas_height);
        DataManager::SetValue("ure_ui_scale_applied",percent);
        DataManager::SetValue("ure_scale_choice",percent);
    } catch(const ure::Error&) {
        DataManager::SetValue("ure_canvas_width",0); DataManager::SetValue("ure_canvas_height",0);
        DataManager::SetValue("ure_ui_scale_applied",100);
        DataManager::SetValue("ure_scale_status","Scale unavailable for this framebuffer; original theme scaling retained");
    }
}
bool ure_gui_variable(const std::string& name,std::string& output) {
    if(name.compare(0,4,"ure_")==0) {
        return ure_locale::display_variable(name,DataManager::GetStrValue(name),
            [](const std::string& key,const std::string& fallback) { return gui_lookup(key,fallback); },output);
    }
    int width=0,height=0; DataManager::GetValue("ure_canvas_width",width); DataManager::GetValue("ure_canvas_height",height);
    if(width<=0 || height<=0)return false;
    // Density changes control sizes, while the logical viewport changes anchors.
    // Explicit stock-theme names avoid changing stored selections or guessing
    // whether an arbitrary numeric variable is a dimension, color or preference.
    struct Anchor { const char* name; int inset; };
    static constexpr Anchor trailing[]={
        {"btn_float_x",132},{"col1_x_neg",48},{"col2_x",372},{"col2_x_text",484},
        {"bs_del",144},{"bs_del_i",108},{"ab_btn1_x",84},{"ab_btn2_x",228},{"ab_btn3_x",372},
        {"main_switch_icon_x",134},{"ab_menu_x",108},{"ab_menu_sort_x",492},{"ab_menu_sort_text_x",442},
        {"btn_raised_right_x",48},{"card1_mtag_x",336},{"card1_ctag_x",332},
        {"snackbar_button_x",304},{"snackbar_button_2_x",568},
        {"fm_sort_x",264},{"fm_menu_x",126}
    };
    static constexpr Anchor widths[]={
        {"input_w",96},{"content_w",96},{"console_width",96},{"terminal_width",48},
        {"slidervalue_w",176},{"nav_path_w",432},{"fm_input_w",264},{"credits_width",48}
    };
    for(const auto& anchor:trailing)if(name==anchor.name) { output=std::to_string(width-anchor.inset); return true; }
    for(const auto& anchor:widths)if(name==anchor.name) { output=std::to_string(std::max(1,width-anchor.inset)); return true; }
    const int nav_width=std::max(1,width-96);
    static constexpr const char* items[]={"nav_item_1","nav_item_2","nav_item_3","nav_item_4","nav_item_5"};
    static constexpr const char* pills[]={"np_pill_x1","np_pill_x2","np_pill_x3","np_pill_x4","np_pill_x5"};
    static constexpr const char* cards[]={"ws_cards_x1","ws_cards_x2","ws_cards_x3","ws_cards_x4"};
    for(int index=1;index<=5;++index) {
        const int center=48+nav_width*(2*index-1)/10;
        if(name==items[index-1]) { output=std::to_string(center); return true; }
        if(name==pills[index-1]) { output=std::to_string(center-96); return true; }
        if(index<=4 && name==cards[index-1]) { output=std::to_string(28+(width-56)*(2*index-1)/8); return true; }
    }
    if(name=="nav_item_w")output=std::to_string(nav_width/5);
    else if(name=="back_button_x")output=std::to_string(width/6);
    else if(name=="slideout_button_x")output=std::to_string(width-width/6);
    else if(name=="row_navbtn_w")output=std::to_string(std::max(1,width/3-42));
    else if(name=="btn_w")output=std::to_string(std::max(1,(width-138)/2));
    else if(name=="tab_third_w")output=std::to_string(width/3);
    else if(name=="tab_second_x")output=std::to_string(width/3);
    else if(name=="tab_third_x")output=std::to_string(width*2/3);
    else if(name=="tab_indicator_second_x")output=std::to_string(width/3+24);
    else if(name=="tab_indicator_third_x")output=std::to_string(width*2/3+24);
    else if(name=="tab_w")output=std::to_string(std::max(1,width/3-48));
    else if(name=="status_right_x" || name=="battery_12_x" || name=="battery_24_x" || name=="battery_2_12_x" || name=="battery_2_24_x") {
        const int inset=name=="status_right_x" ? 20 : name=="battery_12_x" ? 200 : name=="battery_24_x" ? 140 : name=="battery_2_12_x" ? 180 : 120;
        output=std::to_string(width-inset-DataManager::GetIntValue("status_indent_right"));
    }
    else if(name=="ab_btn01_x")output=std::to_string(width/2-72);
    else if(name=="ab_btn02_x")output=std::to_string(width/2+72);
    else if(name=="db_left_x")output=std::to_string(width/2-160);
    else if(name=="db_right_x")output=std::to_string(width/2+104);
    else if(name=="ota_update_progress_x")output=std::to_string(width/2-214);
    else if(name=="pattern_x")output=std::to_string(width/2-324);
    else if(name=="screen_w" || name=="screen_width")output=std::to_string(width);
    else if(name=="screen_h" || name=="screen_height" || name=="screen_original_h")output=std::to_string(height);
    else if(name=="center_x")output=std::to_string(width/2);
    else if(name=="center_y")output=std::to_string(height/2);
    else return false;
    return true;
}
bool ure_gui_keep_variable(const std::string& name) {
    if(name.compare(0,4,"ure_")!=0)return false;
    std::string existing;
    return DataManager::GetValue(name,existing)==0;
}
// URE management callbacks: explicit owned session, no GUI access on workers.
namespace {
int ManagementSession::run(const std::string& command) {
    try {
        const auto locale=value("tw_language").empty() ? std::string("en") : value("tw_language");
        if(review_locale!=locale) {
            invalidate_reviews(); review_locale=locale;
            const bool setting=command.rfind("scale-",0)==0 || command.rfind("mirror-",0)==0 || command=="manage-field-save";
            const bool mutation=command=="save" || command=="apply" || command.ends_with("-execute") || command.ends_with("-apply") ||
                command.ends_with("-apply-image") || command.ends_with("-capture") || command.ends_with("-stage") ||
                command.ends_with("-resume") || command.ends_with("-rollback") || command.ends_with("-cancel") ||
                command.ends_with("-restore") || command.ends_with("-finish");
            ure::require(setting || !mutation,"review-language-changed","Language changed. Review the current plan or journal again before confirming.");
        }
        ure::Root system("/");
        if(command=="mirror-enable" || command=="mirror-disable") {
            gr_external_enable(command=="mirror-enable");
            set("ure_mirror_status","Display change queued for the render thread");
        } else if(command=="mirror-apply") {
            int width=0,height=0,rate=0;
            ure::require(uke_display::parse_selection(value("ure_mirror_resolution"),value("ure_mirror_refresh"),width,height,rate),
                "invalid-display-mode","Select a valid resolution and refresh rate");
            const int percent=ure::display_scale_parse(value("ure_mirror_scale_choice"));
            ure::require(gr_external_configure(width,height,rate,percent),"invalid-display-mode","Output selection is outside the supported range");
            set("ure_mirror_scale_requested",percent);
            set("ure_mirror_status","Output settings queued; unsupported modes preserve the active resolution");
        } else if(command=="mirror-modes") {
            set("ure_mirror_modes",gr_external_modes());
        } else if(command=="scale-reset" || command=="scale-load") {
            const int percent=command=="scale-reset" ? 75 : ure::display_settings_load(value("ure_scale_directory"))["scale_percent"].asInt();
            set("ure_scale_choice",percent);
            set("ure_scale_status","Selection ready. Apply to change the interface size.");
        } else if(command=="scale-apply") {
            const int percent=ure::display_scale_parse(value("ure_scale_choice"));
            set("ure_ui_scale_percent",percent);
            set("ure_scale_status","Applied to text, icons and touch targets.");
            PageManager::RequestUreReload();
        } else if(command=="scale-save") {
            const auto saved=ure::display_settings_save(value("ure_scale_directory"),ure::display_scale_parse(value("ure_ui_scale_applied")));
            set("ure_scale_status",saved["volatile_filesystem"]==true ?
                "Saved on volatile storage; this setting will be lost on reboot" : "Saved and read back; load this directory after mounting it on future boots");
        } else if(command=="services-status")publish(ure::recovery_services_status(system));
        else if(command=="capabilities")publish(ure::capabilities(system));
        else if(command=="storage")publish(ure::storage_graph(system));
        else if(command=="diagnose")publish(ure::diagnose(system,"all"));
        else if(command=="stock-choice-changed")clear_stock_review();
        else if(command=="stock-job-plan") {
            clear_stock_review(); ure::Root parent(value("ure_journal_parent")); pending_stock_plan=ure::stock_job_plan(stock_request());
            reviewed_stock_choices=stock_selection(); pending_stock_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-stock-"+pending_stock_plan["operation_id"].asString())).string();
            set("ure_stock_job_hash",pending_stock_plan["plan_sha256"].asString()); set("ure_stock_job_can_execute","1");
            display_message("ure_stock_job_summary",stock_summary(pending_stock_plan,pending_stock_journal)); publish(pending_stock_plan);
        } else if(command=="stock-job-execute") {
            if(ure::json(stock_selection())!=ure::json(reviewed_stock_choices)) { clear_stock_review(); throw ure::Error("stale-plan","Stock model, SKU, images, source, reset or slot choices changed; review a fresh plan"); }
            ure::require(pending_stock_plan.isObject() && !pending_stock_journal.empty() && pending_stock_plan["plan_sha256"].asString()==value("ure_stock_job_hash"),
                "plan-required","Review the complete six-LUN stock job first");
            publish(ure::stock_job_execute(pending_stock_plan,pending_stock_journal,value("ure_stock_job_hash")));
            set("ure_stock_job_journal",pending_stock_journal); clear_stock_review();
        } else if(command=="stock-job-inspect") {
            clear_stock_review(); const auto review=ure::stock_job_recover(value("ure_stock_job_journal"),"inspect"); publish(review);
            reviewed_stock_journal=value("ure_stock_job_journal"); set("ure_stock_job_journal_hash",review["plan_sha256"].asString());
            for(const auto& action:review["recovery_actions"])set("ure_stock_job_can_"+action.asString(),"1");
        } else if(command=="stock-job-resume" || command=="stock-job-rollback" || command=="stock-job-cancel") {
            ure::require(!reviewed_stock_journal.empty() && reviewed_stock_journal==value("ure_stock_job_journal") && !value("ure_stock_job_journal_hash").empty(),
                "confirmation-required","Inspect and review the selected six-LUN journal first");
            publish(ure::stock_job_recover(reviewed_stock_journal,command.substr(10),value("ure_stock_job_journal_hash"))); clear_stock_review();
        } else if(command=="boot-clear-review") {
            boot_clear_review();
        } else if(command=="boot-route-inventory") {
            auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables"); publish(ure::boot_route_inventory(esp,variables));
            set("ure_status","Registered EFI entries and unchanged default; Uke/Aloha device routing is not yet accepted");
        } else if(command=="boot-route-plan") {
            boot_clear_review(); auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables");
            boot_plan=ure::boot_route_plan(esp,variables,boot_request_fields()); boot_reviewed_selection=boot_selection();
            ure::Root parent(value("ure_journal_parent"));
            boot_pending_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-boot-"+boot_plan["operation_id"].asString())).string();
            auto review=boot_plan; review["journal_directory"]=boot_pending_journal; publish(review);
            set("ure_boot_hash",boot_plan["plan_sha256"].asString());
            set("ure_boot_can_stage",boot_plan["fixture_execute_allowed"].asBool() ? "1" : "0");
            ure_locale::Message summary;
            summary.prose("One-time ").data(value("ure_boot_target")).prose(": EFI option ").data(boot_plan["selected"]["number"].asString()).data(" / ")
                .data(boot_plan["selected"]["description"].asString()).prose("\nPreserved default: ").data(boot_plan["fallback"]["number"].asString()).data(" / ")
                .data(boot_plan["fallback"]["description"].asString()).data("\n").prose(boot_plan["risk"].asString()).prose("\nJournal: ").data(boot_pending_journal);
            display_message("ure_boot_summary",summary);
        } else if(command=="boot-route-stage-fixture") {
            ure::require(boot_plan.isObject() && !boot_pending_journal.empty() && boot_plan["plan_sha256"].asString()==value("ure_boot_hash"),
                "review-required","Review the exact one-time request first");
            if(ure::json(boot_selection())!=ure::json(boot_reviewed_selection)) { boot_clear_review(); throw ure::Error("stale-plan","Boot target, profile, ESP or journal choices changed; review a new request"); }
            auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables");
            publish(ure::boot_route_execute(esp,variables,boot_plan,boot_pending_journal,value("ure_boot_hash")));
            set("ure_boot_journal",boot_pending_journal); boot_clear_review();
        } else if(command=="boot-journal-history") {
            publish(ure::boot_route_history(value("ure_boot_journal")));
        } else if(command=="boot-journal-inspect") {
            boot_clear_review(); auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables");
            const auto review=ure::boot_route_action(esp,variables,value("ure_boot_journal"),"inspect"); publish(review);
            boot_reviewed_journal=value("ure_boot_journal"); boot_journal_selection=boot_context_selection();
            set("ure_boot_journal_hash",review["plan_sha256"].asString());
            for(const auto& action:review["recovery_actions"])set("ure_boot_can_"+action.asString(),"1");
        } else if(command=="boot-journal-recover" || command=="boot-journal-cancel" || command=="boot-journal-fallback-fixture") {
            ure::require(!boot_reviewed_journal.empty() && !value("ure_boot_journal_hash").empty() &&
                ure::json(boot_context_selection())==ure::json(boot_journal_selection),"review-required","Inspect the selected journal and its exact ESP/variable store first");
            auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables");
            publish(ure::boot_route_action(esp,variables,boot_reviewed_journal,command.substr(13),value("ure_boot_journal_hash"))); boot_clear_review();
        } else if(command=="db-clear") {
            dualboot_clear();
        } else if(command.rfind("db-fs-",0)==0) {
            const auto fs=command.substr(6); ure::require(fs=="ext4" || fs=="btrfs" || fs=="f2fs","invalid-layout-filesystem","Choose ext4, Btrfs or F2FS");
            dualboot_clear(); set("ure_db_linux_fs",fs);
        } else if(command.rfind("db-unit-",0)==0) {
            const auto unit=command.substr(8),role=value("ure_db_edit_role");
            ure::require((unit=="MB" || unit=="MiB" || unit=="GB" || unit=="GiB" || unit=="%") &&
                (role=="esp" || role=="linux_boot" || role=="linux" || role=="windows"),"invalid-layout-unit","Choose a supported partition and size unit");
            dualboot_clear(); set("ure_db_"+role+"_unit",unit);
        } else if(command=="db-discover") {
            dualboot_clear();
#ifndef __ANDROID__
            throw ure::Error("fixture-only-command","Host GUI tests must explicitly select a disposable disk image");
#else
            const auto graph=ure::storage_graph(system); std::string selected; unsigned count=0;
            for(const auto& object:graph["objects"])if(object["partition"]==true && object["label"]=="userdata") {
                ++count; selected="sysfs:"+object["parent_lun_sysfs"].asString();
            }
            ure::require(count==1 && selected!="sysfs:","ambiguous-userdata","Exactly one measured userdata parent is required");
            set("ure_gpt_kind","live"); set("ure_gpt_source",selected);
            set("ure_status","Tablet userdata selected for read-only planning; application needs all device checks");
#endif
        } else if(command=="db-choice-changed") {
            dualboot_clear();
            for(const auto* name:{"linux","windows","esp","boot"}) {
                const auto key="ure_db_"+std::string(name);
                ure::require(value(key)=="0" || value(key)=="1","invalid-choice","Select a valid dualboot checkbox value");
            }
            if(choice("ure_db_windows"))set("ure_db_esp","1");
            if(!choice("ure_db_linux"))set("ure_db_boot","0");
        } else if(command.rfind("db-toggle-",0)==0) {
            const auto field=command.substr(10);
            ure::require(field=="linux" || field=="windows" || field=="esp" || field=="boot","invalid-choice","Unknown dualboot choice");
            dualboot_clear(); const auto key="ure_db_"+field;
            if(field=="esp")ure::require(!choice("ure_db_windows"),"windows-requires-esp","Disable Windows before making ESP optional");
            if(field=="boot")ure::require(choice("ure_db_linux"),"linux-boot-without-linux","Select Linux before enabling separate boot");
            set(key,choice(key) ? "0" : "1");
            if(field=="windows" && choice(key))set("ure_db_esp","1");
            if(field=="linux" && !choice(key))set("ure_db_boot","0");
            for(const auto* name:{"linux","windows","esp","boot"})set("ure_db_"+std::string(name)+"_label",choice("ure_db_"+std::string(name)) ? "Selected" : "Not selected");
        } else if(command.rfind("db-edit-",0)==0) {
            const auto field=command.substr(8);
            ure::require(field=="esp_size" || field=="linux_boot_size" || field=="linux_size" || field=="windows_size" ||
                field=="image" || field=="journal","invalid-field","Unknown dualboot editor field");
            dualboot_clear();
            edited_management_field=field=="image" ? "ure_gpt_source" : field=="journal" ? "ure_journal_parent" : "ure_db_"+field;
            if(field=="image")set("ure_gpt_kind","image");
            set("ure_form_field",edited_management_field); set("ure_form_value",value(edited_management_field)); set("ure_form_back","ure_dualboot_sizes");
        } else if(command=="db-preview") {
            dualboot_clear(); const auto choices=dualboot_choices(); auto target=gpt_target(system);
            auto plan=ure::dualboot_plan(system,target,choices["request"],"global-os3.0.303.0");
            const auto& layout=plan["gpt"]["layout"]; ure::Value graph;
            graph["format"]=layout["format"]; graph["pool"]["bytes"]=layout["pool"]["bytes"]; graph["rows"]=ure::Value(Json::arrayValue);
            for(const auto& row:layout["rows"]) { ure::Value part; for(const auto* key:{"role","pool_offset","bytes"})part[key]=row[key]; graph["rows"].append(part); }
            auto summary=ure::dualboot_preview(plan);
            bool allowed=target.identity["kind"]=="regular-image";
            if(!allowed) {
                const auto checks=ure::dualboot_device_preflight(system,target,plan); allowed=checks["eligible"]==true;
                summary+="\nDevice checks:\n";
                for(const auto& check:checks["checks"])if(check["passed"]!=true)
                    summary+="- "+check["check"].asString()+": "+check.get("reason","Unavailable").asString()+"\n";
            }
            if(!value("ure_journal_parent").empty()) {
                ure::Root parent(value("ure_journal_parent"));
                dualboot_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-dualboot-"+plan["operation_id"].asString())).string();
            } else { allowed=false; summary+="\nSelect a backup/journal directory before applying.\n"; }
            dualboot_plan=plan; dualboot_selection=choices;
            set("ure_db_hash",plan["plan_sha256"].asString()); set("ure_db_can_apply",allowed ? "1" : "0");
            set("ure_db_summary",summary); set("ure_layout_graph",ure::json(graph)); publish(plan);
            set("ure_status",allowed ? "Review all sizes and data loss; no change has been applied" : "Preview ready; application is blocked by the listed prerequisites");
        } else if(command=="db-apply") {
            ure::require(dualboot_plan.isObject() && !dualboot_journal.empty() && value("ure_db_can_apply")=="1" &&
                ure::json(dualboot_selection)==ure::json(dualboot_choices()) && value("ure_db_hash")==dualboot_plan["plan_sha256"].asString(),
                "review-required","Recalculate and review the unchanged target, sizes and journal first");
            ure::require(choice("ure_db_erase_ack") && value("ure_db_confirmation")=="ERASE USERDATA","confirmation-required","Acknowledge data loss and type ERASE USERDATA");
            auto target=gpt_target(system);
            const auto result=target.identity["kind"]=="regular-image" ?
                [&]{ auto writer=gpt_target(system,true); return ure::dualboot_image_execute(system,writer,dualboot_plan,dualboot_journal,value("ure_db_hash"),"ERASE USERDATA"); }() :
                ure::dualboot_device_execute(system,dualboot_plan,dualboot_journal,value("ure_db_hash"),"ERASE USERDATA");
            set("ure_partition_journal",dualboot_journal); publish(result); dualboot_clear();
            set("ure_status","Partition operation finished; inspect its journal before further installation");
        } else if(command.rfind("manage-edit-",0)==0) {
            const auto field=command.substr(12);
            static const std::set<std::string> allowed{"ure_esp","ure_journal_parent","ure_fs_size","ure_fs_label","ure_rescue_command","ure_rescue_kernel","ure_rescue_timeout",
                "ure_manage_journal","ure_btrfs_root","ure_btrfs_path","ure_btrfs_source","ure_btrfs_backup","ure_btrfs_saved","ure_btrfs_size","ure_btrfs_device",
                "ure_btrfs_usage","ure_btrfs_limit","ure_btrfs_store","ure_btrfs_parent","ure_btrfs_name","ure_btrfs_incremental_parent",
                "ure_boot_esp","ure_boot_variables","ure_boot_option","ure_boot_fallback","ure_boot_partuuid","ure_boot_journal"};
            ure::require(allowed.count(field),"invalid-field","Select a supported management field");
            edited_management_field=field;
            set("ure_form_field",field); set("ure_form_value",value(field));
            set("ure_form_back",field.rfind("ure_boot_",0)==0 ? "ure_boot_manager" : field.rfind("ure_btrfs_",0)==0 ? "ure_btrfs" : field.rfind("ure_rescue_",0)==0 || field=="ure_esp" ? "ure_linux" : "ure_filesystems");
        } else if(command=="manage-field-save") {
            const auto field=value("ure_form_field");
            ure::require(!edited_management_field.empty() && field==edited_management_field && value("ure_form_value").size()<=4096,"invalid-field","Invalid management field selection");
            // The editable field is selected exclusively by manage-edit-* above.
            set(field,value("ure_form_value")); set("ure_manage_hash",""); set("ure_manage_can_apply","0");
            dualboot_clear();
            boot_clear_review();
            edited_management_field.clear();
        } else if(command=="linux-audit") {
            auto root=os_root(); auto esp=selected_esp(); const auto report=ure::linux_boot_audit(root,esp.get()); publish(report);
            set("ure_status",std::to_string(report["error_count"].asUInt())+" errors, "+std::to_string(report["warning_count"].asUInt())+" warnings; metadata inspection only");
        } else if(command=="rescue-plan") {
            auto root=os_root(); auto esp=selected_esp(); const auto request=rescue_request();
            review_management("rescue",ure::linux_rescue_plan(root,request,esp.get()),management_selection("rescue",request));
        } else if(command=="rescue-execute") {
            const auto request=rescue_request(); reviewed_management("rescue",management_selection("rescue",request)); auto root=os_root(); auto esp=selected_esp();
            set("ure_manage_journal",managed_journal);
            if(bind_backend_control)bind_backend_control("rescue",root,managed_plan,managed_journal);
            const auto result=ure::linux_rescue_execute(root,managed_plan,managed_journal,value("ure_manage_hash"),esp.get()); publish(result);
            if(backend_control_result)backend_control_result(result);
            set("ure_status",result["state"].asString()+"; private console is available in the session journal");
            set("ure_manage_hash",""); set("ure_manage_can_apply","0"); if(result["successful"]!=true)return 1;
        } else if(command=="rescue-inspect") {
            auto store=ure::private_directory(value("ure_manage_journal"),false); ure::Value result;
            result["plan"]=ure::parse_json(store.read("plan.json")); result["state"]=ure::parse_json(store.read("state.json"));
            if(store.exists("console.log")) { auto file=store.open("console.log",O_RDONLY); const auto bytes=ure::storage_bytes(file.get());
                result["console_preview"]=text_prefix(ure::storage_read(file.get(),0,static_cast<std::size_t>(std::min<std::uint64_t>(bytes,65540))),65536); result["console_preview_truncated"]=bytes>65536; }
            publish(result);
        } else if(command=="filesystem-capabilities")publish(ure::filesystem_capabilities());
        else if(command=="partition-capabilities")publish(ure::partition_capabilities());
        else if(command=="filesystem-inspect" || command=="filesystem-check" || command=="storage-preflight") {
            auto target=backup_target(system);
            publish(command=="filesystem-inspect" ? ure::filesystem_probe(target.descriptor.get()) : command=="filesystem-check" ? ure::filesystem_check(target.descriptor.get()) : ure::storage_preflight(system,target,"global-os3.0.303.0"));
        } else if(command=="filesystem-plan") {
            auto target=backup_target(system); const auto request=filesystem_request(target.identity["bytes"].asUInt64());
            review_management("filesystem",ure::filesystem_operation_plan(system,target,request,"global-os3.0.303.0"),management_selection("filesystem",request));
        } else if(command=="filesystem-execute") {
            auto target=backup_target(system,true); const auto request=filesystem_request(target.identity["bytes"].asUInt64()); reviewed_management("filesystem",management_selection("filesystem",request));
            set("ure_manage_journal",managed_journal); publish(ure::filesystem_operation_execute(system,target,managed_plan,managed_journal,value("ure_manage_hash")));
            set("ure_status","Filesystem applied and independently checked; complete original bytes remain in the journal");
            set("ure_manage_hash",""); set("ure_manage_can_apply","0");
        } else if(command=="filesystem-journal-inspect") {
            auto target=backup_target(system); const auto report=ure::filesystem_operation_recover(system,target,value("ure_manage_journal"),"inspect",""); publish(report);
            reviewed_filesystem_journal=value("ure_manage_journal"); set("ure_fs_journal_hash",report["plan_sha256"].asString());
            managed_selection=management_selection("filesystem");
            for(const auto* action:{"resume","rollback","cancel"})set("ure_fs_can_"+std::string(action),"0");
            for(const auto& action:report["application"]["recovery_actions"])set("ure_fs_can_"+action.asString(),"1");
            if(report["original_unchanged_verified"]==true)set("ure_fs_can_cancel","1");
        } else if(command=="filesystem-resume" || command=="filesystem-rollback" || command=="filesystem-cancel") {
            ure::require(reviewed_filesystem_journal==value("ure_manage_journal") && !value("ure_fs_journal_hash").empty() &&
                ure::json(managed_selection)==ure::json(management_selection("filesystem")),"review-required","Inspect this filesystem journal and current target first");
            const auto action=command.substr(11); auto target=backup_target(system,action!="cancel");
            publish(ure::filesystem_operation_recover(system,target,reviewed_filesystem_journal,action,value("ure_fs_journal_hash")));
            set("ure_fs_journal_hash",""); reviewed_filesystem_journal.clear();
        } else if(command.rfind("btrfs-",0)==0) {
            if(command=="btrfs-send-verify")publish(ure::btrfs_send_verify(value("ure_btrfs_store")));
            else if(command=="btrfs-backup-inspect")publish(ure::btrfs_backup_inspect(value("ure_btrfs_store")));
            else if((command=="btrfs-plan" || command=="btrfs-execute") &&
                (value("ure_btrfs_action")=="scrub-cancel" || value("ure_btrfs_action")=="balance-pause" || value("ure_btrfs_action")=="balance-cancel")) {
                throw ure::Error("owner-control-required","Use the exact captured maintenance controller; do not derive its target from mutable selections");
            }
            else {
                auto root=os_root("ure_btrfs_root");
                if(command=="btrfs-info" || command=="btrfs-subvolumes" || command=="btrfs-usage" || command=="btrfs-device-stats" || command=="btrfs-scrub-status" || command=="btrfs-balance-status")publish(ure::btrfs_native_info(root,command.substr(6)));
                else if(command=="btrfs-plan") { const auto request=btrfs_request(root); review_management("btrfs",ure::btrfs_manage_plan(root,request,"global-os3.0.303.0"),management_selection("btrfs",request)); }
                else if(command=="btrfs-execute") {
                    reviewed_management("btrfs",management_selection("btrfs",btrfs_request(root))); set("ure_manage_journal",managed_journal);
                    const auto action=managed_plan["request"]["action"].asString();
                    if(bind_backend_control && (action=="scrub" || action=="balance"))bind_backend_control(action,root,managed_plan,managed_journal);
                    const auto result=ure::btrfs_manage_execute(root,managed_plan,managed_journal,value("ure_manage_hash")); publish(result);
                    if(backend_control_result && (action=="scrub" || action=="balance"))backend_control_result(result);
                    set("ure_manage_hash",""); set("ure_manage_can_apply","0");
                } else if(command=="btrfs-snapshot-plan" || command=="btrfs-send-plan") {
                    const auto snapshot=command=="btrfs-snapshot-plan";
                    const auto plan=snapshot ? ure::btrfs_snapshot_plan(root,value("ure_btrfs_source"),value("ure_btrfs_parent"),value("ure_btrfs_name"),"global-os3.0.303.0",value("ure_btrfs_store")) :
                        ure::btrfs_send_plan(root,value("ure_btrfs_source"),value("ure_btrfs_incremental_parent"),"global-os3.0.303.0",value("ure_btrfs_store"));
                    review_management(snapshot ? "btrfs-snapshot" : "btrfs-send",plan,management_selection("btrfs-backup"));
                } else if(command=="btrfs-backup-review") {
                    const auto plan=ure::btrfs_backup_inspect(value("ure_btrfs_store")); review_management(plan["operation"]=="snapshot" ? "btrfs-snapshot" : "btrfs-send",plan,management_selection("btrfs-backup"));
                } else if(command=="btrfs-backup-execute") {
                    ure::require(managed_kind=="btrfs-snapshot" || managed_kind=="btrfs-send","review-required","Review a snapshot or send backup first"); reviewed_management(managed_kind,management_selection("btrfs-backup"));
                    publish(managed_kind=="btrfs-snapshot" ? ure::btrfs_snapshot_execute(root,value("ure_btrfs_store"),value("ure_manage_hash")) : ure::btrfs_send_capture(root,value("ure_btrfs_store"),value("ure_manage_hash")));
                    set("ure_manage_hash",""); set("ure_manage_can_apply","0");
                } else throw ure::Error("unknown-action","Unknown native Btrfs action");
            }
        }
        else if(command=="report") {
            ure::Value report=ure::public_report(system);
            const auto destination="/tmp/ure-report-"+ure::operation_id()+".json";
            ure::save_json(destination,ure::envelope(report));
            ure::Value result; result["report_path"]=destination; result["public_scrubbed"]=true; publish(result);
        } else if(command=="linux" || command=="windows" || command=="files" || command=="boot") {
            const auto path=value("ure_root");
            ure::require(!path.empty() && path.front()=='/' && path!="/","root-required","Select an already mounted OS root");
            ure::Root root(path);
            if(command=="linux")publish(ure::linux_detect(root));
            if(command=="windows")publish(ure::windows_detect(root));
            if(command=="files")publish(ure::files_list(root,"."));
            if(command=="boot")publish(ure::boot_targets(root,nullptr));
        } else if(command=="journals" || command=="journal-inspect" || command=="journal-resume" || command=="journal-cancel" || command=="journal-rollback") {
            const auto path=value("ure_root");
            ure::require(!path.empty() && path.front()=='/' && path!="/","root-required","Select an already mounted OS root");
            ure::Root root(path);
            if(command=="journals")publish(ure::transaction_list(root,value("ure_journal_parent")));
            else if(command=="journal-inspect") {
                set("ure_journal_hash","");
                for(const auto* action:{"resume","cancel","rollback"})set(std::string("ure_can_")+action,"0");
                const auto inspected=ure::transaction_inspect(root,value("ure_journal")); publish(inspected);
                set("ure_journal_hash",inspected["plan_sha256"].asString());
                for(const auto& action:inspected["recovery_actions"])if(action=="resume" || action=="cancel" || action=="rollback")
                    set("ure_can_"+action.asString(),"1");
                set("ure_status","Review the current file and verified backup before choosing an action");
            } else {
                const auto confirmation=value("ure_journal_hash"); ure::require(!confirmation.empty(),"confirmation-required","Inspect and review this journal first");
                if(command=="journal-resume")publish(ure::transaction_resume(root,value("ure_journal"),confirmation));
                if(command=="journal-cancel")publish(ure::transaction_cancel(root,value("ure_journal"),confirmation));
                if(command=="journal-rollback")publish(ure::transaction_rollback(root,value("ure_journal"),confirmation));
                set("ure_journal_hash","");
                for(const auto* action:{"resume","cancel","rollback"})set(std::string("ure_can_")+action,"0");
            }
        } else if(command.rfind("stream-",0)==0) {
            if(command=="stream-plan" || command=="stream-inspect")clear_stream_review();
            if(command=="stream-plan") {
                auto target=backup_target(system); ure::Root parent(value("ure_journal_parent"));
                const auto plan=ure::restore_stream_plan(system,target,ure::json_file(value("ure_stream_manifest")),"global-os3.0.303.0");
                const auto file="/tmp/ure-stream-plan-"+plan["operation_id"].asString()+".json";
                const auto before="/tmp/ure-stream-before-"+plan["operation_id"].asString()+".json";
                ure::save_json(file,plan); ure::save_json(before,ure::restore_stream_backup_plan(plan));
                auto review=plan;
                for(const auto* name:{"before","after"}) { review[name]["chunk_count"]=review[name]["chunks"].size(); review[name].removeMember("chunks"); }
                review["plan_file"]=file; review["before_manifest_file"]=before;
                review["journal_directory"]=(ure::fs::path(value("ure_journal_parent"))/("ure-stream-"+plan["operation_id"].asString())).string();
                review["host_required"]=true; publish(review);
                set("ure_status","Review this plan on the host; verify both complete host backups before starting transfer");
            } else if(command=="stream-inspect") {
                auto target=backup_target(system); const auto review=ure::restore_stream_status(system,target,value("ure_stream_journal")); publish(review);
                reviewed_stream_journal=value("ure_stream_journal"); set("ure_stream_journal_hash",review["plan_sha256"].asString());
                for(const auto& action:review["recovery_actions"]) {
                    if(action=="host-rollback")set("ure_stream_can_rollback","1");
                    if(action=="finish" || action=="cancel")set("ure_stream_can_"+action.asString(),"1");
                }
                set("ure_status","Review current bytes; reconnect the host for remaining restore or rollback chunks");
            } else if(command=="stream-rollback" || command=="stream-finish" || command=="stream-cancel") {
                ure::require(!reviewed_stream_journal.empty() && reviewed_stream_journal==value("ure_stream_journal") && !value("ure_stream_journal_hash").empty(),
                    "confirmation-required","Inspect and review this host-assisted journal first");
                auto target=backup_target(system,command=="stream-rollback"); const auto confirmation=value("ure_stream_journal_hash");
                if(command=="stream-rollback")publish(ure::restore_stream_rollback(system,target,reviewed_stream_journal,confirmation));
                else if(command=="stream-finish")publish(ure::restore_stream_finish(system,target,reviewed_stream_journal,confirmation));
                else publish(ure::restore_stream_cancel(system,target,reviewed_stream_journal,confirmation));
                clear_stream_review();
                set("ure_status",command=="stream-rollback" ? "Rollback direction recorded; reconnect the host to transfer original chunks" : "Journal action completed and verified");
            } else throw ure::Error("unknown-action","Unknown host-assisted restore action");
        } else if(command.rfind("restore-",0)==0) {
            if(command=="restore-plan" || command=="restore-inspect")clear_restore_review();
            if(command=="restore-plan") {
                auto target=backup_target(system); ure::Root parent(value("ure_journal_parent"));
                pending_restore_backup=value("ure_backup_dir");
                pending_restore_plan=ure::restore_plan(system,target,pending_restore_backup,"global-os3.0.303.0");
                pending_restore_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-restore-"+pending_restore_plan["operation_id"].asString())).string();
                const auto file="/tmp/ure-restore-plan-"+pending_restore_plan["operation_id"].asString()+".json";
                ure::save_json(file,pending_restore_plan); auto review=pending_restore_plan;
                review["before"]["chunk_count"]=review["before"]["chunks"].size(); review["before"].removeMember("chunks");
                review["plan_file"]=file; review["journal_directory"]=pending_restore_journal; publish(review);
                set("ure_restore_plan_hash",pending_restore_plan["plan_sha256"].asString());
                set("ure_restore_can_execute",target.identity["kind"]=="regular-image" ? "1" : "0");
                set("ure_status","Review complete-object overwrite, required free space and rollback destination");
            } else if(command=="restore-execute") {
                ure::require(pending_restore_plan.isObject() && value("ure_restore_plan_hash")==pending_restore_plan["plan_sha256"].asString(),"confirmation-required","Create and review this restore plan first");
                ure::require(value("ure_backup_dir")==pending_restore_backup && ure::fs::path(pending_restore_journal).parent_path()==ure::fs::path(value("ure_journal_parent")),"stale-plan","Backup or journal destination changed; review a new plan");
                auto target=backup_target(system,true);
                set("ure_restore_journal",pending_restore_journal);
                publish(ure::restore_execute(system,target,pending_restore_plan,pending_restore_journal,value("ure_restore_plan_hash")));
                set("ure_restore_journal",pending_restore_journal); clear_restore_review();
            } else if(command=="restore-inspect") {
                auto target=backup_target(system); const auto review=ure::restore_inspect(system,target,value("ure_restore_journal")); publish(review);
                reviewed_restore_journal=value("ure_restore_journal"); set("ure_restore_journal_hash",review["plan_sha256"].asString());
                for(const auto& action:review["recovery_actions"])set("ure_restore_can_"+action.asString(),"1");
                set("ure_status","Review verified current bytes and available recovery actions");
            } else if(command=="restore-resume" || command=="restore-rollback" || command=="restore-cancel") {
                ure::require(!reviewed_restore_journal.empty() && reviewed_restore_journal==value("ure_restore_journal") && !value("ure_restore_journal_hash").empty(),"confirmation-required","Inspect and review the selected restore journal first");
                auto target=backup_target(system,command!="restore-cancel"); const auto confirmation=value("ure_restore_journal_hash");
                if(command=="restore-resume")publish(ure::restore_resume(system,target,reviewed_restore_journal,confirmation));
                else if(command=="restore-rollback")publish(ure::restore_rollback(system,target,reviewed_restore_journal,confirmation));
                else publish(ure::restore_cancel(system,target,reviewed_restore_journal,confirmation));
                clear_restore_review();
            } else throw ure::Error("unknown-action","Unknown raw restore action");
        } else if(command.rfind("tree-",0)==0) {
            const auto store=value("ure_tree_store");
            if(command=="tree-verify")publish(ure::backup_tree_verify(store));
            else if(command=="tree-plan" || command=="tree-review") {
                pending_tree=ure::Value(); set("ure_tree_hash","");
                if(command=="tree-plan") {
                    const auto path=value("ure_tree_root"); ure::require(ure::fs::path(path).is_absolute() && path!="/","root-required","Select an already mounted Linux or home directory");
                    ure::Root source(path); pending_tree=ure::backup_tree_plan(source,".","global-os3.0.303.0",store);
                } else pending_tree=ure::backup_tree_inspect(store);
                reviewed_tree_store=store; reviewed_tree_root=value("ure_tree_root"); reviewed_tree_destination=value("ure_tree_destination");
                set("ure_tree_hash",pending_tree["plan_sha256"].asString());
                auto review=pending_tree; review["selected_root"]=reviewed_tree_root; review["backup_store"]=store; review["restore_destination"]=reviewed_tree_destination;
                publish(review); set("ure_status","Review source, metadata, backup store and new restore destination; no mount or unlock was performed");
            } else if(command=="tree-capture" || command=="tree-restore") {
                ure::require(pending_tree.isObject() && pending_tree["plan_sha256"].asString()==value("ure_tree_hash") && reviewed_tree_store==store,
                    "review-required","Review this tree plan before capture or restore");
                if(command=="tree-capture") {
                    ure::require(value("ure_tree_root")==reviewed_tree_root,"stale-plan","Source selection changed; review the tree plan again");
                    ure::Root source(reviewed_tree_root); publish(ure::backup_tree_capture(source,store,value("ure_tree_hash")));
                } else {
                    ure::require(value("ure_tree_destination")==reviewed_tree_destination,"stale-plan","Restore destination changed; review again");
                    publish(ure::backup_tree_restore(store,reviewed_tree_destination,value("ure_tree_hash")));
                }
                pending_tree=ure::Value(); set("ure_tree_hash","");
            } else throw ure::Error("unknown-command","Unknown directory backup action");
        } else if(command.rfind("raw-",0)==0) {
            if(command=="raw-image" || command=="raw-live") {
                pending_backup=ure::Value(); pending_backup_directory.clear(); set("ure_backup_hash","");
                set("ure_raw_kind",command=="raw-image" ? "image" : "live"); set("ure_raw_source","");
            } else if(command=="raw-usage") {
                ure::require(value("ure_raw_kind")=="live","live-source-required","Usage observations apply to a selected live Storage Graph identity");
                publish(ure::storage_usage(system,value("ure_raw_source")));
            } else if(command=="raw-plan") {
                pending_backup=ure::Value(); set("ure_backup_hash","");
                auto target=backup_target(system); pending_backup_directory=value("ure_backup_dir");
                set("ure_raw_source",target.identity[value("ure_raw_kind")=="live" ? "stable_id" : "path"].asString());
                pending_backup=ure::backup_storage_plan(system,target,"global-os3.0.303.0",64*1024*1024); review_backup();
            } else if(command=="raw-capture") {
                ure::require(pending_backup.isObject() && pending_backup["plan_sha256"].asString()==value("ure_backup_hash"),"plan-required","Review a storage backup plan first");
                validate_backup_selection(pending_backup);
                ure::require(value("ure_backup_dir")==pending_backup_directory,"stale-plan","Destination changed; review a new backup plan");
                publish(ure::backup_capture(system,pending_backup,pending_backup_directory,false));
                pending_backup=ure::Value(); set("ure_backup_hash","");
            } else if(command=="raw-resume") {
                const auto directory=value("ure_backup_dir"); const auto plan=ure::json_file(ure::fs::path(directory)/"plan.json");
                if(value("ure_raw_kind")=="live") {
                    const auto selected=backup_target(system); set("ure_raw_source",selected.identity["stable_id"].asString());
                }
                validate_backup_selection(plan); publish(ure::backup_capture(system,plan,directory,true));
            } else throw ure::Error("unknown-command","Unknown storage backup action");
        } else if(command=="backup-plan" || command=="backup-capture" || command=="backup-resume" || command=="backup-verify") {
            if(command=="backup-verify")publish(ure::backup_verify(value("ure_backup_dir")));
            else {
                const auto path=value("ure_root");
                ure::require(!path.empty() && path.front()=='/' && path!="/","root-required","Select an already mounted source root");
                ure::Root root(path);
                if(command=="backup-plan") {
                    pending_backup=ure::Value(); set("ure_backup_hash","");
                    pending_backup_directory=value("ure_backup_dir");
                    pending_backup=ure::backup_plan(root,value("ure_file"),"global-os3.0.303.0");
                    review_backup();
                } else if(command=="backup-capture") {
                    ure::require(pending_backup.isObject() && pending_backup["plan_sha256"].asString()==value("ure_backup_hash"),"plan-required","Review a backup plan first");
                    ure::require(value("ure_backup_dir")==pending_backup_directory,"stale-plan","Backup destination changed; review a new plan");
                    publish(ure::backup_capture(root,pending_backup,pending_backup_directory,false));
                    pending_backup=ure::Value(); set("ure_backup_hash","");
                } else publish(ure::backup_capture(root,ure::json_file(ure::fs::path(value("ure_backup_dir"))/"plan.json"),value("ure_backup_dir"),true));
            }
        } else if(command=="partition-job-inspect") {
            clear_gpt_review(); auto target=gpt_target(system); const auto report=ure::partition_job_recover(system,target,value("ure_partition_journal"),"inspect"); publish(report);
            reviewed_partition_journal=value("ure_partition_journal"); reviewed_partition_selection=partition_selection();
            set("ure_partition_journal_hash",report["plan_sha256"].asString());
            for(const auto& action:report["recovery_actions"])set("ure_partition_can_"+action.asString(),"1");
            set("ure_status","Journal readback: "+report["classification"].asString()+"; choose only an available recovery action");
        } else if(command=="partition-job-resume" || command=="partition-job-rollback" || command=="partition-job-cancel") {
            ure::require(!reviewed_partition_journal.empty() && reviewed_partition_journal==value("ure_partition_journal") && !value("ure_partition_journal_hash").empty() &&
                ure::json(reviewed_partition_selection)==ure::json(partition_selection()),"review-required","Inspect this combined partition journal and the unchanged target first");
            const auto action=command.substr(14); auto target=gpt_target(system,action!="cancel");
            publish(ure::partition_job_recover(system,target,reviewed_partition_journal,action,value("ure_partition_journal_hash"))); clear_gpt_review();
        } else if(command.rfind("layout-",0)==0 && command!="layout-preview" && command!="layout-plan" && command!="layout-apply-image") {
            clear_gpt_review(); set("ure_layout_graph",""); set("ure_layout_review","Selections changed; calculate and review the layout again");
            if(command=="layout-mode-standard") {
                set("ure_layout_mode","standard"); set("ure_layout_placement","after_userdata");
                set("ure_layout_userdata_policy","preserve"); set("ure_layout_record_edits","[]");
                set("ure_layout_userdata_guid","");
            } else if(command=="layout-mode-advanced")set("ure_layout_mode","advanced");
            else if(command.rfind("layout-edit-",0)==0) {
                const auto role=command.substr(12); ure::require(role=="esp" || role=="linux" || role=="windows" || role=="userdata","invalid-layout-role","Select a supported layout role");
                set("ure_layout_edit_role",role);
                for(const auto* field:{"size","unit","filesystem","guid"})set("ure_layout_edit_"+std::string(field),value("ure_layout_"+role+"_"+field));
            } else if(command=="layout-row-save") {
                const auto role=value("ure_layout_edit_role"); ure::require(role=="esp" || role=="linux" || role=="windows" || role=="userdata","invalid-layout-role","Select a supported layout role");
                for(const auto* field:{"size","unit","filesystem","guid"})set("ure_layout_"+role+"_"+field,value("ure_layout_edit_"+std::string(field)));
            } else if(command=="layout-placement-after")set("ure_layout_placement","after_userdata");
            else if(command=="layout-placement-before" || command=="layout-userdata-recreate") {
                ure::require(value("ure_layout_mode")=="advanced","advanced-mode-required","Select advanced mode to erase and recreate userdata");
                set("ure_layout_userdata_policy","recreate");
                if(command=="layout-placement-before")set("ure_layout_placement","before_userdata");
            } else if(command=="layout-userdata-preserve") {
                set("ure_layout_userdata_policy","preserve"); set("ure_layout_placement","after_userdata");
            } else if(command=="layout-record-clear")set("ure_layout_record_edits","[]");
            else if(command=="layout-record-add") {
                ure::require(value("ure_layout_mode")=="advanced","advanced-mode-required","Select advanced mode before requesting existing record changes");
                const auto text=value("ure_layout_record_index"); unsigned index=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),index);
                ure::require(parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && index>0 && index<=4096,"invalid-record-edit","Enter a GPT index from the inspected partition map");
                ure::Value row; row["index"]=index; row["contents"]=value("ure_layout_record_contents");
                const auto guid=value("ure_layout_record_guid"); if(!guid.empty())row["partuuid"]=guid;
                if(row["contents"]=="format")row["filesystem"]=value("ure_layout_record_filesystem");
                const auto text_edits=value("ure_layout_record_edits"); ure::require(text_edits.size()<=65536,"size-limit","Advanced edits exceed their limit");
                const auto existing=ure::parse_json(text_edits); ure::require(existing.isArray() && existing.size()<4096,"invalid-record-edit","Invalid advanced edit list");
                ure::Value edits(Json::arrayValue); for(const auto& edit:existing)if(edit["index"].asUInt()!=index)edits.append(edit); edits.append(row);
                const auto serialized=ure::json(edits); ure::require(serialized.size()<=65536,"size-limit","Advanced edits exceed their limit"); set("ure_layout_record_edits",serialized);
            } else throw ure::Error("unknown-action","Unknown layout selection action");
        } else if(command=="layout-preview" || command=="layout-plan" || command=="layout-apply-image") {
            if(command=="layout-apply-image") {
                ure::require(pending_gpt_plan["operation"]=="partition.apply-layout" && !reviewed_layout_request.isNull() &&
                    ure::json(reviewed_layout_request)==ure::json(layout_request()) &&
                    pending_gpt_plan["plan_sha256"].asString()==value("ure_gpt_plan_hash") && !pending_gpt_journal.empty(),
                    "review-required","Review this unchanged complete filesystem and GPT layout before applying");
                ure::require(value("ure_gpt_kind")=="image","live-write-unavailable","Live repartitioning requires the complete backup, format/migration and device trust backend");
                ure::require(ure::fs::path(pending_gpt_journal).parent_path()==ure::fs::path(value("ure_journal_parent")),"stale-plan","Journal destination changed; review again");
                auto target=gpt_target(system,true); publish(ure::partition_job_execute(system,target,pending_gpt_plan,pending_gpt_journal,value("ure_gpt_plan_hash")));
                set("ure_partition_journal",pending_gpt_journal); clear_gpt_review();
                set("ure_status","Image filesystems and GPT verified; original userdata and GPT remain available for complete rollback");
            } else {
                clear_gpt_review(); set("ure_layout_graph",""); auto target=gpt_target(system); const auto request=layout_request();
                ure::Value layout;
                if(command=="layout-plan") {
                    ure::require(target.identity["kind"]=="regular-image","live-write-unavailable","Live repartitioning awaits device firmware, ownership and Android encryption acceptance; use read-only preview");
                    ure::Root parent(value("ure_journal_parent")); pending_gpt_plan=ure::partition_job_plan(system,target,request,"global-os3.0.303.0");
                    layout=pending_gpt_plan["gpt"]["layout"]; reviewed_layout_request=request;
                    pending_gpt_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-layout-"+pending_gpt_plan["operation_id"].asString())).string();
                    set("ure_gpt_plan_hash",pending_gpt_plan["plan_sha256"].asString());
                    set("ure_gpt_can_execute",target.identity["kind"]=="regular-image" ? "1" : "0");
                    ure::save_json("/tmp/ure-layout-plan-"+pending_gpt_plan["operation_id"].asString()+".json",pending_gpt_plan);
                } else layout=ure::partition_layout(target,request,"global-os3.0.303.0",&system);
                // The widget needs only bounded allocation geometry, never unit identities.
                ure::Value graph; graph["format"]=layout["format"]; graph["pool"]=layout["pool"]; graph["rows"]=ure::Value(Json::arrayValue);
                for(const auto& row:layout["rows"]) { ure::Value part; for(const auto* field:{"role","pool_offset","bytes"})part[field]=row[field]; graph["rows"].append(part); }
                if(command=="layout-plan") { layout["warnings"]=pending_gpt_plan["warnings"];
                    set("ure_layout_review",ure::partition_layout_text(layout)+"\nComplete image job: filesystem preparation, userdata writes and GPT.\nRequired journal space: "+
                        std::to_string(pending_gpt_plan["estimated_journal_bytes"].asUInt64()/1048576)+" MiB\nJournal: "+pending_gpt_journal);
                    publish(pending_gpt_plan);
                } else { set("ure_layout_review",ure::partition_layout_text(layout)); publish(layout); }
                set("ure_layout_graph",ure::json(graph)); set("ure_status","Review original userdata bounds, filesystems, data loss, journal space and advanced edits before applying");
            }
        } else if(command.rfind("gpt-",0)==0) {
            if(command=="gpt-image" || command=="gpt-live") {
                clear_gpt_review(); set("ure_gpt_kind",command=="gpt-image" ? "image" : "live");
                set("ure_gpt_source","");
            } else if(command=="gpt-verify")publish(ure::gpt_backup_verify(value("ure_gpt_backup_dir")));
            else if(command=="gpt-execute") {
                ure::require(pending_gpt_plan["operation"]!="gpt.layout" && pending_gpt_plan["operation"]!="partition.apply-layout","layout-review-required","Layout changes use their own explicit scoped review");
                ure::require(pending_gpt_plan.isObject() && !pending_gpt_journal.empty() &&
                    pending_gpt_plan["plan_sha256"].asString()==value("ure_gpt_plan_hash"),"plan-required","Create and review a GPT plan first");
                ure::require(ure::fs::path(pending_gpt_journal).parent_path()==ure::fs::path(value("ure_journal_parent")),
                    "stale-plan","Journal destination changed; review a new GPT plan");
                if(pending_gpt_plan["operation"]=="gpt.stock")ure::require(value("ure_stock_inputs")==pending_gpt_plan["stock_inputs_directory"].asString() &&
                    value("ure_stock_lun")==std::to_string(pending_gpt_plan["stock_lun"].asUInt()) && value("ure_stock_identity_backup")==pending_gpt_plan["backup_directory"].asString(),
                    "stale-plan","Stock inputs, LUN or original identity backup changed; review a new plan");
                auto target=gpt_target(system,true);
                publish(ure::gpt_execute(target,pending_gpt_plan,pending_gpt_journal,value("ure_gpt_plan_hash"),&system));
                set("ure_gpt_journal",pending_gpt_journal);
                clear_gpt_review(); set("ure_status","Image GPT verified; inspect its journal before rollback");
            } else if(command=="gpt-rollback" || command=="gpt-resume") {
                ure::require(!reviewed_gpt_journal.empty() && reviewed_gpt_journal==value("ure_gpt_journal") && !value("ure_gpt_journal_hash").empty(),
                    "confirmation-required","Inspect and review the selected GPT journal first");
                auto target=gpt_target(system,command=="gpt-rollback");
                if(command=="gpt-rollback")publish(ure::gpt_rollback(target,reviewed_gpt_journal,value("ure_gpt_journal_hash"),&system));
                else publish(ure::gpt_resume(target,reviewed_gpt_journal,value("ure_gpt_journal_hash"),&system));
                clear_gpt_review();
            } else {
                if(command=="gpt-repair-plan" || command=="gpt-restore-plan" || command=="gpt-stock-plan" || command=="gpt-journal-inspect")clear_gpt_review();
                auto target=gpt_target(system);
                if(command=="gpt-inspect") {
                    ure::Value result; result["identity"]=target.identity;
                    result["table"]=ure::gpt_inspect(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt()); publish(result);
                } else if(command=="gpt-map")publish(ure::partition_map(target,&system));
                else if(command=="gpt-backup")publish(ure::gpt_backup(target,value("ure_gpt_backup_dir"),"global-os3.0.303.0",&system));
                else if(command=="gpt-compare")publish(ure::gpt_compare(target,value("ure_gpt_backup_dir"),"global-os3.0.303.0",&system));
                else if(command=="gpt-repair-plan" || command=="gpt-restore-plan" || command=="gpt-stock-plan") {
                    ure::Root parent(value("ure_journal_parent"));
                    if(command=="gpt-stock-plan") {
                        const auto lun=value("ure_stock_lun"),inputs=value("ure_stock_inputs"),original=value("ure_stock_identity_backup");
                        ure::require(lun.size()==1 && lun[0]>='0' && lun[0]<='5',"invalid-lun","Select UFS LUN 0 through 5");
                        ure::require(ure::fs::path(inputs).is_absolute() && (original.empty() || ure::fs::path(original).is_absolute()),
                            "invalid-path","Select absolute stock-input and optional original-backup paths");
                        pending_gpt_plan=ure::gpt_stock_plan(target,inputs,static_cast<unsigned>(lun[0]-'0'),"global-os3.0.303.0",original,&system);
                        set("ure_stock_inputs",pending_gpt_plan["stock_inputs_directory"].asString());
                        set("ure_stock_identity_backup",pending_gpt_plan["backup_directory"].asString());
                    } else pending_gpt_plan=ure::gpt_plan(target,command=="gpt-repair-plan" ? "gpt.repair" : "gpt.restore","global-os3.0.303.0",
                        command=="gpt-restore-plan" ? ure::fs::path(value("ure_gpt_backup_dir")) : ure::fs::path(),&system);
                    pending_gpt_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-gpt-"+pending_gpt_plan["operation_id"].asString())).string();
                    const auto file="/tmp/ure-gpt-plan-"+pending_gpt_plan["operation_id"].asString()+".json"; ure::save_json(file,pending_gpt_plan);
                    auto review=pending_gpt_plan; review["journal_directory"]=pending_gpt_journal; review["plan_file"]=file; publish(review);
                    set("ure_gpt_plan_hash",pending_gpt_plan["plan_sha256"].asString());
                    set("ure_gpt_can_execute",target.identity["kind"]=="regular-image" ? "1" : "0");
                    set("ure_status",command=="gpt-stock-plan" ? "Review affected partitions and OS visibility; this restores metadata only" :
                        "Review both partition tables and journal destination; live writes remain gated");
                } else if(command=="gpt-journal-inspect") {
                    const auto review=ure::gpt_journal_inspect(target,value("ure_gpt_journal"),&system); publish(review);
                    reviewed_gpt_journal=value("ure_gpt_journal"); set("ure_gpt_journal_hash",review["plan_sha256"].asString());
                    for(const auto& action:review["recovery_actions"])set("ure_gpt_can_"+action.asString(),"1");
                } else throw ure::Error("unknown-action","Unknown GPT action");
            }
        } else if(command=="load") {
            editor.reset(); selected_root.reset(); pending_plan=ure::Value();
            set("ure_line",""); set("ure_preview","");
            set("ure_plan_hash",""); set("ure_line_number","");
            const auto path=value("ure_root");
            ure::require(!path.empty() && path.front()=='/' && path!="/","root-required","Select an already mounted OS root");
            auto root=std::make_unique<ure::Root>(path);
            auto loaded=std::make_unique<ure::Editor>(*root,value("ure_file"),"global-os3.0.303.0");
            for(const auto& row:loaded->lines())ure::require(row.size()<=8192,"line-too-long","GUI editor accepts lines up to 8192 bytes; use the bounded CLI backend for longer lines");
            selected_root=std::move(root); editor=std::move(loaded); current_line=0; refresh_editor();
            set("ure_status","Loaded; changes remain in memory until confirmed");
        } else {
            ure::require(editor && selected_root,"editor-empty","Load a file first");
            if(command=="apply-line")editor->line(current_line,value("ure_line"));
            else if(command=="previous") { if(current_line)--current_line; }
            else if(command=="next") { if(current_line+1<editor->lines().size())++current_line; }
            else if(command=="insert")editor->insert(current_line,"");
            else if(command=="delete")editor->erase(current_line);
            else if(command=="undo")editor->undo();
            else if(command=="redo")editor->redo();
            else if(command=="replace")editor->replace(value("ure_find"),value("ure_replace"));
            else if(command=="find") {
                const auto needle=value("ure_find"); ure::require(!needle.empty(),"invalid-search","Enter search text");
                const auto rows=editor->lines(); bool found=false;
                for(std::size_t i=1;i<=rows.size();++i) { const auto position=(current_line+i)%rows.size(); if(rows[position].find(needle)!=std::string::npos) { current_line=position; found=true; break; } }
                ure::require(found,"search-empty","Search text was not found");
            } else if(command=="plan") {
                pending_plan=editor->plan(*selected_root); auto review=pending_plan; review.removeMember("payload");
                ure::Root journal_parent(value("ure_journal_parent"));
                pending_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-transaction-"+pending_plan["operation_id"].asString())).string();
                review["journal_directory"]=pending_journal;
                set("ure_plan_hash",pending_plan["plan_sha256"].asString()); publish(review);
                set("ure_status","Review the file identity, backup and checksum before confirming"); return 0;
            } else if(command=="save") {
                ure::require(pending_plan.isObject(),"plan-required","Create and review a save plan first");
                const auto directory=pending_journal; ure::require(!directory.empty(),"journal-required","Review a journal destination before saving");
                publish(ure::transaction_run(*selected_root,pending_plan,directory,value("ure_plan_hash")));
                set("ure_journal",directory);
                set("ure_status","Saved and verified; rollback journal: "+directory);
                editor.reset(); pending_plan=ure::Value(); return 0;
            } else throw ure::Error("unknown-action","Unknown URE action");
            refresh_editor(); set("ure_status","Buffer updated; no file write performed");
        }
        return 0;
    } catch(const ure::Error& error) {
        if(error.code=="gui-job-output-limit")invalidate_reviews();
        if(command.rfind("scale-",0)==0)set("ure_scale_status",error.code+": "+error.what());
        set("ure_status",error.code+": "+error.what());
        ure::Value result; result["error"]["code"]=error.code; result["error"]["message"]=error.what(); publish(result); return 1;
    } catch(const std::exception&) { set("ure_status","Unexpected input or runtime failure"); return 1; }
}
}
namespace {
struct GuiBackendControl {
    std::string job_id,kind,journal;
    std::shared_ptr<ure::Root> root;
    ure::Value plan;
};
struct GuiBackendMailbox {
    std::mutex mutex;
    std::shared_ptr<const GuiBackendControl> selected;
    bool running=false;
    void bind(const std::string& job,const std::string& kind,const ure::Root& root,const ure::Value& plan,const std::string& journal) {
        ure::require((kind=="rescue" || kind=="scrub" || kind=="balance") && ure::hash_valid(plan["plan_sha256"].asString()) &&
            journal.size()<=4096 && ure::json(plan).size()<=4*1024*1024,"invalid-backend-controller","A controller needs a bounded exact native plan and journal");
        ure::Fd retained(::fcntl(root.fd(),F_DUPFD_CLOEXEC,3)); ure::require(retained.get()>=0,"controller-root-unavailable","Cannot retain the original controller root");
        auto captured=std::make_shared<GuiBackendControl>(); captured->job_id=job; captured->kind=kind; captured->journal=journal;
        captured->plan=plan; captured->root=std::make_shared<ure::Root>(std::move(retained));
        std::lock_guard<std::mutex> lock(mutex);
        ure::require(!selected,"owner-control-required","Resolve the previous captured backend before starting another controlled backend");
        selected=std::move(captured); running=true;
    }
    std::shared_ptr<const GuiBackendControl> snapshot(const std::string& job="") {
        std::lock_guard<std::mutex> lock(mutex); return selected && (job.empty() || selected->job_id==job) ? selected : nullptr;
    }
    void result(const std::string& job,const ure::Value& result) {
        if(result["operation_owner_released"]==true) {
            std::lock_guard<std::mutex> lock(mutex); if(selected && selected->job_id==job) { selected.reset(); running=false; }
        }
    }
    void finished(const std::string& job) {
        auto captured=snapshot(job); if(!captured)return;
        // Native errors may occur before journal or owner admission. Observe
        // the cooperating domain on the worker, never under the GUI mutex.
        ure::Value owner;
        try { owner=ure::operation_lease_status(); } catch(...) { /* Uncertain owner remains retained; this worker has still returned. */ }
        const bool idle=owner["available"]==true && owner["retained_owner"]!=true && owner["active_exclusion"]!=true;
        struct stat journal{};
        const bool journal_absent=::lstat(captured->journal.c_str(),&journal)<0 && errno==ENOENT;
        const bool unavailable_before_admission=journal_absent && owner["available"]==false && owner["code"]=="ownership-unavailable";
        std::lock_guard<std::mutex> lock(mutex);
        if(selected!=captured)return;
        running=false; if(idle || unavailable_before_admission)selected.reset();
    }
    bool backend_running(const std::shared_ptr<const GuiBackendControl>& captured) {
        std::lock_guard<std::mutex> lock(mutex); return selected==captured && running;
    }
};
struct GuiManagementOwner {
    std::mutex mutex;
    std::shared_ptr<ManagementSession> session=std::make_shared<ManagementSession>(),running;
    ure::Value inputs;
    std::uint64_t epoch=0,last_poll=0;
    std::string published_status;
    std::string controller_review_locale;
    std::shared_ptr<GuiBackendMailbox> backend=std::make_shared<GuiBackendMailbox>();
    bool accepting=true;
    // Declared last: the owned worker joins before its roots/session are freed.
    ure::GuiJobExecutor jobs;
    ure::GuiJobExecutor controls;
};
GuiManagementOwner management_owner;
ure::Value management_inputs() {
    static constexpr const char* keys[]={
    "ure_db_linux","ure_db_windows","ure_db_esp","ure_db_boot","ure_db_userdata_fs","ure_db_linux_fs",
    "ure_db_esp_size","ure_db_esp_unit","ure_db_linux_size","ure_db_linux_unit","ure_db_linux_boot_size","ure_db_linux_boot_unit",
    "ure_db_windows_size","ure_db_windows_unit","ure_db_hash","ure_db_can_apply","ure_db_erase_ack","ure_db_confirmation","ure_db_edit_role",
    "tw_language",
    "ure_backup_dir",
    "ure_backup_hash",
    "ure_boot_esp",
    "ure_boot_fallback",
    "ure_boot_hash",
    "ure_boot_journal",
    "ure_boot_journal_hash",
    "ure_boot_model",
    "ure_boot_option",
    "ure_boot_partuuid",
    "ure_boot_profile",
    "ure_boot_target",
    "ure_boot_variables",
    "ure_btrfs_action",
    "ure_btrfs_backup",
    "ure_btrfs_device",
    "ure_btrfs_incremental_parent",
    "ure_btrfs_limit",
    "ure_btrfs_name",
    "ure_btrfs_parent",
    "ure_btrfs_path",
    "ure_btrfs_readonly",
    "ure_btrfs_repair",
    "ure_btrfs_root",
    "ure_btrfs_saved",
    "ure_btrfs_size",
    "ure_btrfs_source",
    "ure_btrfs_store",
    "ure_btrfs_unit",
    "ure_btrfs_usage",
    "ure_esp",
    "ure_file",
    "ure_find",
    "ure_form_back",
    "ure_form_field",
    "ure_form_value",
    "ure_fs_action",
    "ure_fs_erase",
    "ure_fs_journal_hash",
    "ure_fs_label",
    "ure_fs_size",
    "ure_fs_type",
    "ure_fs_unit",
    "ure_gpt_backup_dir",
    "ure_gpt_journal",
    "ure_gpt_journal_hash",
    "ure_gpt_kind",
    "ure_gpt_plan_hash",
    "ure_gpt_sector",
    "ure_gpt_source",
    "ure_journal",
    "ure_journal_hash",
    "ure_journal_parent",
    "ure_layout_edit_role",
    "ure_layout_mode",
    "ure_layout_placement",
    "ure_layout_record_contents",
    "ure_layout_record_edits",
    "ure_layout_record_filesystem",
    "ure_layout_record_guid",
    "ure_layout_record_index",
    "ure_layout_userdata_guid",
    "ure_layout_userdata_policy",
    "ure_line",
    "ure_manage_hash",
    "ure_manage_journal",
    "ure_mirror_modes",
    "ure_mirror_refresh",
    "ure_mirror_resolution",
    "ure_mirror_scale_choice",
    "ure_mirror_scale_requested",
    "ure_partition_journal",
    "ure_partition_journal_hash",
    "ure_plan_hash",
    "ure_raw_kind",
    "ure_raw_sector",
    "ure_raw_source",
    "ure_replace",
    "ure_rescue_",
    "ure_rescue_action",
    "ure_rescue_command",
    "ure_rescue_kernel",
    "ure_rescue_timeout",
    "ure_rescue_write",
    "ure_restore_journal",
    "ure_restore_journal_hash",
    "ure_restore_plan_hash",
    "ure_root",
    "ure_scale_choice",
    "ure_scale_directory",
    "ure_ui_scale_applied",
    "ure_ui_scale_percent",
    "ure_stock_identity_backup",
    "ure_stock_inputs",
    "ure_stock_job_boot",
    "ure_stock_job_hash",
    "ure_stock_job_images",
    "ure_stock_job_journal",
    "ure_stock_job_journal_hash",
    "ure_stock_job_model",
    "ure_stock_job_originals",
    "ure_stock_job_reset",
    "ure_stock_job_sku",
    "ure_stock_job_slots",
    "ure_stock_job_super",
    "ure_stock_job_whole_boot",
    "ure_stock_job_zero",
    "ure_stock_lun",
    "ure_stream_journal",
    "ure_stream_journal_hash",
    "ure_stream_manifest",
    "ure_tree_destination",
    "ure_tree_hash",
    "ure_tree_root",
    "ure_tree_store",
    };
    ure::Value input{Json::objectValue}; std::size_t bytes=0;
    const auto capture=[&](const std::string& key) {
        const auto text=value(key); bytes+=text.size();
        ure::require(text.size()<=65536 && bytes<=512*1024,"gui-job-input-limit","Management selections exceed the bounded input budget"); input[key]=text;
    };
    for(const auto* key:keys)capture(key);
    for(const auto* role:{"esp","linux","windows","userdata"})
        for(const auto* field:{"size","unit","filesystem","guid"})capture("ure_layout_"+std::string(role)+"_"+field);
    for(const auto* field:{"size","unit","filesystem","guid"})capture("ure_layout_edit_"+std::string(field));
    return input;
}
bool immediate_display_command(const std::string& command) {
    return command=="scale-apply" || command=="scale-reset" || command=="mirror-apply" ||
        command=="mirror-enable" || command=="mirror-disable" || command=="mirror-modes";
}
void apply_management_updates(const ure::Value& updates) {
    ure::require(updates.isObject() && updates.size()<=256,"gui-job-output-limit","Invalid management publication");
    for(auto it=updates.begin();it!=updates.end();++it)DataManager::SetValue(it.name(),it->asString());
}
void publish_job_status() {
    auto status=management_owner.jobs.status(); const auto control=management_owner.controls.status(); status["controller"]=control;
    const auto captured=management_owner.backend->snapshot();
    if(captured) {
        status["bound_backend"]["job_id"]=captured->job_id; status["bound_backend"]["kind"]=captured->kind;
        status["bound_backend"]["plan_sha256"]=captured->plan["plan_sha256"]; status["bound_backend"]["journal"]=captured->journal;
        status["bound_backend"]["running"]=management_owner.backend->backend_running(captured);
    }
    const auto encoded=ure::json(status); if(encoded==management_owner.published_status)return;
    management_owner.published_status=encoded;
    DataManager::SetValue("ure_job_status",encoded);
    DataManager::SetValue("ure_job_state",status["state"].asString());
    DataManager::SetValue("ure_job_active",status["active"]==true || control["active"]==true ? "1" : "0");
    DataManager::SetValue("ure_job_result_pending",status["result_pending"]==true || control["result_pending"]==true ? "1" : "0");
    DataManager::SetValue("ure_job_backend_owner_id",captured ? captured->job_id : "");
    DataManager::SetValue("ure_job_backend_cleanable",captured && captured->kind!="rescue" && !management_owner.backend->backend_running(captured) ? "1" : "0");
    if(status.isMember("job_id"))DataManager::SetValue("ure_job_id",status["job_id"].asString());
    DataManager::SetValue("ure_job_phase",status.get("phase","idle").asString());
}
void collect_backend_control() {
    const auto result=management_owner.controls.collect(management_owner.epoch);
    if(result["ready"]==true) {
        DataManager::SetValue("ure_job_control_result",ure::json(result));
        DataManager::SetValue("ure_job_notice","The exact backend controller returned. Read its result; an acknowledgement alone does not prove cleanup.");
    }
}
ure::Value queue_backend_control(const std::shared_ptr<const GuiBackendControl>& captured,const std::string& action) {
    ure::require(captured!=nullptr,"owner-control-required","There is no captured backend to control"); collect_backend_control();
    const bool cleanup=action=="verify-cleanup";
    ure::require(!cleanup || (captured->kind!="rescue" && !management_owner.backend->backend_running(captured)),
        "controller-cleanup-unavailable","Wait for the original Btrfs worker before independently verifying cleanup");
    ure::require(cleanup || (captured->kind=="rescue" && action=="rescue-cancel") || (captured->kind=="scrub" && action=="scrub-cancel") ||
        (captured->kind=="balance" && (action=="balance-pause" || action=="balance-cancel")),"invalid-maintenance-control","Controller action must match the captured backend");
    ure::Value input; input["owner_job_id"]=captured->job_id; input["action"]=action; input["plan_sha256"]=captured->plan["plan_sha256"];
    const auto mailbox=management_owner.backend;
    const auto id=management_owner.controls.start(input,management_owner.epoch,{},false,[captured,action,mailbox,cleanup](const auto&,auto& progress) {
        progress.checkpoint("exact-backend-control"); const auto hash=captured->plan["plan_sha256"].asString();
        ure::Value result;
        if(cleanup)result=ure::btrfs_manage_execute(*captured->root,captured->plan,captured->journal,hash);
        else if(captured->kind=="rescue")result=ure::linux_rescue_cancel(captured->journal,hash);
        else result=ure::btrfs_manage_control(*captured->root,captured->plan,captured->journal,action,hash);
        mailbox->result(captured->job_id,result); result["controlled_job_id"]=captured->job_id; return result;
    });
    ure::Value ack; ack["state"]="CONTROLLER_QUEUED"; ack["controller_job_id"]=id; ack["controlled_job_id"]=captured->job_id;
    ack["action"]=action; ack["plan_sha256"]=captured->plan["plan_sha256"]; ack["backend_cleanup_verified"]=false; return ack;
}
int collect_management_job() {
    collect_backend_control();
    const auto status=management_owner.jobs.status();
    if(status["result_pending"]!=true)return 0;
    const auto input=management_inputs();
    const bool same=input==management_owner.inputs && status["view_epoch"].asUInt64()==management_owner.epoch;
    const auto result=management_owner.jobs.collect(same ? management_owner.epoch : management_owner.epoch+1);
    if(result["ready"]!=true)return 0;
    DataManager::SetValue("ure_job_result",ure::json(result));
    const auto& output=result["output"];
    const auto code=output.isMember("exit_code") ? output["exit_code"].asInt() : 1;
    DataManager::SetValue("ure_job_exit_code",code);
    DataManager::SetValue("ure_job_notice",result["cancel_requested"]==true ?
        "The backend returned after a stop request. Read its final result; a request alone does not establish cancellation." :
        "Job finished. Read its final result and journal.");
    if(same && output.isMember("updates")) {
        management_owner.session=std::move(management_owner.running);
        apply_management_updates(output["updates"]);
        for(const auto& [key,message]:management_owner.session->messages)ure_locale::remember(key,message);
    } else {
        management_owner.session=std::make_shared<ManagementSession>();
        management_owner.running.reset();
        if(!same) {
            for(const auto* key:{"ure_manage_hash","ure_plan_hash","ure_backup_hash","ure_tree_hash","ure_gpt_plan_hash","ure_gpt_journal_hash",
                "ure_partition_journal_hash","ure_restore_plan_hash","ure_restore_journal_hash","ure_stream_journal_hash","ure_boot_hash","ure_boot_journal_hash",
                "ure_stock_job_hash","ure_stock_job_journal_hash","ure_fs_journal_hash","ure_journal_hash"})DataManager::SetValue(key,"");
            for(const auto* key:{"ure_manage_can_apply","ure_gpt_can_execute","ure_restore_can_execute","ure_stock_job_can_execute","ure_boot_can_stage"})DataManager::SetValue(key,"0");
            DataManager::SetValue("ure_status","Selections changed during the job. Its result is retained in Job status; review the current target again.");
        } else {
            DataManager::SetValue("ure_output",ure::json(output));
            DataManager::SetValue("ure_status","The job did not publish a complete result. Inspect Job status and the backend journal before continuing.");
        }
    }
    publish_job_status(); return code;
}
}
// Render-loop polling is bounded and never waits for a worker's native I/O.
void ure_gui_poll_jobs() {
    std::unique_lock<std::mutex> lock(management_owner.mutex,std::try_to_lock); if(!lock.owns_lock())return;
    const auto now=ure::monotonic_ms(); if(now-management_owner.last_poll<50)return; management_owner.last_poll=now;
    try { static_cast<void>(collect_management_job()); publish_job_status(); }
    catch(const ure::Error&) { DataManager::SetValue("ure_job_notice","Job publication failed; its backend cleanup remains unverified. Inspect the private journal."); }
}
int GUIAction::uremanager(std::string command) {
    std::unique_lock<std::mutex> lock(management_owner.mutex,std::try_to_lock);
    if(!lock.owns_lock())return 1;
    try {
        if(command=="job-status") { publish_job_status(); return 0; }
        if(command=="job-result") { DataManager::SetValue("ure_output",value("ure_job_result")); return 0; }
        if(command=="job-controller-result") { DataManager::SetValue("ure_output",value("ure_job_control_result")); return 0; }
        if(command=="job-collect")return collect_management_job();
        if(command=="job-cancel") {
            const auto id=value("ure_job_id"); auto ack=management_owner.jobs.request_cancel(id);
            const auto captured=management_owner.backend->snapshot(id);
            if(captured) {
                try { ack["backend_controller"]=queue_backend_control(captured,captured->kind=="rescue" ? "rescue-cancel" : captured->kind+"-cancel"); }
                catch(const ure::Error& error) { ack["backend_controller_error"]=error.code; }
            }
            DataManager::SetValue("ure_job_cancel_ack",ure::json(ack));
            DataManager::SetValue("ure_job_notice","Stop requested. A running native operation may finish before stopping; inspect its final journal and cleanup state.");
            publish_job_status(); return 0;
        }
        if(command=="job-backend-cleanup") {
            const auto captured=management_owner.backend->snapshot(value("ure_job_backend_owner_id"));
            ure::require(captured && value("ure_job_backend_owner_id")==captured->job_id,"owner-control-required","Select the exact retained backend shown in Job status");
            DataManager::SetValue("ure_job_cancel_ack",ure::json(queue_backend_control(captured,"verify-cleanup"))); publish_job_status(); return 0;
        }
        if((command=="btrfs-plan" || command=="btrfs-execute") &&
            (value("ure_btrfs_action")=="scrub-cancel" || value("ure_btrfs_action")=="balance-pause" || value("ure_btrfs_action")=="balance-cancel")) {
            const auto captured=management_owner.backend->snapshot(); ure::require(captured && captured->kind!="rescue","owner-control-required","Inspect the captured Btrfs backend before its control");
            ure::require((captured->kind=="scrub" && value("ure_btrfs_action")=="scrub-cancel") ||
                (captured->kind=="balance" && (value("ure_btrfs_action")=="balance-pause" || value("ure_btrfs_action")=="balance-cancel")),
                "invalid-maintenance-control","Choose the controller action for the original captured maintenance job");
            if(command=="btrfs-plan") {
                ure::Value review; review["operation"]="btrfs.exact-controller"; review["owner_job_id"]=captured->job_id;
                review["action"]=value("ure_btrfs_action"); review["journal"]=captured->journal; review["captured_plan"]=captured->plan;
                const auto text=ure::json(review); ure::require(text.size()<=128*1024,"gui-job-output-limit","Controller review exceeds its display budget");
                DataManager::SetValue("ure_output",text); DataManager::SetValue("ure_manage_hash",captured->plan["plan_sha256"].asString());
                DataManager::SetValue("ure_manage_journal",captured->journal); DataManager::SetValue("ure_job_controller_owner_id",captured->job_id);
                DataManager::SetValue("ure_manage_can_apply","1");
                management_owner.controller_review_locale=value("tw_language");
                DataManager::SetValue("ure_status","Control the original captured filesystem and journal. Confirm its plan hash; current root selections do not retarget it."); return 0;
            }
            if(management_owner.controller_review_locale!=value("tw_language")) {
                DataManager::SetValue("ure_manage_hash",""); DataManager::SetValue("ure_manage_can_apply","0");
                throw ure::Error("review-language-changed","Language changed. Review the captured backend again before confirming.");
            }
            ure::require(value("ure_job_controller_owner_id")==captured->job_id && value("ure_manage_hash")==captured->plan["plan_sha256"].asString(),
                "confirmation-required","Review and confirm this exact captured backend first");
            DataManager::SetValue("ure_job_cancel_ack",ure::json(queue_backend_control(captured,value("ure_btrfs_action"))));
            DataManager::SetValue("ure_manage_can_apply","0"); publish_job_status(); return 0;
        }
        if(command=="job-view-changed") { ++management_owner.epoch; return 0; }
        if(immediate_display_command(command)) {
            ManagementSession display; display.variables=management_inputs(); const auto code=display.run(command);
            apply_management_updates(display.updates); return code;
        }
        static_cast<void>(collect_management_job());
        ure::require(management_owner.accepting && management_owner.jobs.status()["active"]!=true && management_owner.controls.status()["active"]!=true && management_owner.session,
            "gui-job-busy","An owned job, backend controller or shutdown is active. Job status and exact controls remain available.");
        auto input=management_inputs(); auto owned=management_owner.session;
        owned->variables=input; owned->updates=ure::Value(Json::objectValue); owned->update_bytes=0;
        management_owner.inputs=input; ++management_owner.epoch;
        management_owner.running=owned; management_owner.session.reset();
        const auto mailbox=management_owner.backend;
        try {
            const auto id=management_owner.jobs.start(input,management_owner.epoch,{},true,
                [owned,command,mailbox](const ure::GuiJobRequest& request,ure::GuiJobControl& control) {
                    owned->variables=request.inputs(); control.checkpoint("native-operation",0,0,true);
                    const auto id=control.job_id();
                    owned->bind_backend_control=[mailbox,id](const auto& kind,const auto& root,const auto& plan,const auto& journal) { mailbox->bind(id,kind,root,plan,journal); };
                    owned->backend_control_result=[mailbox,id](const auto& result) { mailbox->result(id,result); };
                    int code=1;
                    try { code=owned->run(command); }
                    catch(...) {
                        owned->bind_backend_control={}; owned->backend_control_result={}; mailbox->finished(id); throw;
                    }
                    owned->bind_backend_control={}; owned->backend_control_result={}; mailbox->finished(id);
                    ure::Value result; result["exit_code"]=code; result["command"]=command; result["updates"]=std::move(owned->updates);
                    result["native_io_may_be_noninterruptible"]=true; result["backend_cleanup_verified"]=false; return result;
                });
            DataManager::SetValue("ure_job_id",id);
        } catch(...) {
            management_owner.session=std::move(management_owner.running); static_cast<void>(management_owner.jobs.collect(management_owner.epoch)); throw;
        }
        DataManager::SetValue("ure_job_exit_code",""); DataManager::SetValue("ure_job_result",""); DataManager::SetValue("ure_job_cancel_ack","");
        DataManager::SetValue("ure_job_notice","Working with the reviewed selections. Status remains available; some native operations cannot stop immediately.");
        DataManager::SetValue("ure_status","Working; open Job status for progress and stop requests.");
        publish_job_status(); return 0;
    } catch(const ure::Error& error) {
        DataManager::SetValue("ure_job_notice",error.code+": "+error.what()); return 1;
    } catch(const std::exception&) { DataManager::SetValue("ure_job_notice","Unexpected job admission or publication failure"); return 1; }
}
void ure_gui_shutdown_jobs() {
    // Never join under the GUI state mutex. Native I/O may be noninterruptible.
    {
        std::lock_guard<std::mutex> lock(management_owner.mutex); management_owner.accepting=false;
        const auto captured=management_owner.backend->snapshot();
        if(captured && management_owner.backend->backend_running(captured)) {
            try { static_cast<void>(queue_backend_control(captured,captured->kind=="rescue" ? "rescue-cancel" : captured->kind+"-cancel")); }
            catch(const ure::Error&) { /* Existing controller or unavailable admission: retain and join the original supervisor. */ }
        }
    }
    management_owner.controls.shutdown(false);
    management_owner.jobs.shutdown();
}
