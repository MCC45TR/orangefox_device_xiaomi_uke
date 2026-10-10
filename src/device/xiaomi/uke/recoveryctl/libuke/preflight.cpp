// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "boot_state.hpp"
#include "recovery_write_policy.hpp"
#include "../install_policy.h"
#include <algorithm>
#include <array>
#include <fcntl.h>
#include <sys/statfs.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif
namespace ure {
namespace {
std::string property(const char* key) {
#ifdef __ANDROID__
    std::array<char,PROP_VALUE_MAX> value{}; __system_property_get(key,value.data()); return value.data();
#else
    (void)key; return {};
#endif
}
std::string trim(std::string value) { while(!value.empty() && (value.back()=='\n' || value.back()=='\r'))value.pop_back(); return value; }
void check(Value& out,const std::string& name,bool passed,const std::string& reason) {
    Value item; item["check"]=name; item["passed"]=passed; item["reason"]=reason; out["checks"].append(item);
    if(!passed)out["blockers"].append(name);
}
Value boot_query(const std::string& command,const std::vector<std::string>& args={}) {
    Value out; try { std::vector<std::string> words{command}; words.insert(words.end(),args.begin(),args.end()); const auto process=run_tool("bootctl",words,15);
        out["successful"]=process.status==0 && !process.timed_out; out["value"]=trim(process.output); out["exit_status"]=process.status;
    } catch(const Error& error) { out["successful"]=false; out["error_code"]=error.code; } return out;
}
} // namespace
Value storage_preflight(const Root& system,const StorageTarget& target,const std::string& profile) {
    require(identifier(profile),"invalid-profile","An explicit firmware profile is required"); storage_revalidate(target,&system);
    Value out; out["schema"]=1; out["format"]="ure-storage-preflight"; out["target_identity"]=target.identity;
    out["firmware_profile"]=profile; out["checks"]=Value(Json::arrayValue); out["blockers"]=Value(Json::arrayValue);
    out["private_record"]=true; out["physical_test_record"]=false; out["atomic_snapshot"]=false; out["android_fbe_access_authorized"]=false;
    out["native_live_writer_ready"]=live_storage_backend_accepted(); out["firmware_identity_validated"]=false;
    out["device_profile"]=device_profile_admission_status(system,profile);
    out["platform_admission"]=platform_capabilities(system,profile);
    out["model_sku_geometry_validated"]=false;
    if(target.identity["kind"]=="regular-image") {
        check(out,"regular-file-identity",true,"Selected image inode, geometry and metadata were revalidated");
        check(out,"physical-storage-write",false,"Image fixtures do not prove live firmware, slot, snapshot or storage ownership");
        out["image_job_eligible"]=true; out["live_job_eligible"]=false; out["signature"]=filesystem_probe(target.descriptor.get()); return out;
    }
    Root host("/"); const bool authoritative=descriptor_identity(host.fd())==descriptor_identity(system.fd());
    check(out,"current-system-root",authoritative,"Live evidence must come from the current kernel and recovery root, never a supplied JSON or synthetic proc tree");
    check(out,"unit-lun-boot-identity",target.identity["unit_identity_available"]==true && hash_valid(target.identity["unit_identity_sha256"].asString()) &&
        hash_valid(target.identity["boot_id_sha256"].asString()) && target.identity["boot_id_sha256"].asString()!=sha256(""),"A unique unit, LUN and boot identity is required");
    check(out,"exclusive-kernel-claim",target.exclusive_claim,"Kernel ownership must be retained throughout the job");
    check(out,"writable-media",target.identity["read_only_state"]==false,"The kernel must report writable media");
    const auto label=target.identity["label"].asString();
    check(out,"target-owner",target.identity["partition"]==true && (label=="uke_linux" || label=="uke_windows" || label=="uke_esp" || label=="uke_home"),
        "Standard filesystem jobs are restricted to Linux, Windows, home or ESP partitions; Android data and firmware require separate trust and restore workflows");
    check(out,"accepted-model-sku-capacity-profile",out["device_profile"]["profile_accepted"]==true,
        "Only an accepted commercial model/SKU/capacity, exact installed firmware and this unit's six-LUN geometry/GUID backup may authorize live planning");
    check(out,"platform-feature-and-health-admission",out["platform_admission"]["live_action_allowed"]==true,
        "Platform trust, slot/snapshot lifecycle, persistent ownership, fallback and exact-unit battery/thermal/UFS policies must all be accepted");
    // Do not hash gigabytes of an unaccepted unit's boot media or mistake
    // current recovery properties for its installed Android firmware.
    if(out["device_profile"]["profile_accepted"]!=true) {
        check(out,"exact-stock-boot-stack",false,"Whole-partition boot verification is unavailable until unit-profile admission; source package pins are not installed identity");
        check(out,"accepted-live-transaction-backend",false,"The Uke live writer remains unaccepted");
        out["image_job_eligible"]=false; out["live_job_eligible"]=false; return out;
    }
    if(authoritative) {
        out["ownership"]=storage_usage(system,target.identity["stable_id"].asString());
        check(out,"mount-process-swap-usb-ownership",out["ownership"]["quiescent_observed"]==true,"Every visible namespace, holder, swap and gadget export must be resolved and idle");
        const auto device=property("ro.product.device"),suffix=property("ro.boot.slot_suffix");
        out["observed_device"]=device; out["observed_model"]=property("ro.product.model");
        check(out,"uke-device",device=="uke","Pad 7 and POCO Pad X1 share the Uke target; the actual SKU and firmware still need separate matching evidence");
        check(out,"unlocked-bootloader",bootloader_unlocked(system),"Current kernel and recovery properties must consistently identify unlocked Uke");
        const auto slots=boot_query("get-number-slots"),current=boot_query("get-current-slot"),merge=boot_query("get-snapshot-merge-status");
        out["boot_control"]["slots"]=slots; out["boot_control"]["current"]=current; out["boot_control"]["merge"]=merge;
        check(out,"two-ab-slots",slots["successful"]==true && slots["value"]=="2","Authoritative boot-control HAL must report exactly two slots");
        const auto active=current["value"].asString();
        check(out,"consistent-active-slot",current["successful"]==true && ((active=="0" && suffix=="_a") || (active=="1" && suffix=="_b")),"HAL slot and installed boot properties must agree");
        check(out,"no-virtual-ab-update",merge["successful"]==true && merge["value"]=="none","Unknown, snapshotted, merging and cancelled update states block writes");
        const auto fallback=boot_query("is-slot-bootable",{active=="0" ? "1" : "0"}); out["boot_control"]["fallback"]=fallback;
        check(out,"bootable-fallback",fallback["successful"]==true,"Keep an independently bootable inactive stock slot");
        bool stack=profile=="global-os3.0.303.0"; Value images(Json::arrayValue); const auto graph=storage_graph(system);
        if(stack)for(const auto& image:uke::global_stock)for(const auto* slot:{"_a","_b"}) {
            if(std::string(image.name)=="recovery" && suffix==slot)continue; // The active custom recovery is expected.
            Value result; result["label"]=std::string(image.name)+slot; bool found=false;
            result["source_bytes"]=Json::UInt64(image.source_bytes); result["partition_bytes"]=Json::UInt64(image.partition_bytes);
            result["source_sha256"]=image.source_sha256; result["expected_partition_sha256"]=image.partition_sha256;
            result["programming_layout"]=image.programming_layout; result["checksum_scope"]="whole-partition";
            try {
                for(const auto& object:graph["objects"])if(object["label"]==result["label"]) {
                    require(!found,"ambiguous-firmware","Duplicate stock boot-stack label"); found=true; auto selected=storage_select(system,object["stable_id"].asString());
                    const auto hash=sha256(selected.descriptor.get()); result["matching"]=selected.identity["bytes"].asUInt64()==image.partition_bytes && hash==image.partition_sha256;
                    result["sha256"]=hash; storage_revalidate(selected,&system);
                }
            } catch(const Error& error) { result["error_code"]=error.code; result["matching"]=false; }
            stack=stack && found && result["matching"]==true; images.append(result);
        }
        out["stock_boot_stack"]=images; out["firmware_identity_validated"]=stack;
        check(out,"exact-stock-boot-stack",stack,"Both boot slots and inactive recovery must match the reviewed whole-partition Global programming layouts, including DTBO's zero gap and duplicated end footer; this is not model/SKU acceptance");
    }
    check(out,"accepted-live-transaction-backend",live_storage_backend_accepted(),"Live writes remain disabled: Uke/SKU-specific range provenance and the complete native write adapter are not accepted yet");
    out["image_job_eligible"]=false; out["live_job_eligible"]=out["blockers"].empty(); return out;
}
} // namespace ure
