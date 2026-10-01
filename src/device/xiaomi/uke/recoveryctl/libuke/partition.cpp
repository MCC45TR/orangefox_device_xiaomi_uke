// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <algorithm>
#include <map>
#include <utility>

namespace ure {
namespace {
constexpr unsigned probe_limit=128;
std::string owner_hint(const std::string& label) {
    if(label=="uke_linux")return "LINUX_ROOT";
    if(label=="uke_windows")return "WINDOWS_ROOT";
    if(label=="uke_esp")return "ESP_SHARED";
    if(label=="userdata" || label=="metadata")return "ANDROID_DATA";
    if(label.starts_with("recovery_"))return "RECOVERY";
    if(label=="super" || label.starts_with("boot_") || label.starts_with("init_boot_") || label.starts_with("vendor_boot_"))return "ANDROID_SYSTEM";
    return "UNKNOWN";
}
Value metadata_hashes(const StorageTarget& target) {
    Value hashes(Json::arrayValue);
    for(const auto& range:gpt_regions(target.descriptor.get(),target.identity["logical_sector_bytes"].asUInt())) {
        Value row; row["name"]=range.name; row["offset"]=Json::UInt64(range.offset); row["bytes"]=Json::UInt64(range.bytes.size()); row["sha256"]=sha256(range.bytes); hashes.append(row);
    }
    return hashes;
}
Value extent(std::uint64_t first,std::uint64_t last,std::uint32_t sector) {
    Value range; range["start_lba"]=Json::UInt64(first); range["end_lba"]=Json::UInt64(last);
    range["offset"]=Json::UInt64(first*sector); range["bytes"]=Json::UInt64((last-first+1)*sector); return range;
}
}
Value partition_map(const StorageTarget& target,const Root* system) {
    require(target.identity["kind"]=="regular-image" || target.identity["partition"]==false,"invalid-target","A partition map selects a whole disk/LUN");
    storage_revalidate(target,system); const auto sector=target.identity["logical_sector_bytes"].asUInt();
    const auto table=gpt_inspect(target.descriptor.get(),sector); const bool healthy=table["healthy"]==true;
    const auto before=healthy ? metadata_hashes(target) : Value();
    Value result; result["schema"]=1; result["format"]="ure-partition-map"; result["target_identity"]=target.identity; result["gpt"]=table;
    result["partitions"]=Value(Json::arrayValue); result["unallocated_ranges"]=Value(Json::arrayValue); result["reserved_records"]=table["reserved_records"];
    result["read_only"]=true; result["private_record"]=true; result["physical_test_record"]=false;
    result["ownership_basis"]="LABEL_HINT_ONLY_NOT_INSTALLED_OS_PROOF"; result["android_fbe_access_authorized"]=false;
    result["unallocated_ranges_available"]=healthy; result["signature_probe_limit"]=probe_limit;
    result["signature_probe_bytes_per_partition_max"]=8192; result["signature_samples_are_atomic_snapshot"]=false;
    result["management_eligible"]=false; result["warnings"]=Value(Json::arrayValue);
    if(!healthy)result["warnings"].append("GPT is unhealthy or ambiguous; no unallocated-space or signature inference is available");
    unsigned probed=0;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> used;
    for(const auto& part:table["partitions"]) {
        auto row=part; const auto first=part["start_lba"].asUInt64(),last=part["end_lba"].asUInt64();
        row["offset"]=Json::UInt64(first*sector); row["owner_hint"]=owner_hint(part["label"].asString());
        row["ownership_verified"]=false; row["write_policy"]="PLAN_AND_OWNERSHIP_PROOF_REQUIRED";
        if(row["owner_hint"]=="UNKNOWN" || row["owner_hint"]=="ANDROID_DATA" || row["owner_hint"]=="ANDROID_SYSTEM" || row["owner_hint"]=="RECOVERY")row["write_policy"]="PROTECTED_OR_UNCLASSIFIED";
        if(healthy && probed<probe_limit) {
            row["content"]=filesystem_probe_range(target.descriptor.get(),first*sector,(last-first+1)*sector); ++probed;
            row["content"]["observation_state"]="SIGNATURE_SAMPLE_ONLY";
            row["content"]["container_encryption_observation"]=row["content"]["encryption"]=="none" ? "NO_CONTAINER_SIGNATURE_DETECTED" : "CONTAINER_SIGNATURE_DETECTED";
            if(row["owner_hint"]=="ANDROID_DATA")row["content"]["android_fbe_trust"]="UNVERIFIED";
        } else row["content"]["observation_state"]=healthy ? "NOT_PROBED_LIMIT" : "NOT_PROBED_UNHEALTHY_GPT";
        result["partitions"].append(row); used.emplace_back(first,last);
    }
    result["signature_samples"]=probed; result["signature_samples_truncated"]=healthy && probed<table["partitions"].size();
    for(const auto& reservation:table["reserved_records"])used.emplace_back(reservation["start_lba"].asUInt64(),reservation["end_lba"].asUInt64());
    if(healthy) {
        const auto first=table["primary"]["first_usable_lba"].asUInt64(),last=table["primary"]["last_usable_lba"].asUInt64();
        std::sort(used.begin(),used.end()); auto cursor=first; std::uint64_t unallocated=0;
        auto gap=[&](std::uint64_t begin,std::uint64_t end) {
            auto range=extent(begin,end,sector); range["allocation_state"]="UNALLOCATED_IN_GPT_NOT_WRITE_PERMISSION";
            const std::uint64_t alignment=(1024*1024)/sector; const auto aligned=(begin+alignment-1)/alignment*alignment;
            range["aligned_1mib_start_lba"]=aligned<=end ? Value(Json::UInt64(aligned)) : Value();
            unallocated+=(end-begin+1)*sector; result["unallocated_ranges"].append(range);
        };
        for(const auto& [begin,end]:used) { if(cursor<begin)gap(cursor,begin-1); cursor=end+1; }
        if(cursor<=last)gap(cursor,last);
        result["unallocated_bytes"]=Json::UInt64(unallocated);
        require(json(metadata_hashes(target))==json(before),"stale-device","GPT changed during partition-map observation");
    } else result["unallocated_bytes"]=Value();
    storage_revalidate(target,system); return result;
}
} // namespace ure
