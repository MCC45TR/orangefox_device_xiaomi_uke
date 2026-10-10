// SPDX-License-Identifier: Apache-2.0
#include "../../src/device/xiaomi/uke/recoveryctl/libuke/ownership.cpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool test,const std::string& message) { if(!test)throw std::runtime_error(message); }
ure::Value object(const std::string& name,const std::string& number,bool partition=false) {
    ure::Value result; result["kernel_name"]=name; result["device_number"]=number; result["partition"]=partition;
    result["sysfs_path"]=std::string("sys/devices/mock/block/")+(partition ? "sda/" : "")+name;
    result["parent_lun_sysfs"]=partition ? "sys/devices/mock/block/sda" : result["sysfs_path"].asString();
    result["parent_lun_name"]=partition ? "sda" : name; result["stable_id"]="sysfs:"+result["sysfs_path"].asString();
    result["dependencies_available"]=true;
    for(const auto* field:{"slaves","holders","mounts"})result[field]=ure::Value(Json::arrayValue);
    return result;
}
ure::Value observations() {
    ure::Value result;
    for(const auto* name:{"mounts","swaps","processes","usb"})result["coverage"][name]=true;
    for(const auto* name:{"mounts","open_users","swaps","usb"})result[name]=ure::Value(Json::arrayValue);
    return result;
}
bool blocked(const ure::Value& result,const std::string& code) { for(const auto& item:result["blockers"])if(item["code"]==code)return true; return false; }
}
int main() {
    try {
        const std::set<gid_t> groups{0,3009};
        for(const auto* options:{"rw", "rw,hidepid=0", "hidepid=off", "rw,gid=3009,hidepid=invisible", "hidepid=2,gid=3009", "hidepid=noaccess,gid=0"})
            check(ure::proc_mount_visible(options,groups),"Measured proc visibility was refused");
        for(const auto* options:{"hidepid=2", "hidepid=2,gid=3010", "hidepid=ptraceable,gid=3009", "hidepid=invisible,gid=invalid", "hidepid=2,gid=3009,gid=3009", "hidepid=0,hidepid=2", "hidepid=unknown", "hidepid=2,gid=4294967296"})
            check(!ure::proc_mount_visible(options,groups),"Unproved proc visibility was admitted");
        check(ure::notification_usb_attributes({"state","power","subsystem","uevent"}),"Configfs notification class was refused");
        for(const auto& names:std::vector<std::vector<std::string>>{{}, {"power"}, {"state","functions"}, {"state","enable"}, {"state","lun0"}, {"state","unknown"}})
            check(!ure::notification_usb_attributes(names),"An unobserved legacy gadget control was accepted");
        check(ure::exited_process_state("42 (pigz) Z 1 0") && ure::exited_process_state("42 (name) with spaces) X 1 0"),"Exited kernel process state was missed");
        for(const auto* status:{"", "42 (pigz) S 1 0", "42 (pigz) R 1 0", "42 (pigz) Z", "42 (pigz)Z 1", "42 (pigz) Q 1 0"})
            check(!ure::exited_process_state(status),"Live or malformed process state was treated as exited");
        char pattern[]="/tmp/ure-mount-observation-XXXXXX"; const char* directory=::mkdtemp(pattern);
        check(directory!=nullptr,"Cannot create mount fixture");
        const ure::fs::path fixture(directory); ure::Root root(fixture);
        const std::string row="42 1 8:21 / /metadata ro,nosuid - f2fs /dev/block/metadata ro,norecovery\n";
        auto write_table=[&](const std::string& text) { auto file=root.open("mountinfo",O_WRONLY|O_CREAT|O_TRUNC,0600);
            check(::write(file.get(),text.data(),text.size())==static_cast<ssize_t>(text.size()),"Cannot write mount fixture"); };
        write_table(row); ure::Value observed; observed["mounts"]=ure::Value(Json::arrayValue); std::set<std::string> seen_mounts;
        for(unsigned process=0;process<256;++process)ure::mount_owners(root,"mountinfo",observed,std::to_string(process),seen_mounts);
        check(observed["mounts"].size()==1,"Identical per-process mount records consumed the ownership budget");
        write_table(row+"43 1 8:21 / /other rw - f2fs /dev/block/metadata rw\n");
        ure::mount_owners(root,"mountinfo",observed,"foreign",seen_mounts);
        check(observed["mounts"].size()==2 && observed["mounts"][1]["options"]=="rw","Distinct foreign mount was coalesced");
        write_table(row+"malformed\n");
        try { ure::mount_owners(root,"mountinfo",observed,"foreign",seen_mounts); throw std::runtime_error("Malformed repeated mount table accepted"); }
        catch(const ure::Error& error) { check(error.code=="invalid-usage","Unexpected mount-table refusal"); }
        ure::fs::remove_all(fixture);
        ure::Value graph; graph["objects"]=ure::Value(Json::arrayValue);
        graph["objects"].append(object("sda","8:0")); graph["objects"].append(object("sda1","8:1",true)); graph["objects"].append(object("sda2","8:2",true));
        const auto disk=graph["objects"][0]["stable_id"].asString(),partition=graph["objects"][1]["stable_id"].asString();
        auto seen=observations();
        auto result=ure::storage_usage_policy(graph,seen,partition);
        check(result["quiescent_observed"]==true && result["atomic_snapshot"]==false,"Clear observations were not classified correctly");
        graph["objects"][2]["mounts"].append("sibling mount");
        check(ure::storage_usage_policy(graph,seen,partition)["quiescent_observed"]==true,"Unrelated sibling mount blocked a partition backup");
        check(blocked(ure::storage_usage_policy(graph,seen,disk),"mounted"),"Whole-disk selection ignored a mounted child");
        graph["objects"][2]["mounts"].clear();
        auto mapper=object("dm-0","253:0"); mapper["slaves"].append("sda1"); graph["objects"].append(mapper);
        ure::Value mount; mount["device_number"]="253:0"; mount["path"]="/foreign namespace"; seen["mounts"].append(mount);
        check(blocked(ure::storage_usage_policy(graph,seen,partition),"mounts"),"Mapped mount in another namespace was missed");
        seen=observations(); ure::Value writer; writer["device_number"]="8:0"; writer["write_access"]=true; seen["open_users"].append(writer);
        check(blocked(ure::storage_usage_policy(graph,seen,partition),"open_users"),"A whole-LUN writer did not block a partition source");
        writer["write_access"]=false; seen["open_users"][0]=writer;
        check(ure::storage_usage_policy(graph,seen,partition)["quiescent_observed"]==true,"Read-only foreign users were misclassified as writers");
        for(const auto* category:{"swaps","usb"}) {
            seen=observations(); ure::Value owner; owner["device_number"]="8:1"; seen[category].append(owner);
            check(blocked(ure::storage_usage_policy(graph,seen,partition),category),"Active swap or USB storage was missed");
        }
        seen=observations(); seen["coverage"]["processes"]=false;
        check(blocked(ure::storage_usage_policy(graph,seen,partition),"usage-unavailable"),"Incomplete process coverage was accepted");
        seen=observations(); graph["objects"][1]["holders"].append("missing");
        check(blocked(ure::storage_usage_policy(graph,seen,partition),"unresolved-holder"),"Unknown kernel holder was accepted");
        graph["objects"][1]["holders"].clear(); graph["objects"][3]["slaves"].append("missing");
        check(blocked(ure::storage_usage_policy(graph,seen,partition),"unresolved-dependency"),"Unknown mapped dependency was accepted");
        graph["objects"][3]["slaves"].resize(1); graph["objects"][1]["dependencies_available"]=false;
        check(blocked(ure::storage_usage_policy(graph,seen,partition),"dependencies-unavailable"),"Missing sysfs dependency directories were accepted");
        graph["objects"][1]["dependencies_available"]=true;
        graph["objects"][1]["slaves"].append("dm-0");
        check(blocked(ure::storage_usage_policy(graph,seen,partition),"dependency-cycle-or-depth"),"Cyclic mapper dependencies were accepted");
        graph["objects"][1]["slaves"].clear();
        for(unsigned i=0;i<256;++i) { ure::Value owner; owner["device_number"]="8:1"; seen["swaps"].append(owner); }
        result=ure::storage_usage_policy(graph,seen,partition);
        check(result["quiescent_observed"]==false && result["blockers_truncated"]==true && result["blockers"].size()==128,"Bounded output lost a blocking condition");
        graph["objects"].append(graph["objects"][1]);
        try { ure::storage_usage_policy(graph,seen,partition); throw std::runtime_error("Duplicate identity accepted"); }
        catch(const ure::Error& error) { check(error.code=="ambiguous-identity","Unexpected duplicate rejection"); }
        std::cout<<"Storage usage policy: parent/child and mapper propagation, sibling isolation, namespace mounts, foreign writers, swap/USB, incomplete evidence, unknown holders/dependencies, cycles, bounds and identity ambiguity passed; synthetic observations only\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
