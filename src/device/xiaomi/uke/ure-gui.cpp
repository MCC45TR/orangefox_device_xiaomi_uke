// SPDX-License-Identifier: GPL-3.0-or-later
// Project-owned OrangeFox adapter. All storage operations use libuke directly.
#include "objects.hpp"
#include "../data.hpp"
#include "../gui.hpp"
#include "uke.h"
#include <algorithm>
#include <mutex>

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
ure::Value pending_restore_plan;
std::string pending_restore_journal,pending_restore_backup,reviewed_restore_journal;
std::string reviewed_stream_journal;
std::size_t current_line=0;
std::string value(const std::string& name) { std::string result; DataManager::GetValue(name,result); return result; }
void publish(const ure::Value& data) { DataManager::SetValue("ure_output",ure::json(data)); }
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
    for(const auto* name:{"ure_gpt_plan_hash","ure_gpt_journal_hash","ure_gpt_can_execute","ure_gpt_can_rollback","ure_gpt_can_resume"})DataManager::SetValue(name,"");
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
int GUIAction::uremanager(std::string command) {
    std::lock_guard<std::mutex> guard(session_mutex);
    try {
        ure::Root system("/");
        if(command=="capabilities")publish(ure::capabilities(system));
        else if(command=="storage")publish(ure::storage_graph(system));
        else if(command=="diagnose")publish(ure::diagnose(system,"all"));
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
        } else if(command.rfind("gpt-",0)==0) {
            if(command=="gpt-image" || command=="gpt-live") {
                clear_gpt_review(); DataManager::SetValue("ure_gpt_kind",command=="gpt-image" ? "image" : "live");
                DataManager::SetValue("ure_gpt_source","");
            } else if(command=="gpt-verify")publish(ure::gpt_backup_verify(value("ure_gpt_backup_dir")));
            else if(command=="gpt-execute") {
                ure::require(pending_gpt_plan.isObject() && !pending_gpt_journal.empty() &&
                    pending_gpt_plan["plan_sha256"].asString()==value("ure_gpt_plan_hash"),"plan-required","Create and review a GPT plan first");
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
                if(command=="gpt-repair-plan" || command=="gpt-restore-plan" || command=="gpt-journal-inspect")clear_gpt_review();
                auto target=gpt_target(system);
                if(command=="gpt-inspect") {
                    ure::Value result; result["identity"]=target.identity;
                    result["table"]=ure::gpt_inspect(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt()); publish(result);
                } else if(command=="gpt-backup")publish(ure::gpt_backup(target,value("ure_gpt_backup_dir"),"global-os3.0.303.0",&system));
                else if(command=="gpt-compare")publish(ure::gpt_compare(target,value("ure_gpt_backup_dir"),"global-os3.0.303.0",&system));
                else if(command=="gpt-repair-plan" || command=="gpt-restore-plan") {
                    ure::Root parent(value("ure_journal_parent"));
                    pending_gpt_plan=ure::gpt_plan(target,command=="gpt-repair-plan" ? "gpt.repair" : "gpt.restore","global-os3.0.303.0",
                        command=="gpt-restore-plan" ? ure::fs::path(value("ure_gpt_backup_dir")) : ure::fs::path(),&system);
                    pending_gpt_journal=(ure::fs::path(value("ure_journal_parent"))/("ure-gpt-"+pending_gpt_plan["operation_id"].asString())).string();
                    const auto file="/tmp/ure-gpt-plan-"+pending_gpt_plan["operation_id"].asString()+".json"; ure::save_json(file,pending_gpt_plan);
                    auto review=pending_gpt_plan; review["journal_directory"]=pending_gpt_journal; review["plan_file"]=file; publish(review);
                    DataManager::SetValue("ure_gpt_plan_hash",pending_gpt_plan["plan_sha256"].asString());
                    DataManager::SetValue("ure_gpt_can_execute",target.identity["kind"]=="regular-image" ? "1" : "0");
                    DataManager::SetValue("ure_status","Review both partition tables and journal destination; live writes remain gated");
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
        DataManager::SetValue("ure_status",error.code+": "+error.what());
        ure::Value result; result["error"]["code"]=error.code; result["error"]["message"]=error.what(); publish(result); return 1;
    } catch(const std::exception&) { DataManager::SetValue("ure_status","Unexpected input or runtime failure"); return 1; }
}
