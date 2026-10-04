// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <charconv>
#include <fcntl.h>
#include <map>
#include <set>

namespace ure {
namespace {
class Options {
    std::map<std::string,std::string> values_;
public:
    std::vector<std::string> words;
    explicit Options(std::vector<std::string> input) {
        bool json_flag=false;
        for(std::size_t i=0;i<input.size();++i) {
            if(input[i]=="--json") { require(!json_flag,"invalid-options","Duplicate JSON flag"); json_flag=true; continue; }
            if(!input[i].starts_with("--")) { words.push_back(input[i]); continue; }
            const auto key=input[i]; require(i+1<input.size() && !input[i+1].starts_with("--") && !values_.contains(key),"invalid-options","Duplicate or incomplete management option");
            values_[key]=input[++i];
        }
    }
    bool has(const std::string& key)const { return values_.contains(key); }
    std::string get(const std::string& key,const std::string& fallback={})const { const auto it=values_.find(key); return it==values_.end() ? fallback : it->second; }
    std::string need(const std::string& key)const { require(has(key),"invalid-options","Required management option is absent: "+key); return get(key); }
    void allow(std::initializer_list<const char*> keys)const { std::set<std::string> allowed; for(const auto* key:keys)allowed.insert(key);
        for(const auto& [key,value]:values_) { (void)value; require(allowed.contains(key),"invalid-options","Option is unrelated to this management operation: "+key); } }
};
StorageTarget target(const Options& options,const Root& system,bool writable=false) {
    require(options.has("--image")!=options.has("--object"),"invalid-options","Select one filesystem image or stable live storage identity");
    const auto sector=options.get("--sector-size","512"); require(sector=="512" || sector=="4096","invalid-sector","Select 512 or 4096-byte sectors");
    if(options.has("--image"))return storage_image(options.get("--image"),sector=="512" ? 512U : 4096U,writable);
    require(!options.has("--sector-size"),"invalid-options","Live sectors come from the kernel");
    require(!writable,"firmware-unverified","The native Uke live filesystem writer is not accepted; review the detailed storage preflight blockers");
    return storage_select(system,options.get("--object"),true);
}
void positional(const Options& options,std::size_t count) { require(options.words.size()==count,"invalid-options","Unexpected positional management arguments"); }
} // namespace
bool management_command(const std::vector<std::string>& args) {
    if(args.size()<2)return false;
    const auto& command=args[0]; const auto& op=args[1];
    if(command=="installer")return op.starts_with("image-");
    if(command=="filesystem")return op=="capabilities" || op=="plan" || op=="execute" || op=="inspect-journal" || op=="resume" || op=="rollback" || op=="cancel";
    if(command=="partition")return op=="job-plan" || op=="job-execute" || op=="job-inspect" || op=="job-resume" || op=="job-rollback" || op=="job-cancel";
    if(command=="stock")return op=="image-inspect" || op=="job-plan" || op=="job-execute" || op=="job-inspect" || op=="job-resume" || op=="job-rollback" || op=="job-cancel";
    if(command=="storage")return op=="preflight";
    if(command=="boot")return op.starts_with("route-");
    if(command=="linux")return op=="audit" || op=="rescue-plan" || op=="rescue-execute" || op=="rescue-inspect";
    if(command=="btrfs")return op=="info" || op=="subvolumes" || op=="usage" || op=="device-stats" || op=="scrub-status" || op=="balance-status" || op=="plan" || op=="execute" ||
        op=="subvolume-info" || op=="snapshot-plan" || op=="snapshot-execute" || op=="send-plan" || op=="send-capture" || op=="send-verify" || op=="backup-inspect" || op=="stream-check";
    return false;
}
Value management_dispatch(std::vector<std::string> args) {
    Options options(std::move(args)); const auto& words=options.words; require(words.size()>=2,"invalid-options","A management operation is required");
    const auto command=words[0],operation=words[1];
    if(command=="installer") {
        positional(options,3);
        const auto sector=options.get("--sector-size","4096");
        require(sector=="512" || sector=="4096","invalid-sector","Select 512 or 4096-byte sectors");
        const auto width=sector=="512" ? 512U : 4096U;
        Root system(options.get("--system-root","/"));
        if(operation=="image-prepare") {
            options.allow({"--system-root","--image","--fallback-image","--sector-size","--root","--content-file","--backup","--output"});
            auto selected=storage_image(options.need("--image"),width),fallback=storage_image(options.need("--fallback-image"),width);
            Root staged(options.need("--root"));
            auto plan=recovery_install_prepare(system,selected,fallback,staged,options.need("--content-file"),json_file(words[2]),options.need("--backup"));
            save_json(options.need("--output"),plan); return plan;
        }
        require(operation=="image-execute" || operation=="image-inspect" || operation=="image-resume" ||
            operation=="image-rollback" || operation=="image-cancel","invalid-action","Unknown installer image command");
        if(operation=="image-execute")options.allow({"--system-root","--image","--fallback-image","--sector-size","--journal","--confirm"});
        else options.allow({"--system-root","--image","--fallback-image","--sector-size","--confirm"});
        auto selected=storage_image(options.need("--image"),width,operation=="image-execute" || operation=="image-resume" || operation=="image-rollback");
        auto fallback=storage_image(options.need("--fallback-image"),width);
        if(operation=="image-execute")return recovery_install_execute(system,selected,fallback,json_file(words[2]),options.need("--journal"),options.need("--confirm"));
        require(operation!="image-inspect" || !options.has("--confirm"),"invalid-options","Installer inspection does not accept confirmation");
        return recovery_install_recover(system,selected,fallback,words[2],operation.substr(6),operation=="image-inspect" ? "" : options.need("--confirm"));
    }
    if(command=="boot") {
        if(operation=="route-history") { positional(options,3); options.allow({}); return boot_route_history(words[2]); }
        require(operation=="route-inventory" || operation=="route-plan" || operation=="route-execute" || operation=="route-inspect" ||
            operation=="route-recover" || operation=="route-consume-fixture" || operation=="route-ack-fixture" || operation=="route-cancel" ||
            operation=="route-fallback-fixture","invalid-boot-action","Unknown boot management command");
        Root esp(options.need("--esp")),variables(options.need("--variables"));
        if(operation=="route-inventory") { positional(options,2); options.allow({"--esp","--variables"}); return boot_route_inventory(esp,variables); }
        positional(options,3);
        if(operation=="route-plan") { options.allow({"--esp","--variables","--output"}); const auto plan=boot_route_plan(esp,variables,json_file(words[2])); save_json(options.need("--output"),plan); return plan; }
        if(operation=="route-execute") { options.allow({"--esp","--variables","--journal","--confirm"}); return boot_route_execute(esp,variables,json_file(words[2]),options.need("--journal"),options.need("--confirm")); }
        const auto action=operation.substr(6);
        if(action=="inspect") { options.allow({"--esp","--variables"}); return boot_route_action(esp,variables,words[2],action); }
        if(action=="ack-fixture") { options.allow({"--esp","--variables","--confirm","--receipt"}); return boot_route_action(esp,variables,words[2],action,options.need("--confirm"),json_file(options.need("--receipt"))); }
        options.allow({"--esp","--variables","--confirm"}); return boot_route_action(esp,variables,words[2],action,options.need("--confirm"));
    }
    if(command=="filesystem" && operation=="capabilities") { positional(options,2); options.allow({}); return filesystem_capabilities(); }
    if(command=="stock") {
        positional(options,3);
        if(operation=="image-inspect") {
            options.allow({}); const auto path=fs::absolute(words[2]).lexically_normal(); Root parent(path.parent_path());
            auto file=parent.open(path.filename().string(),O_RDONLY|O_NONBLOCK); return stock_image_inspect(file.get());
        }
        if(operation=="job-plan") { options.allow({"--output"}); const auto plan=stock_job_plan(json_file(words[2])); save_json(options.need("--output"),plan); return plan; }
        if(operation=="job-execute") { options.allow({"--journal","--confirm"}); return stock_job_execute(json_file(words[2]),options.need("--journal"),options.need("--confirm")); }
        options.allow({"--confirm"}); const auto action=operation.substr(4);
        require(action!="inspect" || !options.has("--confirm"),"invalid-options","Read-only stock journal inspection does not accept confirmation");
        return stock_job_recover(words[2],action,action=="inspect" ? "" : options.need("--confirm"));
    }
    if(command=="partition") {
        positional(options,3); Root system(options.get("--system-root","/"));
        if(operation=="job-plan") { options.allow({"--system-root","--image","--object","--sector-size","--profile","--output"}); auto selected=target(options,system);
            const auto plan=partition_job_plan(system,selected,json_file(words[2]),options.need("--profile")); save_json(options.need("--output"),plan); return plan; }
        if(operation=="job-execute") { options.allow({"--system-root","--image","--object","--sector-size","--journal","--confirm"}); auto selected=target(options,system,true);
            return partition_job_execute(system,selected,json_file(words[2]),options.need("--journal"),options.need("--confirm")); }
        options.allow({"--system-root","--image","--object","--sector-size","--confirm"}); const auto action=operation.substr(4);
        require(action!="inspect" || !options.has("--confirm"),"invalid-options","Read-only partition journal inspection does not accept confirmation");
        auto selected=target(options,system,action=="resume" || action=="rollback");
        return partition_job_recover(system,selected,words[2],action,action=="inspect" ? "" : options.need("--confirm"));
    }
    if(command=="filesystem" || command=="storage") {
        Root system(options.get("--system-root","/")); const bool write=operation=="execute" || operation=="resume" || operation=="rollback";
        if(operation=="preflight") { positional(options,2); options.allow({"--system-root","--image","--object","--sector-size","--profile"}); auto selected=target(options,system); return storage_preflight(system,selected,options.need("--profile")); }
        positional(options,3);
        if(operation=="plan") { options.allow({"--system-root","--image","--object","--sector-size","--profile","--output"}); auto selected=target(options,system);
            const auto plan=filesystem_operation_plan(system,selected,json_file(words[2]),options.need("--profile")); save_json(options.need("--output"),plan); return plan; }
        if(operation=="execute") { options.allow({"--system-root","--image","--object","--sector-size","--journal","--confirm"}); auto selected=target(options,system,write);
            return filesystem_operation_execute(system,selected,json_file(words[2]),options.need("--journal"),options.need("--confirm")); }
        options.allow({"--system-root","--image","--object","--sector-size","--confirm"});
        require(operation!="inspect-journal" || !options.has("--confirm"),"invalid-options","Journal inspection does not accept confirmation"); auto selected=target(options,system,write);
        return filesystem_operation_recover(system,selected,words[2],operation=="inspect-journal" ? "inspect" : operation,operation=="inspect-journal" ? "" : options.need("--confirm"));
    }
    if(command=="linux") {
        if(operation=="rescue-inspect") { positional(options,3); options.allow({}); auto store=private_directory(words[2],false); Value out;
            out["plan"]=parse_json(store.read("plan.json")); out["state"]=parse_json(store.read("state.json")); return out; }
        require(options.has("--root"),"root-required","Select an already mounted Linux root");
        Root root(options.get("--root")); std::unique_ptr<Root> esp; if(options.has("--esp"))esp=std::make_unique<Root>(options.get("--esp"));
        if(operation=="audit") { positional(options,2); options.allow({"--root","--esp","--output"}); auto result=linux_boot_audit(root,esp.get()); if(options.has("--output"))save_json(options.get("--output"),result); return result; }
        positional(options,3);
        if(operation=="rescue-plan") { options.allow({"--root","--esp","--output"}); const auto plan=linux_rescue_plan(root,json_file(words[2]),esp.get()); save_json(options.need("--output"),plan); return plan; }
        options.allow({"--root","--esp","--journal","--confirm"}); return linux_rescue_execute(root,json_file(words[2]),options.need("--journal"),options.need("--confirm"),esp.get());
    }
    if(operation=="stream-check") { positional(options,3); options.allow({}); const auto directory=fs::path(words[2]).parent_path(); Root parent(directory.empty() ? fs::path(".") : directory); auto file=parent.open(fs::path(words[2]).filename().string(),O_RDONLY|O_NONBLOCK); return btrfs_stream_check(file.get()); }
    if(operation=="send-verify" || operation=="backup-inspect") { positional(options,3); options.allow({}); return operation=="send-verify" ? btrfs_send_verify(words[2]) : btrfs_backup_inspect(words[2]); }
    require(options.has("--root"),"root-required","Select an already mounted Btrfs root"); Root root(options.get("--root"));
    if(operation=="plan") { positional(options,3); options.allow({"--root","--profile","--output"}); const auto plan=btrfs_manage_plan(root,json_file(words[2]),options.need("--profile")); save_json(options.need("--output"),plan); return plan; }
    if(operation=="execute") { positional(options,3); options.allow({"--root","--journal","--confirm"}); return btrfs_manage_execute(root,json_file(words[2]),options.need("--journal"),options.need("--confirm")); }
    if(operation=="snapshot-plan") { positional(options,5); options.allow({"--root","--profile","--output"}); return btrfs_snapshot_plan(root,words[2],words[3],words[4],options.need("--profile"),options.need("--output")); }
    if(operation=="snapshot-execute" || operation=="send-capture") { positional(options,3); options.allow({"--root","--confirm"}); return operation=="snapshot-execute" ? btrfs_snapshot_execute(root,words[2],options.need("--confirm")) : btrfs_send_capture(root,words[2],options.need("--confirm")); }
    if(operation=="send-plan") { require(words.size()==3 || words.size()==4,"invalid-options","Send planning selects a source snapshot and optional incremental parent"); options.allow({"--root","--profile","--output"});
        return btrfs_send_plan(root,words[2],words.size()==4 ? words[3] : "",options.need("--profile"),options.need("--output")); }
    if(operation=="subvolume-info") { positional(options,3); options.allow({"--root"}); return btrfs_subvolume_info(root,words[2]); }
    positional(options,2); options.allow({"--root"}); return btrfs_native_info(root,operation);
}
} // namespace ure
