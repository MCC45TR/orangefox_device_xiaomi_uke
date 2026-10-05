// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "recovery_write_policy.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <map>
#include <set>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace ure {
namespace {
struct Feature {
    const char* id;
    const char* name;
    const char* backend;
    std::vector<const char*> requirements;
};
// A check describes a required evidence class, not a configurable allow flag.
// No platform backend or commercial unit profile is accepted in this release.
static_assert(!live_storage_backend_accepted());
const std::vector<Feature>& features() {
    static const std::vector<Feature> items{
        {"android-fbe", "Android encrypted data", "unaccepted-keymint-tee-backend",
            {"installed-avb-trust", "keymint-tee-chain", "metadata-key-origin", "fbe-user-key-isolation", "fscrypt-kernel-service-closure"}},
        {"android-slots", "Android A/B slot changes", "unaccepted-boot-control-writer",
            {"whole-ab-boot-stacks", "boot-control-hal-closure", "persistent-operation-owner", "forced-restart-recovery", "stock-return-fallback"}},
        {"android-snapshots", "Virtual A/B snapshot transitions", "unaccepted-snapshot-coordinator",
            {"snapshot-metadata-cow-closure", "snapuserd-dm-user-closure", "boot-control-hal-closure", "persistent-operation-owner", "forced-restart-recovery"}},
        {"android-super", "Logical partitions and super", "unaccepted-logical-partition-writer",
            {"super-both-metadata-copies", "super-extent-group-bounds", "snapshot-metadata-cow-closure", "persistent-operation-owner", "forced-restart-recovery"}},
        {"android-ota", "Android OTA installation", "unaccepted-update-engine-backend",
            {"ota-authenticated-payload", "ota-version-rollback-policy", "installed-avb-trust", "whole-ab-boot-stacks", "super-both-metadata-copies", "super-extent-group-bounds", "snapshot-metadata-cow-closure", "snapuserd-dm-user-closure", "persistent-operation-owner", "forced-restart-recovery", "stock-return-fallback"}},
        {"android-secondary", "Isolated second Android installation", "unaccepted-second-android-backend",
            {"second-android-data-key-isolation", "second-android-metadata-misc-isolation", "installed-avb-trust", "keymint-tee-chain", "whole-ab-boot-stacks", "stock-return-fallback", "persistent-operation-owner", "forced-restart-recovery"}},
        {"boot-once", "One-time Android, Linux or Windows boot", "unaccepted-uke-uefi-aloha-backend",
            {"uefi-aloha-unit-port", "efivar-esp-identity", "loader-signature-policy", "root-dt-module-abi-chain", "persistent-operation-owner", "forced-restart-recovery", "stock-return-fallback"}},
        {"boot-fallback", "Health-confirmed boot and preserved fallback", "unaccepted-os-health-backend",
            {"uefi-aloha-unit-port", "efivar-esp-identity", "loader-signature-policy", "root-dt-module-abi-chain", "trusted-os-health-receipt", "persistent-operation-owner", "forced-restart-recovery", "stock-return-fallback"}},
        {"kernel-boot-chain", "Installed root, kernel, DT and module trust", "unaccepted-installed-boot-chain",
            {"root-block-identity", "root-dt-module-abi-chain", "initramfs-executable-closure", "loader-signature-policy", "stock-return-fallback"}},
        {"linux-repair", "Installed Linux package and startup repair", "unaccepted-installed-distro-repair",
            {"root-block-identity", "installed-distro-runtime-closure", "installed-package-repair-oracle", "installed-initramfs-selinux-oracle", "persistent-operation-owner", "forced-restart-recovery"}},
        {"storage-write", "Device storage write readiness", "unaccepted-physical-storage-writer",
            {"persistent-operation-owner", "forced-restart-recovery", "stock-return-fallback"}}
    };
    return items;
}
std::set<std::string> requirements() {
    std::set<std::string> result;
    for(const auto& feature:features())for(const auto* name:feature.requirements)result.insert(name);
    return result;
}
std::string trim(std::string input) {
    while(!input.empty() && (input.back()=='\n' || input.back()=='\r'))input.pop_back();
    return input;
}
[[maybe_unused]] bool text(const Value& value,std::size_t maximum=512) {
    return value.isString() && !value.asString().empty() && value.asString().size()<=maximum && value.asString().find('\0')==std::string::npos;
}
void check(Value& row,const std::string& code,bool passed,const std::string& reason) {
    Value item; item["code"]=code; item["passed"]=passed; item["reason"]=reason;
    row["checks"].append(item); if(!passed)row["blockers"].append(code);
}
Value base(const std::string& profile,bool fixture) {
    Value out; out["schema"]=1; out["format"]="ure-platform-capabilities"; out["requested_profile"]=profile;
    out["read_only"]=true; out["private_record"]=true; out["fixture_declarations_only"]=fixture;
    out["physical_test_record"]=false; out["live_action_allowed"]=false; out["credential_use_allowed"]=false;
    out["mapper_creation_allowed"]=false; out["encrypted_mount_allowed"]=false; out["atomic_snapshot"]=false;
    out["trusted_evidence_registry_configured"]=false;
    out["features"]=Value(Json::arrayValue); out["required_evidence"]=Value(Json::arrayValue);
    for(const auto& name:requirements())out["required_evidence"].append(name);
    out["evidence_context_fields"]=Value(Json::arrayValue);
    for(const auto* name:{"profile","commercial_model","model_number","device","product","hardware_sku","vendor_sku",
        "firmware_version","build_fingerprint","unit_identity_sha256","boot_id_sha256","kernel_sha256"})out["evidence_context_fields"].append(name);
    out["deferred"]["bitlocker"]=true; out["deferred"]["ssh_network"]=true;
    return out;
}
Value attribute(const Root& system,const std::string& path) {
    Value out; out["state"]="unavailable";
    try {
        auto file=system.open_resolved(path,O_RDONLY|O_NONBLOCK); struct stat info{};
        require(::fstat(file.get(),&info)==0 && S_ISREG(info.st_mode),"invalid-file","Telemetry must be a regular sysfs attribute");
        // sysfs st_size is not the length of its value. Read to EOF with a
        // fixed attribute budget, including when the attribute uses a class alias.
        std::array<char,257> buffer{}; std::string value;
        while(true) {
            const auto count=::read(file.get(),buffer.data(),buffer.size());
            if(count<0 && errno==EINTR)continue;
            require(count>=0,"io-error","Cannot read telemetry attribute");
            if(count==0)break;
            require(value.size()+static_cast<std::size_t>(count)<=256,"size-limit","Telemetry attribute exceeds its budget");
            value.append(buffer.data(),static_cast<std::size_t>(count));
        }
        value=trim(std::move(value));
        require(!value.empty() && value.find('\0')==value.npos,"invalid-file","Telemetry attribute is empty or contains NUL");
        out["state"]="observed"; out["value"]=value;
    } catch(const Error& error) {
        out["code"]=error.code;
        if(error.code=="invalid-file" || error.code=="size-limit")out["state"]="malformed";
        else if(error.code=="path-unavailable" && (errno==EACCES || errno==EPERM))out["state"]="denied";
    }
    return out;
}
std::vector<std::string> directory(const Root& system,const std::string& path,std::size_t limit,Value& status) {
    try { return system.list(path,limit); }
    catch(const Error& error) { status["enumeration_code"]=error.code; return {}; }
}
bool decimal(const Value& item,int low,int high) {
    if(item["state"]!="observed" || !item["value"].isString())return false;
    const auto value=item["value"].asString(); int number=0;
    const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),number);
    return error==std::errc{} && end==value.data()+value.size() && number>=low && number<=high;
}
Value health(const Root& system) {
    Value out; out["read_only"]=true; out["threshold_policy_configured"]=false; out["decision"]="hold";
    out["battery"]=Value(Json::arrayValue); out["thermal"]=Value(Json::arrayValue); out["ufs"]=Value(Json::arrayValue);
    const auto power="sys/class/power_supply";
    for(const auto& name:directory(system,power,64,out)) {
        if(!identifier(name))continue;
        const auto prefix=std::string(power)+"/"+name+"/";
        if(attribute(system,prefix+"type")["value"]!="Battery")continue;
        Value row; row["name"]=name;
        for(const auto* key:{"present","capacity","status","health","temp"})row[key]=attribute(system,prefix+key);
        row["capacity_valid"]=decimal(row["capacity"],0,100);
        row["temperature_unit"]="tenths-of-degree-Celsius; channel acceptance separate";
        row["present_valid"]=decimal(row["present"],0,1); out["battery"].append(row);
    }
    for(const auto& name:directory(system,"sys/class/thermal",128,out)) {
        if(!name.starts_with("thermal_zone") || !identifier(name))continue;
        Value row; row["name"]=name;
        row["type"]=attribute(system,"sys/class/thermal/"+name+"/type");
        row["temp"]=attribute(system,"sys/class/thermal/"+name+"/temp");
        row["reported_abi_unit"]="millidegrees-Celsius; vendor channel semantics unaccepted";
        row["calibrated_channel"]=false; out["thermal"].append(row);
    }
    // Discover controllers; do not import another device's register addresses.
    // Only the standard read-only health descriptor is queried, never debug,
    // calibration, reset, bkops or firmware endpoints.
    for(const auto& name:directory(system,"sys/bus/platform/devices",512,out)) {
        if(!identifier(name) || !(name.ends_with(".ufshc") || name.ends_with(".ufs")))continue;
        Value row; row["controller"]=name;
        for(const auto* key:{"eol_info","life_time_estimation_a","life_time_estimation_b"})
            row[key]=attribute(system,"sys/bus/platform/devices/"+name+"/health_descriptor/"+key);
        row["interpretation"]="raw-descriptor-values; model policy unconfigured"; out["ufs"].append(row);
    }
    out["blockers"]=Value(Json::arrayValue);
    out["blockers"].append("health-unit-channel-policy-unaccepted");
    out["blockers"].append("health-threshold-duration-policy-unconfigured");
    if(out["battery"].size()!=1)out["blockers"].append("battery-observation-missing-or-ambiguous");
    else {
        const auto& battery=out["battery"][0];
        if(battery["present_valid"]!=true || battery["present"]["value"]!="1")out["blockers"].append("battery-presence-unverified");
        if(battery["capacity_valid"]!=true)out["blockers"].append("battery-capacity-unverified");
        if(battery["status"]["value"]!="Charging" && battery["status"]["value"]!="Discharging" && battery["status"]["value"]!="Full")out["blockers"].append("battery-charge-state-unverified");
        if(battery["health"]["value"]!="Good")out["blockers"].append("battery-health-unverified");
        if(battery["temp"]["state"]!="observed")out["blockers"].append("battery-temperature-observation-unavailable");
    }
    if(out["thermal"].empty())out["blockers"].append("thermal-observation-unavailable");
    if(out["ufs"].size()!=1)out["blockers"].append("ufs-health-missing-or-ambiguous");
    else for(const auto* key:{"eol_info","life_time_estimation_a","life_time_estimation_b"})
        if(out["ufs"][0][key]["state"]!="observed")out["blockers"].append(std::string("ufs-")+key+"-observation-unavailable");
    return out;
}
[[maybe_unused]] std::string property(const char* name) {
#ifdef __ANDROID__
    std::array<char,PROP_VALUE_MAX> result{}; __system_property_get(name,result.data()); return result.data();
#else
    (void)name; return {};
#endif
}
[[maybe_unused]] Value query(const std::string& command,const std::vector<std::string>& args={}) {
    Value row; row["available"]=false;
    try {
        std::vector<std::string> words{command}; words.insert(words.end(),args.begin(),args.end());
        const auto result=run_tool("bootctl",words,2);
        row["available"]=result.status==0 && !result.timed_out;
        row["value"]=trim(result.output); row["exit_status"]=result.status; row["timed_out"]=result.timed_out;
    } catch(const Error& error) { row["code"]=error.code; }
    return row;
}
Value boot_observation(bool authoritative) {
    Value out; out["read_only"]=true; out["available"]=false; out["consistent_reads"]=false;
#ifdef __ANDROID__
    if(authoritative) {
        out["slot_suffix"]=property("ro.boot.slot_suffix");
        for(const auto* command:{"get-number-slots","get-current-slot","get-active-boot-slot","get-snapshot-merge-status"})out[command]=query(command);
        for(unsigned slot=0;slot<2;++slot) {
            Value row; row["slot"]=slot;
            row["bootable"]=query("is-slot-bootable",{std::to_string(slot)});
            row["successful"]=query("is-slot-marked-successful",{std::to_string(slot)}); out["slots"].append(row);
        }
        bool consistent=property("ro.boot.slot_suffix")==out["slot_suffix"].asString();
        for(const auto* command:{"get-number-slots","get-current-slot","get-active-boot-slot","get-snapshot-merge-status"}) {
            const auto again=query(command);
            consistent=consistent && again["available"]==true && out[command]["available"]==true && again["value"]==out[command]["value"];
        }
        out["consistent_reads"]=consistent; out["available"]=consistent;
    } else out["code"]="platform-observation-root-untrusted";
#else
    (void)authoritative; out["code"]="android-runtime-unavailable";
#endif
    return out;
}
void boot_checks(Value& out,const Value& observed) {
    check(out,"boot-control-observation-unavailable",observed["available"]==true && observed["consistent_reads"]==true,
        "Read-only HAL observations must succeed and remain equal across bounded repeated reads; this is not an atomic snapshot");
    check(out,"two-ab-slots-unverified",observed["get-number-slots"]["available"]==true && observed["get-number-slots"]["value"]=="2","Boot control must report exactly two A/B slots");
    const auto current=observed["get-current-slot"]["value"].asString(),suffix=observed["slot_suffix"].asString();
    check(out,"active-slot-inconsistent",observed["get-current-slot"]["available"]==true && ((current=="0" && suffix=="_a") || (current=="1" && suffix=="_b")),"Running slot and recovery boot suffix must agree");
    check(out,"pending-slot-transition",observed["get-active-boot-slot"]["available"]==true && (current=="0" || current=="1") && observed["get-active-boot-slot"]["value"]==current,"Pending next-boot slot transitions require a separately reviewed lifecycle");
    check(out,"virtual-ab-not-idle",observed["get-snapshot-merge-status"]["available"]==true && observed["get-snapshot-merge-status"]["value"]=="none",
        "Unknown, snapshotted, merging, cancelled, malformed or missing merge state holds writes; this does not authorize merge or cancellation");
    bool fallback=false;
    if((current=="0" || current=="1") && observed["slots"].isArray() && observed["slots"].size()==2) {
        const auto& row=observed["slots"][current=="0" ? 1U : 0U];
        fallback=row["bootable"]["available"]==true && row["bootable"]["value"]=="1" &&
            row["successful"]["available"]==true && row["successful"]["value"]=="1";
    }
    check(out,"stock-slot-health-unverified",fallback,"Inactive stock fallback must be both bootable and marked successful; HAL flags still do not prove stock bytes or a physical boot");
}
void append_features(Value& out,const std::map<std::string,bool>& declared={},bool context_matches=false) {
    for(const auto& definition:features()) {
        Value item; item["id"]=definition.id; item["name"]=definition.name; item["backend"]=definition.backend;
        item["state"]="unavailable"; item["live_action_allowed"]=false; item["physical_test_record"]=false;
        item["checks"]=Value(Json::arrayValue); item["blockers"]=Value(Json::arrayValue);
        check(item,"exact-unit-profile-unaccepted",false,"Commercial model, SKU, capacity, installed firmware, six-LUN geometry and this unit's original GUID/boot backups are unaccepted");
        check(item,"platform-backend-unaccepted",false,definition.backend);
        check(item,"health-admission-unaccepted",false,"Fresh battery, charging, thermal and UFS observations need exact-unit channel, duration, threshold and hold policies");
        bool matches=context_matches;
        for(const auto* name:definition.requirements) {
            const auto found=declared.find(name); const bool matched=found!=declared.end() && found->second;
            matches=matches && matched;
            Value record; record["check"]=name; record["accepted"]=false; record["fixture_declaration_matches"]=matched;
            item["required_evidence"].append(record); item["blockers"].append(std::string(name)+"-unaccepted");
        }
        item["fixture_requirements_match"]=out["fixture_declarations_only"]==true && matches;
        out["features"].append(item);
    }
}
#ifndef __ANDROID__
void fields(const Value& value,std::initializer_list<const char*> allowed) {
    require(value.isObject(),"invalid-platform-declaration","Platform declarations must be objects");
    std::set<std::string> names; for(const auto* key:allowed)names.insert(key);
    require(value.size()==names.size(),"invalid-platform-declaration","Platform declaration has missing or extra fields");
    for(const auto& key:value.getMemberNames())require(names.contains(key),"invalid-platform-declaration","Unknown platform declaration field");
}
void context(const Value& value) {
    fields(value,{"profile","identity","unit_identity_sha256","boot_id_sha256","kernel_sha256"});
    require(text(value["profile"],96) && identifier(value["profile"].asString()),"invalid-platform-declaration","An explicit bounded profile is required");
    fields(value["identity"],{"commercial_model","model_number","device","product","hardware_sku","vendor_sku","firmware_version","build_fingerprint"});
    for(const auto& name:value["identity"].getMemberNames())require(text(value["identity"][name]),"invalid-platform-declaration","Exact commercial/SKU/firmware identity fields are required");
    for(const auto* name:{"unit_identity_sha256","boot_id_sha256","kernel_sha256"})
        require(text(value[name],64) && hash_valid(value[name].asString()) && value[name].asString()!=sha256(""),"invalid-platform-declaration","Exact unit, boot and kernel hashes are required");
}
#endif
} // namespace

void platform_require_live_action(std::string_view feature) {
    const auto found=std::find_if(features().begin(),features().end(),[&](const auto& item) { return feature==item.id; });
    require(found!=features().end(),"invalid-platform-feature","Unknown platform capability");
    throw Error("platform-action-unavailable",std::string(found->name)+" requires accepted exact-unit trust, backend, health and fallback contracts; inspect platform capabilities");
}
Value platform_capabilities(const Root& system,const std::string& profile) {
    require(identifier(profile) && profile.size()<=96,"invalid-profile","Select an explicit bounded firmware profile");
    auto out=base(profile,false); out["device_profile"]=device_profile_admission_status(system,profile);
    const bool authoritative=out["device_profile"]["current_system_root"]==true;
    out["current_system_root"]=authoritative;
    out["boot_control"]=boot_observation(authoritative); out["health"]=health(system);
    out["checks"]=Value(Json::arrayValue); out["blockers"]=Value(Json::arrayValue);
    check(out,"platform-observation-root-untrusted",authoritative,"Imported filesystem trees cannot establish current kernel or property authority");
    boot_checks(out,out["boot_control"]);
    out["decision"]="hold";
    out["observation_scope"]=authoritative ? "current-root-read-only; installed trust unaccepted" : "imported-root-read-only; non-authoritative";
    append_features(out); return out;
}
Value platform_compare_fixture(const Value& contract,const Value& observation) {
#ifdef __ANDROID__
    (void)contract; (void)observation;
    throw Error("fixture-only-command","Imported platform declarations cannot authorize or simulate Android device admission");
#else
    fields(contract,{"schema","format","context","requirements"}); fields(observation,{"schema","format","context","evidence","boot_control"});
    require(contract["schema"]==1 && contract["format"]=="ure-platform-contract" && observation["schema"]==1 &&
        observation["format"]=="ure-platform-observation","invalid-platform-declaration","Wrong platform declaration schema or format");
    context(contract["context"]); context(observation["context"]);
    const auto names=requirements();
    require(contract["requirements"].isArray() && contract["requirements"].size()==names.size() &&
        observation["evidence"].isArray() && observation["evidence"].size()<=names.size(),"invalid-platform-declaration","Every required evidence class must be declared exactly once within the budget");
    std::map<std::string,std::string> expected;
    for(const auto& row:contract["requirements"]) {
        fields(row,{"check","artifact_sha256"});
        require(text(row["check"],80) && names.contains(row["check"].asString()) && text(row["artifact_sha256"],64) && hash_valid(row["artifact_sha256"].asString()) &&
            expected.emplace(row["check"].asString(),row["artifact_sha256"].asString()).second,"invalid-platform-declaration","Unknown, duplicate or malformed platform requirement");
    }
    const auto context_hash=sha256(json(contract["context"])); std::map<std::string,bool> declared;
    for(const auto& row:observation["evidence"]) {
        fields(row,{"check","artifact_sha256","context_sha256","passed"});
        require(text(row["check"],80) && names.contains(row["check"].asString()) && text(row["artifact_sha256"],64) && hash_valid(row["artifact_sha256"].asString()) &&
            text(row["context_sha256"],64) && hash_valid(row["context_sha256"].asString()) && row["passed"].isBool(),"invalid-platform-declaration","Invalid context-bound evidence declaration");
        const auto name=row["check"].asString();
        require(declared.emplace(name,row["passed"]==true && row["artifact_sha256"]==expected.at(name) && row["context_sha256"]==context_hash).second,
            "invalid-platform-declaration","Duplicate platform evidence");
    }
    // Restrict observations to the exact parser subset used by runtime reads.
    const auto& boot=observation["boot_control"];
    fields(boot,{"available","consistent_reads","slot_suffix","get-number-slots","get-current-slot","get-active-boot-slot","get-snapshot-merge-status","slots"});
    require(boot["available"].isBool() && boot["consistent_reads"].isBool() && text(boot["slot_suffix"],8) && boot["slots"].isArray() && boot["slots"].size()==2,
        "invalid-platform-declaration","Incomplete bounded boot-control observation");
    auto validate_query=[](const Value& row) { fields(row,{"available","value"});
        require(row["available"].isBool() && row["value"].isString() && row["value"].asString().size()<=32 && row["value"].asString().find('\0')==std::string::npos,
            "invalid-platform-declaration","Invalid bounded boot-control query"); };
    for(const auto* key:{"get-number-slots","get-current-slot","get-active-boot-slot","get-snapshot-merge-status"})validate_query(boot[key]);
    for(unsigned slot=0;slot<2;++slot) {
        const auto& row=boot["slots"][slot]; fields(row,{"slot","bootable","successful"});
        require(row["slot"].isUInt() && row["slot"].asUInt()==slot,"invalid-platform-declaration","Boot slot records must have exact ordered identity");
        validate_query(row["bootable"]); validate_query(row["successful"]);
    }
    auto out=base(contract["context"]["profile"].asString(),true);
    out["context_sha256"]=context_hash; out["checks"]=Value(Json::arrayValue); out["blockers"]=Value(Json::arrayValue);
    const bool matches=json(contract["context"])==json(observation["context"]);
    check(out,"platform-context-mismatch",matches,"Every model/SKU, installed firmware, unit, current boot and kernel declaration must match exactly");
    boot_checks(out,boot); out["boot_control"]=boot;
    bool complete=matches && out["blockers"].empty() && declared.size()==names.size();
    for(const auto& [name,matched]:declared) { (void)name; complete=complete && matched; }
    out["fixture_checks_passed"]=complete;
    append_features(out,declared,matches && out["blockers"].empty());
    out["comparison_is_artifact_verification"]=false; out["comparison_is_authentication"]=false;
    out["decision"]="hold"; return out;
#endif
}
} // namespace ure
