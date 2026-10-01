// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
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
