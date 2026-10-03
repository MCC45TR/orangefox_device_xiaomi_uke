// SPDX-License-Identifier: GPL-3.0-or-later
// Project-owned OrangeFox adapter. All storage operations use libuke directly.
#include "objects.hpp"
#include "../data.hpp"
#include "../gui.hpp"
#include "uke.h"
#include "pages.hpp"
#include "minuitwrp/minui.h"
#include "../minuitwrp/display-mirror.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <fcntl.h>
#include <mutex>
#include <set>
#include <thread>

namespace {
std::mutex session_mutex;
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
std::string reviewed_partition_journal;
ure::Value reviewed_partition_selection;
std::string reviewed_tree_store,reviewed_tree_root,reviewed_tree_destination;
ure::Value managed_plan,managed_selection;
std::string managed_kind,managed_journal,reviewed_filesystem_journal;
std::string edited_management_field;
ure::Value boot_plan,boot_reviewed_selection,boot_journal_selection;
std::string boot_pending_journal,boot_reviewed_journal;
std::atomic<bool> maintenance_running{false};
std::size_t current_line=0;
std::string value(const std::string& name) { std::string result; DataManager::GetValue(name,result); return result; }
void publish(const ure::Value& data) { DataManager::SetValue("ure_output",ure::json(data)); }
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
    DataManager::SetValue("ure_boot_hash",""); DataManager::SetValue("ure_boot_journal_hash",""); DataManager::SetValue("ure_boot_can_stage","0");
    for(const auto* action:{"recover","cancel","fallback-fixture"})DataManager::SetValue(std::string("ure_boot_can_")+action,"0");
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
    DataManager::SetValue("ure_manage_hash",managed_plan["plan_sha256"].asString());
    DataManager::SetValue("ure_manage_can_apply",kind!="filesystem" || managed_plan["target_identity"]["kind"]=="regular-image" ? "1" : "0");
    std::string summary;
    if(kind=="filesystem") {
        const auto& request=managed_plan["request"]; summary=request["action"].asString()+" / "+request["filesystem"].asString()+" on "+value("ure_raw_source")+"\n";
        if(request["action"]=="resize")summary+="Requested filesystem size: "+std::to_string(request["target_bytes"].asUInt64()/1048576)+" MiB\n";
        summary+="Required journal space: "+std::to_string(managed_plan["estimated_max_journal_bytes"].asUInt64()/1048576)+" MiB\n";
        summary+="Partition boundaries stay unchanged. "+managed_plan["risk"].asString();
        if(managed_plan["target_identity"]["kind"]!="regular-image")summary+="\nLive application is blocked by the current storage preflight.";
    } else if(kind=="rescue") {
        summary="System: "+managed_plan["distribution_family"].asString()+"; action: "+managed_plan["request"]["action"].asString()+"\n";
        summary+="Timeout: "+std::to_string(managed_plan["request"]["timeout_seconds"].asUInt())+" seconds; automatic connections: "+std::to_string(managed_plan["connections"].size())+"\n";
        summary+=managed_plan["risk"].asString();
    } else summary=managed_plan.get("risk",managed_plan.get("coherence","Review the selected read-only snapshot, incremental parent and backup store")).asString();
    summary+="\nJournal: "+managed_journal; DataManager::SetValue("ure_manage_summary",summary);
    DataManager::SetValue("ure_status","Review the selected target, action, required space, warnings and journal before confirming");
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
    DataManager::SetValue("ure_backup_hash",pending_backup["plan_sha256"].asString());
    DataManager::SetValue("ure_status","Review source identity, coherence and destination before capture or host transfer");
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
    DataManager::SetValue("ure_layout_graph",""); DataManager::SetValue("ure_layout_review","Calculate and review the current selections before applying");
    for(const auto* name:{"ure_gpt_plan_hash","ure_gpt_journal_hash","ure_gpt_can_execute","ure_gpt_can_rollback","ure_gpt_can_resume",
        "ure_partition_journal_hash","ure_partition_can_resume","ure_partition_can_rollback","ure_partition_can_cancel"})DataManager::SetValue(name,"");
}
ure::Value partition_selection() {
    ure::Value selected; for(const auto* key:{"ure_gpt_kind","ure_gpt_source","ure_gpt_sector","ure_partition_journal"})selected[key]=value(key); return selected;
}
void clear_stock_review() {
    pending_stock_plan=ure::Value(); reviewed_stock_choices=ure::Value(); pending_stock_journal.clear(); reviewed_stock_journal.clear();
    for(const auto* name:{"ure_stock_job_hash","ure_stock_job_journal_hash","ure_stock_job_summary","ure_stock_job_can_execute",
        "ure_stock_job_can_resume","ure_stock_job_can_rollback","ure_stock_job_can_cancel"})DataManager::SetValue(name,"");
}
ure::Value stock_selection() {
    ure::Value selected;
    for(const auto* key:{"ure_stock_inputs","ure_stock_job_images","ure_stock_job_originals","ure_stock_job_model","ure_stock_job_sku",
        "ure_stock_job_boot","ure_stock_job_slots","ure_stock_job_super","ure_stock_job_reset","ure_stock_job_zero","ure_journal_parent"})selected[key]=value(key);
    return selected;
}
ure::Value stock_request() {
    const ure::fs::path images(value("ure_stock_job_images")),originals(value("ure_stock_job_originals"));
    ure::require(images.is_absolute() && (originals.empty() || originals.is_absolute()),"invalid-path","Select absolute image and optional original GPT directories");
    ure::Value request; request["schema"]=1; request["format"]="ure-stock-job-request"; request["firmware_profile"]="global-os3.0.303.0";
    request["stock_inputs_directory"]=value("ure_stock_inputs"); request["model"]=value("ure_stock_job_model"); request["sku"]=value("ure_stock_job_sku");
    request["erase_android_data"]=choice("ure_stock_job_reset"); request["zero_sparse_holes"]=choice("ure_stock_job_zero");
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
std::string stock_summary(const ure::Value& plan,const std::string& journal) {
    std::string text="Declared model: "+plan["request"]["model"].asString()+"; SKU: "+plan["request"]["sku"].asString()+"\nImage workflow; tablet identity and boot acceptance pending.\n";
    for(const auto& lun:plan["luns"])text+="LUN "+std::to_string(lun["lun"].asUInt())+": "+std::to_string(lun["identity"]["bytes"].asUInt64()/1048576)+" MiB\n";
    text+="Selected OS payloads: "+std::to_string(plan["request"]["payloads"].size())+"\nRequired journal space: "+std::to_string(plan["estimated_journal_bytes"].asUInt64()/1048576)+" MiB\nJournal: "+journal+"\n";
    for(const auto& row:plan["regions"])if(row["role"]=="payload")text+=row["name"].asString()+": program "+std::to_string(row["bytes"].asUInt64()/1048576)+" MiB; preserve tail "+std::to_string((row["destination_capacity"].asUInt64()-row["bytes"].asUInt64())/1048576)+" MiB\n";
    for(const auto& warning:plan["warnings"])text+=warning.asString()+"\n";
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
        DataManager::SetValue(name,"");
}
void clear_stream_review() {
    reviewed_stream_journal.clear();
    for(const auto* name:{"ure_stream_journal_hash","ure_stream_can_rollback","ure_stream_can_finish","ure_stream_can_cancel"})DataManager::SetValue(name,"");
}
void refresh_editor() {
    const auto rows=editor->lines();
    current_line=std::min(current_line,rows.size()-1);
    DataManager::SetValue("ure_line",rows[current_line]);
    DataManager::SetValue("ure_line_number",std::to_string(current_line+1)+" / "+std::to_string(rows.size()));
    DataManager::SetValue("ure_preview",editor->text().substr(0,65536));
    pending_plan=ure::Value(); pending_journal.clear(); DataManager::SetValue("ure_plan_hash","");
}
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
    if(name.compare(0,4,"ure_")==0)return false;
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
    static constexpr const char* items[]={"nav_item_1","nav_item_2","nav_item_3","nav_item_4"};
    static constexpr const char* pills[]={"np_pill_x1","np_pill_x2","np_pill_x3","np_pill_x4"};
    static constexpr const char* cards[]={"ws_cards_x1","ws_cards_x2","ws_cards_x3","ws_cards_x4"};
    for(int index=1;index<=4;++index) {
        const int center=48+nav_width*(2*index-1)/8;
        if(name==items[index-1]) { output=std::to_string(center); return true; }
        if(name==pills[index-1]) { output=std::to_string(center-96); return true; }
        if(name==cards[index-1]) { output=std::to_string(28+(width-56)*(2*index-1)/8); return true; }
    }
    if(name=="nav_item_w")output=std::to_string(nav_width/4);
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
int GUIAction::uremanager(std::string command) {
    std::lock_guard<std::mutex> guard(session_mutex);
    try {
        ure::Root system("/");
        const auto btrfs_action=value("ure_btrfs_action");
        const bool control=btrfs_action=="scrub-cancel" || btrfs_action=="balance-pause" || btrfs_action=="balance-cancel";
        ure::require(!maintenance_running || command=="btrfs-info" || command=="btrfs-scrub-status" || command=="btrfs-balance-status" ||
            ((command=="btrfs-plan" || command=="btrfs-execute") && control),"operation-busy","A native maintenance job is running; inspect status or review its cancellation");
        if(command=="mirror-enable" || command=="mirror-disable") {
            gr_external_enable(command=="mirror-enable");
            DataManager::SetValue("ure_mirror_status","Display change queued for the render thread");
        } else if(command=="mirror-apply") {
            int width=0,height=0,rate=0;
            ure::require(uke_display::parse_selection(value("ure_mirror_resolution"),value("ure_mirror_refresh"),width,height,rate),
                "invalid-display-mode","Select a valid resolution and refresh rate");
            ure::require(gr_external_select(width,height,rate),"invalid-display-mode","Output selection is outside the supported range");
            DataManager::SetValue("ure_mirror_status","Mode change queued; unsupported modes preserve the active output");
        } else if(command=="mirror-modes") {
            DataManager::SetValue("ure_mirror_modes",gr_external_modes());
        } else if(command=="scale-apply" || command=="scale-reset" || command=="scale-load") {
            int percent=75;
            if(command=="scale-apply")percent=ure::display_scale_parse(value("ure_scale_choice"));
            if(command=="scale-load")percent=ure::display_settings_load(value("ure_scale_directory"))["scale_percent"].asInt();
            DataManager::SetValue("ure_ui_scale_percent",percent);
            DataManager::SetValue("ure_scale_status",command=="scale-load" ? "Saved scale loaded; no storage was mounted" : "Scale applied to text, icons and touch targets");
            PageManager::RequestUreReload();
        } else if(command=="scale-save") {
            const auto saved=ure::display_settings_save(value("ure_scale_directory"),ure::display_scale_parse(value("ure_ui_scale_applied")));
            DataManager::SetValue("ure_scale_status",saved["volatile_filesystem"]==true ?
                "Saved on volatile storage; this setting will be lost on reboot" : "Saved and read back; load this directory after mounting it on future boots");
        } else if(command=="capabilities")publish(ure::capabilities(system));
        else if(command=="storage")publish(ure::storage_graph(system));
        else if(command=="diagnose")publish(ure::diagnose(system,"all"));
        else if(command=="stock-choice-changed")clear_stock_review();
        else if(command=="stock-job-plan") {
            clear_stock_review(); ure::Root parent(value("ure_journal_parent")); pending_stock_plan=ure::stock_job_plan(stock_request());
            reviewed_stock_choices=stock_selection(); pending_stock_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-stock-"+pending_stock_plan["operation_id"].asString())).string();
            DataManager::SetValue("ure_stock_job_hash",pending_stock_plan["plan_sha256"].asString()); DataManager::SetValue("ure_stock_job_can_execute","1");
            DataManager::SetValue("ure_stock_job_summary",stock_summary(pending_stock_plan,pending_stock_journal)); publish(pending_stock_plan);
        } else if(command=="stock-job-execute") {
            if(ure::json(stock_selection())!=ure::json(reviewed_stock_choices)) { clear_stock_review(); throw ure::Error("stale-plan","Stock model, SKU, images, source, reset or slot choices changed; review a fresh plan"); }
            ure::require(pending_stock_plan.isObject() && !pending_stock_journal.empty() && pending_stock_plan["plan_sha256"].asString()==value("ure_stock_job_hash"),
                "plan-required","Review the complete six-LUN stock job first");
            publish(ure::stock_job_execute(pending_stock_plan,pending_stock_journal,value("ure_stock_job_hash")));
            DataManager::SetValue("ure_stock_job_journal",pending_stock_journal); clear_stock_review();
        } else if(command=="stock-job-inspect") {
            clear_stock_review(); const auto review=ure::stock_job_recover(value("ure_stock_job_journal"),"inspect"); publish(review);
            reviewed_stock_journal=value("ure_stock_job_journal"); DataManager::SetValue("ure_stock_job_journal_hash",review["plan_sha256"].asString());
            for(const auto& action:review["recovery_actions"])DataManager::SetValue("ure_stock_job_can_"+action.asString(),"1");
        } else if(command=="stock-job-resume" || command=="stock-job-rollback" || command=="stock-job-cancel") {
            ure::require(!reviewed_stock_journal.empty() && reviewed_stock_journal==value("ure_stock_job_journal") && !value("ure_stock_job_journal_hash").empty(),
                "confirmation-required","Inspect and review the selected six-LUN journal first");
            publish(ure::stock_job_recover(reviewed_stock_journal,command.substr(10),value("ure_stock_job_journal_hash"))); clear_stock_review();
        } else if(command=="boot-clear-review") {
            boot_clear_review();
        } else if(command=="boot-route-inventory") {
            auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables"); publish(ure::boot_route_inventory(esp,variables));
            DataManager::SetValue("ure_status","Registered EFI entries and unchanged default; Uke/Aloha device routing is not yet accepted");
        } else if(command=="boot-route-plan") {
            boot_clear_review(); auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables");
            boot_plan=ure::boot_route_plan(esp,variables,boot_request_fields()); boot_reviewed_selection=boot_selection();
            ure::Root parent(value("ure_journal_parent"));
            boot_pending_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-boot-"+boot_plan["operation_id"].asString())).string();
            auto review=boot_plan; review["journal_directory"]=boot_pending_journal; publish(review);
            DataManager::SetValue("ure_boot_hash",boot_plan["plan_sha256"].asString());
            DataManager::SetValue("ure_boot_can_stage",boot_plan["fixture_execute_allowed"].asBool() ? "1" : "0");
            DataManager::SetValue("ure_boot_summary","One-time "+value("ure_boot_target")+": EFI option "+boot_plan["selected"]["number"].asString()+
                " / "+boot_plan["selected"]["description"].asString()+"\nPreserved default: "+boot_plan["fallback"]["number"].asString()+
                " / "+boot_plan["fallback"]["description"].asString()+"\n"+boot_plan["risk"].asString()+"\nJournal: "+boot_pending_journal);
        } else if(command=="boot-route-stage-fixture") {
            ure::require(boot_plan.isObject() && !boot_pending_journal.empty() && boot_plan["plan_sha256"].asString()==value("ure_boot_hash"),
                "review-required","Review the exact one-time request first");
            if(ure::json(boot_selection())!=ure::json(boot_reviewed_selection)) { boot_clear_review(); throw ure::Error("stale-plan","Boot target, profile, ESP or journal choices changed; review a new request"); }
            auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables");
            publish(ure::boot_route_execute(esp,variables,boot_plan,boot_pending_journal,value("ure_boot_hash")));
            DataManager::SetValue("ure_boot_journal",boot_pending_journal); boot_clear_review();
        } else if(command=="boot-journal-history") {
            publish(ure::boot_route_history(value("ure_boot_journal")));
        } else if(command=="boot-journal-inspect") {
            boot_clear_review(); auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables");
            const auto review=ure::boot_route_action(esp,variables,value("ure_boot_journal"),"inspect"); publish(review);
            boot_reviewed_journal=value("ure_boot_journal"); boot_journal_selection=boot_context_selection();
            DataManager::SetValue("ure_boot_journal_hash",review["plan_sha256"].asString());
            for(const auto& action:review["recovery_actions"])DataManager::SetValue("ure_boot_can_"+action.asString(),"1");
        } else if(command=="boot-journal-recover" || command=="boot-journal-cancel" || command=="boot-journal-fallback-fixture") {
            ure::require(!boot_reviewed_journal.empty() && !value("ure_boot_journal_hash").empty() &&
                ure::json(boot_context_selection())==ure::json(boot_journal_selection),"review-required","Inspect the selected journal and its exact ESP/variable store first");
            auto esp=os_root("ure_boot_esp"),variables=os_root("ure_boot_variables");
            publish(ure::boot_route_action(esp,variables,boot_reviewed_journal,command.substr(13),value("ure_boot_journal_hash"))); boot_clear_review();
        } else if(command.rfind("manage-edit-",0)==0) {
            const auto field=command.substr(12);
            static const std::set<std::string> allowed{"ure_esp","ure_journal_parent","ure_fs_size","ure_fs_label","ure_rescue_command","ure_rescue_kernel","ure_rescue_timeout",
                "ure_manage_journal","ure_btrfs_root","ure_btrfs_path","ure_btrfs_source","ure_btrfs_backup","ure_btrfs_saved","ure_btrfs_size","ure_btrfs_device",
                "ure_btrfs_usage","ure_btrfs_limit","ure_btrfs_store","ure_btrfs_parent","ure_btrfs_name","ure_btrfs_incremental_parent",
                "ure_boot_esp","ure_boot_variables","ure_boot_option","ure_boot_fallback","ure_boot_partuuid","ure_boot_journal"};
            ure::require(allowed.count(field),"invalid-field","Select a supported management field");
            edited_management_field=field;
            DataManager::SetValue("ure_form_field",field); DataManager::SetValue("ure_form_value",value(field));
            DataManager::SetValue("ure_form_back",field.rfind("ure_boot_",0)==0 ? "ure_boot_manager" : field.rfind("ure_btrfs_",0)==0 ? "ure_btrfs" : field.rfind("ure_rescue_",0)==0 || field=="ure_esp" ? "ure_linux" : "ure_filesystems");
        } else if(command=="manage-field-save") {
            const auto field=value("ure_form_field");
            ure::require(!edited_management_field.empty() && field==edited_management_field && value("ure_form_value").size()<=4096,"invalid-field","Invalid management field selection");
            // The editable field is selected exclusively by manage-edit-* above.
            DataManager::SetValue(field,value("ure_form_value")); DataManager::SetValue("ure_manage_hash",""); DataManager::SetValue("ure_manage_can_apply","0");
            boot_clear_review();
            edited_management_field.clear();
        } else if(command=="linux-audit") {
            auto root=os_root(); auto esp=selected_esp(); const auto report=ure::linux_boot_audit(root,esp.get()); publish(report);
            DataManager::SetValue("ure_status",std::to_string(report["error_count"].asUInt())+" errors, "+std::to_string(report["warning_count"].asUInt())+" warnings; metadata inspection only");
        } else if(command=="rescue-plan") {
            auto root=os_root(); auto esp=selected_esp(); const auto request=rescue_request();
            review_management("rescue",ure::linux_rescue_plan(root,request,esp.get()),management_selection("rescue",request));
        } else if(command=="rescue-execute") {
            const auto request=rescue_request(); reviewed_management("rescue",management_selection("rescue",request)); auto root=os_root(); auto esp=selected_esp();
            DataManager::SetValue("ure_manage_journal",managed_journal);
            const auto result=ure::linux_rescue_execute(root,managed_plan,managed_journal,value("ure_manage_hash"),esp.get()); publish(result);
            DataManager::SetValue("ure_status",result["state"].asString()+"; private console is available in the session journal");
            DataManager::SetValue("ure_manage_hash",""); DataManager::SetValue("ure_manage_can_apply","0"); if(result["successful"]!=true)return 1;
        } else if(command=="rescue-inspect") {
            auto store=ure::private_directory(value("ure_manage_journal"),false); ure::Value result;
            result["plan"]=ure::parse_json(store.read("plan.json")); result["state"]=ure::parse_json(store.read("state.json"));
            if(store.exists("console.log")) { auto file=store.open("console.log",O_RDONLY); const auto bytes=ure::storage_bytes(file.get());
                result["console_preview"]=ure::storage_read(file.get(),0,static_cast<std::size_t>(std::min<std::uint64_t>(bytes,65536))); result["console_preview_truncated"]=bytes>65536; }
            publish(result);
        } else if(command=="filesystem-capabilities")publish(ure::filesystem_capabilities());
        else if(command=="filesystem-inspect" || command=="filesystem-check" || command=="storage-preflight") {
            auto target=backup_target(system);
            publish(command=="filesystem-inspect" ? ure::filesystem_probe(target.descriptor.get()) : command=="filesystem-check" ? ure::filesystem_check(target.descriptor.get()) : ure::storage_preflight(system,target,"global-os3.0.303.0"));
        } else if(command=="filesystem-plan") {
            auto target=backup_target(system); const auto request=filesystem_request(target.identity["bytes"].asUInt64());
            review_management("filesystem",ure::filesystem_operation_plan(system,target,request,"global-os3.0.303.0"),management_selection("filesystem",request));
        } else if(command=="filesystem-execute") {
            auto target=backup_target(system,true); const auto request=filesystem_request(target.identity["bytes"].asUInt64()); reviewed_management("filesystem",management_selection("filesystem",request));
            DataManager::SetValue("ure_manage_journal",managed_journal); publish(ure::filesystem_operation_execute(system,target,managed_plan,managed_journal,value("ure_manage_hash")));
            DataManager::SetValue("ure_status","Filesystem applied and independently checked; complete original bytes remain in the journal");
            DataManager::SetValue("ure_manage_hash",""); DataManager::SetValue("ure_manage_can_apply","0");
        } else if(command=="filesystem-journal-inspect") {
            auto target=backup_target(system); const auto report=ure::filesystem_operation_recover(system,target,value("ure_manage_journal"),"inspect",""); publish(report);
            reviewed_filesystem_journal=value("ure_manage_journal"); DataManager::SetValue("ure_fs_journal_hash",report["plan_sha256"].asString());
            managed_selection=management_selection("filesystem");
            for(const auto* action:{"resume","rollback","cancel"})DataManager::SetValue("ure_fs_can_"+std::string(action),"0");
            for(const auto& action:report["application"]["recovery_actions"])DataManager::SetValue("ure_fs_can_"+action.asString(),"1");
            if(report["original_unchanged_verified"]==true)DataManager::SetValue("ure_fs_can_cancel","1");
        } else if(command=="filesystem-resume" || command=="filesystem-rollback" || command=="filesystem-cancel") {
            ure::require(reviewed_filesystem_journal==value("ure_manage_journal") && !value("ure_fs_journal_hash").empty() &&
                ure::json(managed_selection)==ure::json(management_selection("filesystem")),"review-required","Inspect this filesystem journal and current target first");
            const auto action=command.substr(11); auto target=backup_target(system,action!="cancel");
            publish(ure::filesystem_operation_recover(system,target,reviewed_filesystem_journal,action,value("ure_fs_journal_hash")));
            DataManager::SetValue("ure_fs_journal_hash",""); reviewed_filesystem_journal.clear();
        } else if(command.rfind("btrfs-",0)==0) {
            if(command=="btrfs-send-verify")publish(ure::btrfs_send_verify(value("ure_btrfs_store")));
            else if(command=="btrfs-backup-inspect")publish(ure::btrfs_backup_inspect(value("ure_btrfs_store")));
            else {
                auto root=os_root("ure_btrfs_root");
                if(command=="btrfs-info" || command=="btrfs-subvolumes" || command=="btrfs-usage" || command=="btrfs-device-stats" || command=="btrfs-scrub-status" || command=="btrfs-balance-status")publish(ure::btrfs_native_info(root,command.substr(6)));
                else if(command=="btrfs-plan") { const auto request=btrfs_request(root); review_management("btrfs",ure::btrfs_manage_plan(root,request,"global-os3.0.303.0"),management_selection("btrfs",request)); }
                else if(command=="btrfs-execute") {
                    reviewed_management("btrfs",management_selection("btrfs",btrfs_request(root))); DataManager::SetValue("ure_manage_journal",managed_journal);
                    if(managed_plan["request"]["action"]=="scrub" || managed_plan["request"]["action"]=="balance") {
                        ure::require(!maintenance_running.exchange(true),"operation-busy","A native maintenance job is already running");
                        const auto plan=managed_plan; const auto directory=managed_journal,path=value("ure_btrfs_root"),confirmation=value("ure_manage_hash");
                        DataManager::SetValue("ure_maintenance_state","RUNNING");
                        try { std::thread([plan,directory,path,confirmation] {
                            try { ure::Root selected(path); const auto result=ure::btrfs_manage_execute(selected,plan,directory,confirmation); publish(result); DataManager::SetValue("ure_maintenance_state",result["state"].asString()); }
                            catch(const ure::Error& error) { ure::Value failure; failure["error"]["code"]=error.code; failure["error"]["message"]=error.what(); publish(failure); DataManager::SetValue("ure_maintenance_state","INTERRUPTED: inspect native status and journal"); }
                            catch(...) { DataManager::SetValue("ure_maintenance_state","FAILED: inspect the private journal"); }
                            maintenance_running=false;
                        }).detach(); } catch(...) { maintenance_running=false; throw; }
                        DataManager::SetValue("ure_status","Native maintenance is running; status and reviewed cancellation remain available");
                    } else publish(ure::btrfs_manage_execute(root,managed_plan,managed_journal,value("ure_manage_hash")));
                    DataManager::SetValue("ure_manage_hash",""); DataManager::SetValue("ure_manage_can_apply","0");
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
                    DataManager::SetValue("ure_manage_hash",""); DataManager::SetValue("ure_manage_can_apply","0");
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
                DataManager::SetValue("ure_journal_hash","");
                for(const auto* action:{"resume","cancel","rollback"})DataManager::SetValue(std::string("ure_can_")+action,"0");
                const auto inspected=ure::transaction_inspect(root,value("ure_journal")); publish(inspected);
                DataManager::SetValue("ure_journal_hash",inspected["plan_sha256"].asString());
                for(const auto& action:inspected["recovery_actions"])if(action=="resume" || action=="cancel" || action=="rollback")
                    DataManager::SetValue("ure_can_"+action.asString(),"1");
                DataManager::SetValue("ure_status","Review the current file and verified backup before choosing an action");
            } else {
                const auto confirmation=value("ure_journal_hash"); ure::require(!confirmation.empty(),"confirmation-required","Inspect and review this journal first");
                if(command=="journal-resume")publish(ure::transaction_resume(root,value("ure_journal"),confirmation));
                if(command=="journal-cancel")publish(ure::transaction_cancel(root,value("ure_journal"),confirmation));
                if(command=="journal-rollback")publish(ure::transaction_rollback(root,value("ure_journal"),confirmation));
                DataManager::SetValue("ure_journal_hash","");
                for(const auto* action:{"resume","cancel","rollback"})DataManager::SetValue(std::string("ure_can_")+action,"0");
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
                DataManager::SetValue("ure_status","Review this plan on the host; verify both complete host backups before starting transfer");
            } else if(command=="stream-inspect") {
                auto target=backup_target(system); const auto review=ure::restore_stream_status(system,target,value("ure_stream_journal")); publish(review);
                reviewed_stream_journal=value("ure_stream_journal"); DataManager::SetValue("ure_stream_journal_hash",review["plan_sha256"].asString());
                for(const auto& action:review["recovery_actions"]) {
                    if(action=="host-rollback")DataManager::SetValue("ure_stream_can_rollback","1");
                    if(action=="finish" || action=="cancel")DataManager::SetValue("ure_stream_can_"+action.asString(),"1");
                }
                DataManager::SetValue("ure_status","Review current bytes; reconnect the host for remaining restore or rollback chunks");
            } else if(command=="stream-rollback" || command=="stream-finish" || command=="stream-cancel") {
                ure::require(!reviewed_stream_journal.empty() && reviewed_stream_journal==value("ure_stream_journal") && !value("ure_stream_journal_hash").empty(),
                    "confirmation-required","Inspect and review this host-assisted journal first");
                auto target=backup_target(system,command=="stream-rollback"); const auto confirmation=value("ure_stream_journal_hash");
                if(command=="stream-rollback")publish(ure::restore_stream_rollback(system,target,reviewed_stream_journal,confirmation));
                else if(command=="stream-finish")publish(ure::restore_stream_finish(system,target,reviewed_stream_journal,confirmation));
                else publish(ure::restore_stream_cancel(system,target,reviewed_stream_journal,confirmation));
                clear_stream_review();
                DataManager::SetValue("ure_status",command=="stream-rollback" ? "Rollback direction recorded; reconnect the host to transfer original chunks" : "Journal action completed and verified");
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
                DataManager::SetValue("ure_restore_plan_hash",pending_restore_plan["plan_sha256"].asString());
                DataManager::SetValue("ure_restore_can_execute",target.identity["kind"]=="regular-image" ? "1" : "0");
                DataManager::SetValue("ure_status","Review complete-object overwrite, required free space and rollback destination");
            } else if(command=="restore-execute") {
                ure::require(pending_restore_plan.isObject() && value("ure_restore_plan_hash")==pending_restore_plan["plan_sha256"].asString(),"confirmation-required","Create and review this restore plan first");
                ure::require(value("ure_backup_dir")==pending_restore_backup && ure::fs::path(pending_restore_journal).parent_path()==ure::fs::path(value("ure_journal_parent")),"stale-plan","Backup or journal destination changed; review a new plan");
                auto target=backup_target(system,true);
                DataManager::SetValue("ure_restore_journal",pending_restore_journal);
                publish(ure::restore_execute(system,target,pending_restore_plan,pending_restore_journal,value("ure_restore_plan_hash")));
                DataManager::SetValue("ure_restore_journal",pending_restore_journal); clear_restore_review();
            } else if(command=="restore-inspect") {
                auto target=backup_target(system); const auto review=ure::restore_inspect(system,target,value("ure_restore_journal")); publish(review);
                reviewed_restore_journal=value("ure_restore_journal"); DataManager::SetValue("ure_restore_journal_hash",review["plan_sha256"].asString());
                for(const auto& action:review["recovery_actions"])DataManager::SetValue("ure_restore_can_"+action.asString(),"1");
                DataManager::SetValue("ure_status","Review verified current bytes and available recovery actions");
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
                pending_tree=ure::Value(); DataManager::SetValue("ure_tree_hash","");
                if(command=="tree-plan") {
                    const auto path=value("ure_tree_root"); ure::require(ure::fs::path(path).is_absolute() && path!="/","root-required","Select an already mounted Linux or home directory");
                    ure::Root source(path); pending_tree=ure::backup_tree_plan(source,".","global-os3.0.303.0",store);
                } else pending_tree=ure::backup_tree_inspect(store);
                reviewed_tree_store=store; reviewed_tree_root=value("ure_tree_root"); reviewed_tree_destination=value("ure_tree_destination");
                DataManager::SetValue("ure_tree_hash",pending_tree["plan_sha256"].asString());
                auto review=pending_tree; review["selected_root"]=reviewed_tree_root; review["backup_store"]=store; review["restore_destination"]=reviewed_tree_destination;
                publish(review); DataManager::SetValue("ure_status","Review source, metadata, backup store and new restore destination; no mount or unlock was performed");
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
                pending_tree=ure::Value(); DataManager::SetValue("ure_tree_hash","");
            } else throw ure::Error("unknown-command","Unknown directory backup action");
        } else if(command.rfind("raw-",0)==0) {
            if(command=="raw-image" || command=="raw-live") {
                pending_backup=ure::Value(); pending_backup_directory.clear(); DataManager::SetValue("ure_backup_hash","");
                DataManager::SetValue("ure_raw_kind",command=="raw-image" ? "image" : "live"); DataManager::SetValue("ure_raw_source","");
            } else if(command=="raw-usage") {
                ure::require(value("ure_raw_kind")=="live","live-source-required","Usage observations apply to a selected live Storage Graph identity");
                publish(ure::storage_usage(system,value("ure_raw_source")));
            } else if(command=="raw-plan") {
                pending_backup=ure::Value(); DataManager::SetValue("ure_backup_hash","");
                auto target=backup_target(system); pending_backup_directory=value("ure_backup_dir");
                DataManager::SetValue("ure_raw_source",target.identity[value("ure_raw_kind")=="live" ? "stable_id" : "path"].asString());
                pending_backup=ure::backup_storage_plan(system,target,"global-os3.0.303.0",64*1024*1024); review_backup();
            } else if(command=="raw-capture") {
                ure::require(pending_backup.isObject() && pending_backup["plan_sha256"].asString()==value("ure_backup_hash"),"plan-required","Review a storage backup plan first");
                validate_backup_selection(pending_backup);
                ure::require(value("ure_backup_dir")==pending_backup_directory,"stale-plan","Destination changed; review a new backup plan");
                publish(ure::backup_capture(system,pending_backup,pending_backup_directory,false));
                pending_backup=ure::Value(); DataManager::SetValue("ure_backup_hash","");
            } else if(command=="raw-resume") {
                const auto directory=value("ure_backup_dir"); const auto plan=ure::json_file(ure::fs::path(directory)/"plan.json");
                if(value("ure_raw_kind")=="live") {
                    const auto selected=backup_target(system); DataManager::SetValue("ure_raw_source",selected.identity["stable_id"].asString());
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
                    pending_backup=ure::Value(); DataManager::SetValue("ure_backup_hash","");
                    pending_backup_directory=value("ure_backup_dir");
                    pending_backup=ure::backup_plan(root,value("ure_file"),"global-os3.0.303.0");
                    review_backup();
                } else if(command=="backup-capture") {
                    ure::require(pending_backup.isObject() && pending_backup["plan_sha256"].asString()==value("ure_backup_hash"),"plan-required","Review a backup plan first");
                    ure::require(value("ure_backup_dir")==pending_backup_directory,"stale-plan","Backup destination changed; review a new plan");
                    publish(ure::backup_capture(root,pending_backup,pending_backup_directory,false));
                    pending_backup=ure::Value(); DataManager::SetValue("ure_backup_hash","");
                } else publish(ure::backup_capture(root,ure::json_file(ure::fs::path(value("ure_backup_dir"))/"plan.json"),value("ure_backup_dir"),true));
            }
        } else if(command=="partition-job-inspect") {
            clear_gpt_review(); auto target=gpt_target(system); const auto report=ure::partition_job_recover(system,target,value("ure_partition_journal"),"inspect"); publish(report);
            reviewed_partition_journal=value("ure_partition_journal"); reviewed_partition_selection=partition_selection();
            DataManager::SetValue("ure_partition_journal_hash",report["plan_sha256"].asString());
            for(const auto& action:report["recovery_actions"])DataManager::SetValue("ure_partition_can_"+action.asString(),"1");
            DataManager::SetValue("ure_status","Journal readback: "+report["classification"].asString()+"; choose only an available recovery action");
        } else if(command=="partition-job-resume" || command=="partition-job-rollback" || command=="partition-job-cancel") {
            ure::require(!reviewed_partition_journal.empty() && reviewed_partition_journal==value("ure_partition_journal") && !value("ure_partition_journal_hash").empty() &&
                ure::json(reviewed_partition_selection)==ure::json(partition_selection()),"review-required","Inspect this combined partition journal and the unchanged target first");
            const auto action=command.substr(14); auto target=gpt_target(system,action!="cancel");
            publish(ure::partition_job_recover(system,target,reviewed_partition_journal,action,value("ure_partition_journal_hash"))); clear_gpt_review();
        } else if(command.rfind("layout-",0)==0 && command!="layout-preview" && command!="layout-plan" && command!="layout-apply-image") {
            clear_gpt_review(); DataManager::SetValue("ure_layout_graph",""); DataManager::SetValue("ure_layout_review","Selections changed; calculate and review the layout again");
            if(command=="layout-mode-standard") {
                DataManager::SetValue("ure_layout_mode","standard"); DataManager::SetValue("ure_layout_placement","after_userdata");
                DataManager::SetValue("ure_layout_userdata_policy","preserve"); DataManager::SetValue("ure_layout_record_edits","[]");
                DataManager::SetValue("ure_layout_userdata_guid","");
            } else if(command=="layout-mode-advanced")DataManager::SetValue("ure_layout_mode","advanced");
            else if(command.rfind("layout-edit-",0)==0) {
                const auto role=command.substr(12); ure::require(role=="esp" || role=="linux" || role=="windows" || role=="userdata","invalid-layout-role","Select a supported layout role");
                DataManager::SetValue("ure_layout_edit_role",role);
                for(const auto* field:{"size","unit","filesystem","guid"})DataManager::SetValue("ure_layout_edit_"+std::string(field),value("ure_layout_"+role+"_"+field));
            } else if(command=="layout-row-save") {
                const auto role=value("ure_layout_edit_role"); ure::require(role=="esp" || role=="linux" || role=="windows" || role=="userdata","invalid-layout-role","Select a supported layout role");
                for(const auto* field:{"size","unit","filesystem","guid"})DataManager::SetValue("ure_layout_"+role+"_"+field,value("ure_layout_edit_"+std::string(field)));
            } else if(command=="layout-placement-after")DataManager::SetValue("ure_layout_placement","after_userdata");
            else if(command=="layout-placement-before" || command=="layout-userdata-recreate") {
                ure::require(value("ure_layout_mode")=="advanced","advanced-mode-required","Select advanced mode to erase and recreate userdata");
                DataManager::SetValue("ure_layout_userdata_policy","recreate");
                if(command=="layout-placement-before")DataManager::SetValue("ure_layout_placement","before_userdata");
            } else if(command=="layout-userdata-preserve") {
                DataManager::SetValue("ure_layout_userdata_policy","preserve"); DataManager::SetValue("ure_layout_placement","after_userdata");
            } else if(command=="layout-record-clear")DataManager::SetValue("ure_layout_record_edits","[]");
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
                const auto serialized=ure::json(edits); ure::require(serialized.size()<=65536,"size-limit","Advanced edits exceed their limit"); DataManager::SetValue("ure_layout_record_edits",serialized);
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
                DataManager::SetValue("ure_partition_journal",pending_gpt_journal); clear_gpt_review();
                DataManager::SetValue("ure_status","Image filesystems and GPT verified; original userdata and GPT remain available for complete rollback");
            } else {
                clear_gpt_review(); DataManager::SetValue("ure_layout_graph",""); auto target=gpt_target(system); const auto request=layout_request();
                ure::Value layout;
                if(command=="layout-plan") {
                    ure::require(target.identity["kind"]=="regular-image","live-write-unavailable","Live repartitioning awaits device firmware, ownership and Android encryption acceptance; use read-only preview");
                    ure::Root parent(value("ure_journal_parent")); pending_gpt_plan=ure::partition_job_plan(system,target,request,"global-os3.0.303.0");
                    layout=pending_gpt_plan["gpt"]["layout"]; reviewed_layout_request=request;
                    pending_gpt_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-layout-"+pending_gpt_plan["operation_id"].asString())).string();
                    DataManager::SetValue("ure_gpt_plan_hash",pending_gpt_plan["plan_sha256"].asString());
                    DataManager::SetValue("ure_gpt_can_execute",target.identity["kind"]=="regular-image" ? "1" : "0");
                    ure::save_json("/tmp/ure-layout-plan-"+pending_gpt_plan["operation_id"].asString()+".json",pending_gpt_plan);
                } else layout=ure::partition_layout(target,request,"global-os3.0.303.0",&system);
                // The widget needs only bounded allocation geometry, never unit identities.
                ure::Value graph; graph["format"]=layout["format"]; graph["pool"]=layout["pool"]; graph["rows"]=ure::Value(Json::arrayValue);
                for(const auto& row:layout["rows"]) { ure::Value part; for(const auto* field:{"role","pool_offset","bytes"})part[field]=row[field]; graph["rows"].append(part); }
                if(command=="layout-plan") { layout["warnings"]=pending_gpt_plan["warnings"];
                    DataManager::SetValue("ure_layout_review",ure::partition_layout_text(layout)+"\nComplete image job: filesystem preparation, userdata writes and GPT.\nRequired journal space: "+
                        std::to_string(pending_gpt_plan["estimated_journal_bytes"].asUInt64()/1048576)+" MiB\nJournal: "+pending_gpt_journal);
                    publish(pending_gpt_plan);
                } else { DataManager::SetValue("ure_layout_review",ure::partition_layout_text(layout)); publish(layout); }
                DataManager::SetValue("ure_layout_graph",ure::json(graph)); DataManager::SetValue("ure_status","Review original userdata bounds, filesystems, data loss, journal space and advanced edits before applying");
            }
        } else if(command.rfind("gpt-",0)==0) {
            if(command=="gpt-image" || command=="gpt-live") {
                clear_gpt_review(); DataManager::SetValue("ure_gpt_kind",command=="gpt-image" ? "image" : "live");
                DataManager::SetValue("ure_gpt_source","");
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
                DataManager::SetValue("ure_gpt_journal",pending_gpt_journal);
                clear_gpt_review(); DataManager::SetValue("ure_status","Image GPT verified; inspect its journal before rollback");
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
                        DataManager::SetValue("ure_stock_inputs",pending_gpt_plan["stock_inputs_directory"].asString());
                        DataManager::SetValue("ure_stock_identity_backup",pending_gpt_plan["backup_directory"].asString());
                    } else pending_gpt_plan=ure::gpt_plan(target,command=="gpt-repair-plan" ? "gpt.repair" : "gpt.restore","global-os3.0.303.0",
                        command=="gpt-restore-plan" ? ure::fs::path(value("ure_gpt_backup_dir")) : ure::fs::path(),&system);
                    pending_gpt_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-gpt-"+pending_gpt_plan["operation_id"].asString())).string();
                    const auto file="/tmp/ure-gpt-plan-"+pending_gpt_plan["operation_id"].asString()+".json"; ure::save_json(file,pending_gpt_plan);
                    auto review=pending_gpt_plan; review["journal_directory"]=pending_gpt_journal; review["plan_file"]=file; publish(review);
                    DataManager::SetValue("ure_gpt_plan_hash",pending_gpt_plan["plan_sha256"].asString());
                    DataManager::SetValue("ure_gpt_can_execute",target.identity["kind"]=="regular-image" ? "1" : "0");
                    DataManager::SetValue("ure_status",command=="gpt-stock-plan" ? "Review affected partitions and OS visibility; this restores metadata only" :
                        "Review both partition tables and journal destination; live writes remain gated");
                } else if(command=="gpt-journal-inspect") {
                    const auto review=ure::gpt_journal_inspect(target,value("ure_gpt_journal"),&system); publish(review);
                    reviewed_gpt_journal=value("ure_gpt_journal"); DataManager::SetValue("ure_gpt_journal_hash",review["plan_sha256"].asString());
                    for(const auto& action:review["recovery_actions"])DataManager::SetValue("ure_gpt_can_"+action.asString(),"1");
                } else throw ure::Error("unknown-action","Unknown GPT action");
            }
        } else if(command=="load") {
            editor.reset(); selected_root.reset(); pending_plan=ure::Value();
            DataManager::SetValue("ure_line",""); DataManager::SetValue("ure_preview","");
            DataManager::SetValue("ure_plan_hash",""); DataManager::SetValue("ure_line_number","");
            const auto path=value("ure_root");
            ure::require(!path.empty() && path.front()=='/' && path!="/","root-required","Select an already mounted OS root");
            auto root=std::make_unique<ure::Root>(path);
            auto loaded=std::make_unique<ure::Editor>(*root,value("ure_file"),"global-os3.0.303.0");
            for(const auto& row:loaded->lines())ure::require(row.size()<=8192,"line-too-long","GUI editor accepts lines up to 8192 bytes; use the bounded CLI backend for longer lines");
            selected_root=std::move(root); editor=std::move(loaded); current_line=0; refresh_editor();
            DataManager::SetValue("ure_status","Loaded; changes remain in memory until confirmed");
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
                DataManager::SetValue("ure_plan_hash",pending_plan["plan_sha256"].asString()); publish(review);
                DataManager::SetValue("ure_status","Review the file identity, backup and checksum before confirming"); return 0;
            } else if(command=="save") {
                ure::require(pending_plan.isObject(),"plan-required","Create and review a save plan first");
                const auto directory=pending_journal; ure::require(!directory.empty(),"journal-required","Review a journal destination before saving");
                publish(ure::transaction_run(*selected_root,pending_plan,directory,value("ure_plan_hash")));
                DataManager::SetValue("ure_journal",directory);
                DataManager::SetValue("ure_status","Saved and verified; rollback journal: "+directory);
                editor.reset(); pending_plan=ure::Value(); return 0;
            } else throw ure::Error("unknown-action","Unknown URE action");
            refresh_editor(); DataManager::SetValue("ure_status","Buffer updated; no file write performed");
        }
        return 0;
    } catch(const ure::Error& error) {
        if(command.rfind("scale-",0)==0)DataManager::SetValue("ure_scale_status",error.code+": "+error.what());
        DataManager::SetValue("ure_status",error.code+": "+error.what());
        ure::Value result; result["error"]["code"]=error.code; result["error"]["message"]=error.what(); publish(result); return 1;
    } catch(const std::exception&) { DataManager::SetValue("ure_status","Unexpected input or runtime failure"); return 1; }
}
