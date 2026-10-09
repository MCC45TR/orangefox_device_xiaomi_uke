// SPDX-License-Identifier: Apache-2.0
// Pure native admission controls and disposable regular-file observations.
// This test never creates a mapper, mounts a filesystem or opens a block node.
#include "../../src/device/xiaomi/uke/recoveryctl/libuke/dualboot_device.cpp"
#include <iostream>
#include <map>

namespace {
constexpr std::uint64_t mib=1048576,capacity=128ULL*1024*mib;
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
struct HandoffState { int claims=0,anchors=0,effects=0,readbacks=0; };
struct HandoffFd {
    HandoffState* state=nullptr; bool exclusive=false;
    HandoffFd()=default;
    HandoffFd(HandoffState& value,bool claim):state(&value),exclusive(claim) { if(claim)++state->claims; else ++state->anchors; }
    HandoffFd(const HandoffFd&)=delete;
    HandoffFd& operator=(const HandoffFd&)=delete;
    HandoffFd(HandoffFd&& other) noexcept:state(other.state),exclusive(other.exclusive) { other.state=nullptr; }
    HandoffFd& operator=(HandoffFd&& other) noexcept { close(); state=other.state; exclusive=other.exclusive; other.state=nullptr; return *this; }
    ~HandoffFd() { close(); }
    void close() noexcept { if(state) { if(exclusive)--state->claims; else --state->anchors; state=nullptr; } }
};
template<class F> void reject(F action,const char* code) {
    try { action(); } catch(const ure::Error& error) {
        if(error.code!=code)throw std::runtime_error(std::string("Expected ")+code+", received "+error.code+": "+error.what());
        return;
    }
    throw std::runtime_error("Required refusal was absent");
}
ure::Value data_object() { ure::Value data; data["device_number"]="8:32"; data["bytes"]=Json::UInt64(16ULL*1024*mib); return data; }
ure::DmTable table(const std::string& parameters="aes-xts-plain64 private-test-key 0 8:32 0 3 sector_size:4096 iv_large_sectors wrappedkey_v0") {
    ure::DmTable out; out.type="default-key"; out.parameters=parameters; out.length=data_object()["bytes"].asUInt64()/512; out.open_count=0; return out;
}
ure::Value mount() {
    ure::Value result; result["device_number"]="8:21"; result["path"]="/metadata"; result["filesystem"]="f2fs";
    result["options"]="ro,nosuid,nodev"; result["super_options"]="ro,norecovery"; return result;
}
ure::Value usage() {
    ure::Value out; out["blockers"]=ure::Value(Json::arrayValue); out["blocker_count"]=0; out["blockers_truncated"]=false;
    for(const auto* category:{"mounts","processes","swaps","usb"})out["coverage"][category]=true;
    return out;
}
void block(ure::Value& out,const char* code,const ure::Value& detail) {
    ure::Value row; row["code"]=code; row["detail"]=detail; out["blockers"].append(row); out["blocker_count"]=out["blockers"].size();
}
void put(std::string& bytes,std::size_t offset,std::uint64_t value,unsigned count) {
    for(unsigned i=0;i<count;++i)bytes.at(offset+i)=static_cast<char>((value>>(i*8))&255);
}
std::uint32_t crc(std::string_view bytes) {
    std::uint32_t out=UINT32_MAX; for(char byte:bytes) { out^=static_cast<unsigned char>(byte);
        for(unsigned bit=0;bit<8;++bit)out=(out>>1)^((out&1U) ? 0xedb88320U : 0U); } return out^UINT32_MAX;
}
struct Workspace {
    ure::fs::path path;
    Workspace() { std::array<char,64> pattern{}; const std::string text="/tmp/ure-dualboot-device-XXXXXX";
        std::copy(text.begin(),text.end(),pattern.begin()); const char* made=::mkdtemp(pattern.data()); check(made,"Cannot create private fixture"); path=made; }
    ~Workspace() { std::error_code ignored; ure::fs::remove_all(path,ignored); }
};
void fixture(const ure::fs::path& file) {
    ure::Fd fd(::open(file.c_str(),O_RDWR|O_CREAT|O_EXCL,0600)); check(fd.get()>=0 && ::ftruncate(fd.get(),static_cast<off_t>(capacity))==0,"Cannot size sparse fixture");
    constexpr std::uint64_t sector=4096,sectors=capacity/sector,last=sectors-6,backup=sectors-5;
    std::string entries(4096,'\0');
    for(unsigned index=0;index<32;++index) {
        const auto at=index*128; for(unsigned byte=0;byte<16;++byte) { entries[at+byte]=static_cast<char>(byte+11); entries[at+16+byte]=static_cast<char>(byte+51); }
        put(entries,at+16,index+1,4); put(entries,at+32,(index+1)*mib/sector,8); put(entries,at+40,index==31 ? last : (index+2)*mib/sector-1,8);
        const auto label=index==31 ? "userdata" : index==20 ? "metadata" : "oem_"+std::to_string(index+1);
        for(std::size_t i=0;i<label.size();++i)entries[at+56+2*i]=label[i];
    }
    ure::write_exact(fd.get(),2*sector,entries); ure::write_exact(fd.get(),backup*sector,entries);
    for(const auto lba:std::array<std::uint64_t,2>{1,sectors-1}) {
        std::string header(sector,'\0'); header.replace(0,8,"EFI PART"); put(header,8,0x10000,4); put(header,12,92,4);
        put(header,24,lba,8); put(header,32,sectors-lba,8); put(header,40,6,8); put(header,48,last,8);
        for(unsigned byte=0;byte<16;++byte)header[56+byte]=static_cast<char>(byte+81);
        put(header,72,lba==1 ? 2 : backup,8); put(header,80,32,4); put(header,84,128,4); put(header,88,crc(entries),4);
        put(header,16,crc(std::string_view(header).substr(0,92)),4); ure::write_exact(fd.get(),lba*sector,header);
    }
    std::string mbr(sector,'\0'); mbr[450]=static_cast<char>(0xee); put(mbr,454,1,4); put(mbr,458,UINT32_MAX,4); mbr[510]=0x55; mbr[511]=static_cast<char>(0xaa);
    ure::write_exact(fd.get(),0,mbr); ure::sync_fd(fd.get());
}
ure::Value preview(const ure::fs::path& file) {
    const auto input=ure::parse_json(R"({"schema":1,"format":"uke-dualboot-request","linux_enabled":true,"windows_enabled":true,"separate_linux_boot":true,"userdata_policy":"recreate","userdata_filesystem":"f2fs","esp":{"size":"512","unit":"MiB"},"linux_boot":{"size":"512","unit":"MiB"},"linux":{"size":"8","unit":"GiB","filesystem":"ext4"},"windows":{"size":"16","unit":"GiB"}})");
    auto target=ure::storage_image(file,4096); auto gpt=ure::gpt_layout_plan(target,ure::dualboot_layout_request(input),"fixture");
    auto identity=target.identity; identity["kind"]="live-block"; identity["partition"]=false; identity["read_only_state"]=false;
    identity["unit_identity_available"]=true; identity["unit_identity_sha256"]=std::string(64,'a'); identity["lun_address"]="0:0:0:0";
    identity["disk_guid"]=gpt["current_table"]["disk_guid"]; identity["sysfs_path"]="sys/devices/mock/block/sda";
    gpt["target_identity"]=identity; gpt["layout"]["target_identity"]=identity;
    gpt["layout"]["layout_sha256"]=ure::live_seal(gpt["layout"],"layout_sha256"); gpt["plan_sha256"]=ure::live_seal(gpt);
    ure::Value out; out["schema"]=1; out["operation"]="dualboot.setup"; out["operation_id"]=ure::operation_id(); out["firmware_profile"]="fixture";
    out["request"]=input; out["target_identity"]=identity; out["gpt"]=gpt; out["data_loss"]=true; out["confirmation_phrase"]="ERASE USERDATA";
    out["writes_other_partition_payloads"]=false; out["device_commands"]=ure::dualboot_device_commands(out); out["plan_sha256"]=ure::live_seal(out); return out;
}
void reseal(ure::Value& value) {
    value["gpt"]["layout"]["layout_sha256"]=ure::live_seal(value["gpt"]["layout"],"layout_sha256");
    value["gpt"]["plan_sha256"]=ure::live_seal(value["gpt"]); value["plan_sha256"]=ure::live_seal(value);
}
struct RestoreInterrupted {};
void repeated_restore_trials(const ure::fs::path& workspace) {
    constexpr std::size_t region_bytes=4096,disk_bytes=65536;
    std::vector<ure::Region> regions;
    for(const auto* name:{"backup_table","backup_header","primary_table","primary_header","protective_mbr"}) {
        const auto index=regions.size();
        regions.push_back({name,std::string(region_bytes,static_cast<char>('A'+index)),
            std::string(region_bytes,static_cast<char>('a'+index)),4096+index*8192});
    }
    std::string original(disk_bytes,'!');
    for(const auto& region:regions)original.replace(static_cast<std::size_t>(region.offset),region_bytes,region.before);
    auto protected_bytes=[&](int descriptor) {
        auto bytes=ure::storage_read(descriptor,0,disk_bytes);
        for(const auto& region:regions)bytes.replace(static_cast<std::size_t>(region.offset),region_bytes,region_bytes,'\0');
        return bytes;
    };
    unsigned interruptions=0,checkpoints=0;
    for(std::size_t interrupted=0;interrupted<regions.size();++interrupted) {
        const auto path=workspace/("repeat-restore-"+std::to_string(interrupted)); auto store=ure::private_directory(path,true);
        { auto disk=store.open("disk.bin",O_RDWR|O_CREAT|O_EXCL,0600); ure::write_exact(disk.get(),0,original);
          for(std::size_t prior=0;prior<interrupted;++prior)ure::write_exact(disk.get(),regions[prior].offset,regions[prior].after);
          ure::write_exact(disk.get(),regions[interrupted].offset,regions[interrupted].after.substr(0,region_bytes/2)); ure::sync_fd(disk.get()); }
        ure::Value initial; initial["phase"]="GPT_WRITE"; initial["region"]=regions[interrupted].name; store.save_record("state.json",initial);
        auto original_disk=store.open("disk.bin",O_RDONLY); const auto protected_original=protected_bytes(original_disk.get()); original_disk=ure::Fd{};
        std::map<std::string,unsigned> attempts; bool complete=false;
        for(unsigned retry=0;!complete && retry<64;++retry) {
            // A fresh descriptor and the actual synced JSON record model a
            // killed writer reopening its journal after every partial effect.
            auto disk=store.open("disk.bin",O_RDWR); auto state=ure::parse_json(store.read("state.json",4096));
            try {
                ure::write_region_set(disk.get(),regions,state,true,[&](ure::Value& current,const char* phase_name) {
                    const auto durable=ure::parse_json(store.read("state.json",4096));
                    for(const auto& observation:ure::region_inspect(disk.get(),regions,durable))
                        if(observation["before"]==false && observation["after"]==false)
                            check(current["region"]==observation["name"],"Restoration advanced its durable marker past an unhealed torn GPT region");
                    current["phase"]=phase_name; store.save_record("state.json",current,true);
                    check(ure::json(ure::parse_json(store.read("state.json",4096)))==ure::json(current),"Restore write began before its exact active region was durable");
                    ++checkpoints;
                },[&](int descriptor,std::uint64_t offset,const std::string& next) {
                    const auto durable=ure::parse_json(store.read("state.json",4096));
                    const auto region=std::find_if(regions.begin(),regions.end(),[&](const ure::Region& item){return item.offset==offset;});
                    check(region!=regions.end() && durable["phase"]=="GPT_RESTORE" && durable["region"]==region->name,"Partial restore effect differs from its synced active-region record");
                    const auto attempt=attempts[region->name]++;
                    if(attempt<2) {
                        ure::write_exact(descriptor,offset,next.substr(0,512*(attempt+1))); ure::sync_fd(descriptor); ++interruptions;
                        throw RestoreInterrupted{};
                    }
                    ure::write_exact(descriptor,offset,next);
                });
                complete=true;
            } catch(const RestoreInterrupted&) {
                (void)ure::region_inspect(disk.get(),regions,ure::parse_json(store.read("state.json",4096)));
            }
            check(protected_bytes(disk.get())==protected_original,"Interrupted restore changed bytes outside the five GPT regions");
            if(complete)check(ure::storage_read(disk.get(),0,disk_bytes)==original,"Repeatedly interrupted restoration did not reproduce every original byte");
        }
        check(complete,"Repeated restore never reached the original GPT bytes");
        check(attempts.size()==interrupted+1,"Restoration wrote a region that was already original");
        for(const auto& attempt:attempts)check(attempt.second==3,"Every affected region must survive two partial restore interruptions before completion");
    }
    check(interruptions==30 && checkpoints==45,"Repeated restore fault-injection coverage changed");
    for(std::size_t damaged=0;damaged<regions.size();++damaged) {
        const auto path=workspace/("foreign-restore-"+std::to_string(damaged)); auto store=ure::private_directory(path,true);
        auto disk=store.open("disk.bin",O_RDWR|O_CREAT|O_EXCL,0600); ure::write_exact(disk.get(),0,original);
        ure::write_exact(disk.get(),regions[damaged].offset,regions[damaged].after.substr(0,region_bytes/2));
        ure::write_exact(disk.get(),regions[damaged].offset+100,"?"); ure::sync_fd(disk.get());
        ure::Value state; state["phase"]="GPT_WRITE"; state["region"]=regions[damaged].name;
        const auto before=ure::storage_read(disk.get(),0,disk_bytes); unsigned effects=0;
        reject([&] { ure::write_region_set(disk.get(),regions,state,true,[&](ure::Value&,const char*){++effects;},
            [&](int,std::uint64_t,const std::string&){++effects;}); },"foreign-gpt-change");
        check(effects==0 && ure::storage_read(disk.get(),0,disk_bytes)==before,"Foreign GPT bytes reached a checkpoint or changed the image");
    }
    const auto apply_path=workspace/"canonical-apply"; auto apply_store=ure::private_directory(apply_path,true);
    auto apply_disk=apply_store.open("disk.bin",O_RDWR|O_CREAT|O_EXCL,0600); ure::write_exact(apply_disk.get(),0,original); ure::sync_fd(apply_disk.get());
    const auto protected_original=protected_bytes(apply_disk.get()); ure::Value apply_state; apply_state["phase"]="FILESYSTEMS_VERIFIED";
    unsigned applied=0;
    ure::write_region_set(apply_disk.get(),regions,apply_state,false,[&](ure::Value& current,const char* phase_name) {
        check(applied<regions.size() && current["region"]==regions[applied].name && std::string_view(phase_name)=="GPT_WRITE","Apply no longer publishes backup metadata first");
        current["phase"]=phase_name; apply_store.save_record("state.json",current,applied!=0); ++applied;
    },[&](int descriptor,std::uint64_t offset,const std::string& next) {
        check(ure::parse_json(apply_store.read("state.json",4096))["region"]==regions[applied-1].name,"Apply effect preceded its durable region record");
        ure::write_exact(descriptor,offset,next);
    });
    check(applied==5 && protected_bytes(apply_disk.get())==protected_original,"Canonical GPT apply changed protected bytes or missed a region");
    for(const auto& region:regions)check(ure::storage_read(apply_disk.get(),region.offset,region_bytes)==region.after,"Canonical GPT apply readback differs");
    std::cout<<"Repeated GPT restore: five partial-apply positions, 30 synced partial-restore interruptions, 45 durable checkpoints, five foreign-byte refusals, canonical apply order and complete protected-byte preservation passed on bounded regular files.\n";
}
}
int main() {
    try {
        dm_ioctl reply{}; reply.version[0]=DM_VERSION_MAJOR;
        for(const auto bytes:{offsetof(dm_ioctl,data),sizeof(dm_ioctl),ure::dm_budget}) {
            reply.data_size=static_cast<std::uint32_t>(bytes); ure::dm_reply_header(reply,ure::dm_budget);
        }
        for(unsigned variant=0;variant<5;++variant) {
            auto bad=reply;
            if(variant==0)bad.data_size=0;
            if(variant==1)bad.data_size=static_cast<std::uint32_t>(offsetof(dm_ioctl,data)-1);
            if(variant==2)bad.data_size=static_cast<std::uint32_t>(ure::dm_budget+1);
            if(variant==3)++bad.version[0];
            if(variant==4)bad.flags|=DM_BUFFER_FULL_FLAG;
            reject([&]{ure::dm_reply_header(bad,ure::dm_budget);},"invalid-dualboot-map");
        }
        for(unsigned failure=0;failure<6;++failure) {
            HandoffState state; unsigned opens=0,checks=0;
            try {
                ure::dualboot_view_handoff([&](bool exclusive){
                    ++opens;
                    if(failure==1 && opens==1)throw std::runtime_error("competing claim");
                    if(failure==2 && opens==2)throw std::runtime_error("unavailable anchor");
                    if(failure==5 && opens==3)throw std::runtime_error("changed post-tool ownership");
                    check(exclusive ? state.claims==0 : state.claims==1,"Invalid exclusive-claim acquisition order");
                    return HandoffFd(state,exclusive);
                },[&](const HandoffFd& anchor,int count){
                    ++checks; check(!anchor.exclusive && state.anchors==1 && state.claims+state.anchors==count,"Handoff count/descriptor mismatch");
                    if(failure==3 && checks==2)throw std::runtime_error("foreign map before tool");
                },[&](const HandoffFd& anchor){
                    check(anchor.state==&state && state.claims==0 && state.anchors==1,"Tool ran with conflicting block claim");
                    ++state.effects; if(failure==4)throw std::runtime_error("tool failed");
                },[&](const HandoffFd&){
                    check(state.claims==1 && state.anchors==1,"Readback did not reacquire the claim"); ++state.readbacks;
                });
                check(failure==0,"Required handoff failure was not observed");
            } catch(const std::runtime_error&) { if(failure==0)throw; }
            check(state.claims==0 && state.anchors==0,"Failed handoff leaked a claim or anchor");
            check(state.effects==(failure==0 || failure>=4 ? 1 : 0) && state.readbacks==(failure==0 ? 1 : 0),"Failure crossed an unverified tool/readback boundary");
        }
        ure::DmTable expected; expected.name="ure-fixture"; expected.uuid="URE-DUALBOOT-fixture"; expected.type="linear";
        expected.parameters="8:32 4096"; expected.device=ure::devnumber("253:0"); expected.length=4096; expected.flags=DM_ACTIVE_PRESENT_FLAG; expected.event=7;
        for(const auto count:{1,2}) {
            auto current=[&]{ ure::DmTable value; value.name=expected.name; value.uuid=expected.uuid; value.type=expected.type; value.parameters=expected.parameters;
                value.device=expected.device; value.length=expected.length; value.flags=expected.flags; value.event=expected.event; value.open_count=count; return value; };
            auto valid=current(); ure::owned_view_policy(valid,expected,count);
            for(unsigned variant=0;variant<15;++variant) {
                auto bad=current();
                if(variant==0)bad.name+="-other";
                if(variant==1)bad.uuid+="-other";
                if(variant==2)bad.device=ure::devnumber("253:1");
                if(variant==3)bad.type="crypt";
                if(variant==4)bad.start=1;
                if(variant==5)++bad.length;
                if(variant==6)bad.parameters="8:32 8192";
                if(variant==7)++bad.event;
                if(variant==8)++bad.open_count;
                if(variant==9)bad.flags=0;
                if(variant==10)bad.flags|=DM_READONLY_FLAG;
                if(variant==11)bad.flags|=DM_SUSPEND_FLAG;
                if(variant==12)bad.flags|=DM_INACTIVE_PRESENT_FLAG;
                if(variant==14)bad.flags|=DM_DEFERRED_REMOVE;
                reject([&]{ure::owned_view_policy(bad,expected,variant==13 ? 0 : count);},"stale-dualboot-view");
            }
        }
        ure::Value guard_graph,owners; guard_graph["objects"]=ure::Value(Json::arrayValue);
        const std::array<std::string,4> names{"sda","sda32","dm-0","dm-1"},numbers{"8:0","8:32","253:0","253:1"};
        for(std::size_t i=0;i<names.size();++i) {
            ure::Value item; item["kernel_name"]=names[i]; item["device_number"]=numbers[i]; item["stable_id"]="sysfs:fixture/"+names[i];
            item["sysfs_path"]="sys/devices/mock/"+names[i]; item["partition"]=i==1; item["parent_lun_name"]=i==1 ? "sda" : names[i];
            item["parent_lun_sysfs"]=i==1 ? "sys/devices/mock/sda" : item["sysfs_path"].asString(); item["dependencies_available"]=true;
            for(const auto* field:{"slaves","holders","mounts"})item[field]=ure::Value(Json::arrayValue);
            if(i>=2)item["slaves"].append(names[i-1]);
            if(i>=1 && i<3)item["holders"].append(names[i+1]);
            guard_graph["objects"].append(item);
        }
        for(const auto* name:{"mounts","swaps","processes","usb"})owners["coverage"][name]=true;
        for(const auto* name:{"mounts","open_users","swaps","usb"})owners[name]=ure::Value(Json::arrayValue);
        const auto guard_disk=guard_graph["objects"][0]["stable_id"].asString();
        const auto clear_guard=ure::storage_usage_policy(guard_graph,owners,guard_disk);
        check(clear_guard["quiescent_observed"]==true && clear_guard["related_objects"].size()==4,"Resolved idle userdata guard blocked final ownership");
        ure::device_usage_policy(clear_guard,ure::Value{});
        for(unsigned variant=0;variant<5;++variant) {
            auto graph=guard_graph,seen=owners; ure::Value owner; owner["device_number"]="253:1"; owner["path"]="/foreign";
            if(variant==0)seen["mounts"].append(owner);
            if(variant==1) { owner["write_access"]=true; seen["open_users"].append(owner); }
            if(variant==2)graph["objects"][3]["slaves"][0]="missing";
            if(variant==3)graph["objects"][1]["slaves"].append("dm-1");
            if(variant==4)graph["objects"][3]["dependencies_available"]=false;
            reject([&]{ure::device_usage_policy(ure::storage_usage_policy(graph,seen,guard_disk),ure::Value{});},
                variant==0 ? "metadata-not-readonly-norecovery" : "dualboot-device-busy");
        }
        std::cout<<"Owned view handoff: six success/failure paths, thirty exact mapper-policy refusals and five retained-guard ownership refusals passed with all local descriptors released. Callback/graph controls only; no block or mapper operations.\n";
        std::string recovery_command(32,'\0'); recovery_command.replace(0,13,"boot-recovery"); ure::recovery_bcb_command(recovery_command);
        for(const auto& refused:std::vector<std::string>{"",std::string(31,'\0'),std::string(33,'\0'),std::string(32,'\0'),std::string(32,static_cast<char>(0xff)),
            "boot-recovery",std::string("boot-fastboot")+std::string(19,'\0'),std::string("boot-recovery ")+std::string(18,'\0')})
            reject([&]{ure::recovery_bcb_command(refused);},"recovery-bcb-command-required");
        for(const auto offset:{0U,4U,12U,13U,31U}) {
            auto refused=recovery_command; refused[offset]=offset<13 ? '\0' : 'x';
            reject([&]{ure::recovery_bcb_command(refused);},"recovery-bcb-command-required");
        }
        ure::Value misc_graph; misc_graph["objects"]=ure::Value(Json::arrayValue); ure::Value misc;
        misc["label"]="misc"; misc["kernel_name"]="sdd3"; misc["sysfs_path"]="sys/devices/mock/block/sdd/sdd3"; misc["partition"]=true;
        misc["device_number"]="8:51"; misc["partuuid"]="a0a0a0a0-a0a0-a0a0-a0a0-a0a0a0a0a0a0"; misc["bytes"]=Json::UInt64(mib);
        misc["dependencies_available"]=true; misc["holders"]=ure::Value(Json::arrayValue); misc["slaves"]=ure::Value(Json::arrayValue);
        misc_graph["objects"].append(misc); check(ure::json(ure::recovery_misc_object(misc_graph))==ure::json(misc),"Unique kernel misc selection changed");
        for(unsigned variant=0;variant<10;++variant) {
            auto refused=misc_graph;
            if(variant==0)refused["objects"].clear();
            if(variant==1)refused["objects"].append(misc);
            if(variant==2)refused["objects"][0]["partition"]=false;
            if(variant==3)refused["objects"][0]["sysfs_path"]="dev/block/by-name/misc";
            if(variant==4)refused["objects"][0]["partuuid"]="invalid";
            if(variant==5)refused["objects"][0]["bytes"]=Json::UInt64(1024);
            if(variant==6)refused["objects"][0]["holders"].append("dm-1");
            if(variant==7)refused["objects"][0]["slaves"].append("sda1");
            if(variant==8)refused["objects"][0]["dependencies_available"]=false;
            if(variant==9)refused["objects"][0]["kernel_name"]="../sdd3";
            reject([&]{ure::recovery_misc_object(refused);},"recovery-bcb-unavailable");
        }
        ure::Value misc_plan,record; misc["parent_lun_sysfs"]="sys/devices/mock/block/sdd"; misc["partition_index"]=Json::UInt64(3); misc["start_512_sectors"]=Json::UInt64(128);
        record["label"]="misc"; record["partuuid"]=misc["partuuid"]; record["bytes"]=misc["bytes"]; record["index"]=Json::UInt64(3); record["start_lba"]=Json::UInt64(16);
        misc_plan["target_identity"]["sysfs_path"]=misc["parent_lun_sysfs"]; misc_plan["gpt"]["layout"]["protected_records"].append(record); ure::recovery_misc_geometry(misc,misc_plan);
        for(unsigned variant=0;variant<8;++variant) {
            auto refused=misc_plan;
            if(variant==0)refused["gpt"]["layout"]["protected_records"].clear();
            if(variant==1)refused["gpt"]["layout"]["protected_records"].append(record);
            if(variant==2)refused["target_identity"]["sysfs_path"]="sys/devices/mock/block/sde";
            if(variant==3)refused["gpt"]["layout"]["protected_records"][0]["bytes"]=Json::UInt64(2*mib);
            if(variant==4)refused["gpt"]["layout"]["protected_records"][0]["partuuid"]="b0b0b0b0-b0b0-b0b0-b0b0-b0b0b0b0b0b0";
            if(variant==5)refused["gpt"]["layout"]["protected_records"][0]["index"]=Json::UInt64(4);
            if(variant==6)refused["gpt"]["layout"]["protected_records"][0]["start_lba"]=Json::UInt64(17);
            if(variant==7)refused["gpt"]["layout"]["protected_records"][0]["start_lba"]=Json::UInt64(UINT64_MAX);
            reject([&]{ure::recovery_misc_geometry(misc,refused);},"recovery-bcb-unavailable");
        }
        reject([&]{ure::recovery_bcb_read(-1);},"recovery-bcb-unavailable");
        const auto userdata=data_object();
        auto accepted=table(); const auto policy=ure::crypto_policy(accepted,userdata);
        check(policy.type=="default-key" && policy.backing==ure::devnumber("8:32") && policy.offset==0 && policy.iv==0 && policy.wrapped,"Valid default-key policy was rejected");
        check(accepted.parameters.empty(),"Accepted key-bearing table was retained");
        for(const auto* parameters:{"aes-xts-plain64 private-test-key 1 8:32 0 3 sector_size:4096 iv_large_sectors wrappedkey_v0",
            "aes-xts-plain64 private-test-key 0 8:31 0 3 sector_size:4096 iv_large_sectors wrappedkey_v0",
            "aes-xts-plain64 private-test-key 0 8:32 1 3 sector_size:4096 iv_large_sectors wrappedkey_v0",
            "aes-xts-plain64 private-test-key 0 8:32 0 2 sector_size:4096 iv_large_sectors wrappedkey_v0",
            "aes-xts-plain64 private-test-key 0 8:32 0 3 sector_size:512 iv_large_sectors wrappedkey_v0",
            "aes-xts-plain64 private-test-key 0 8:32 0 3 sector_size:4096 iv_large_sectors wrappedkey_v1",
            "aes-xts-plain64 private-test-key 0 8:32 0 3 sector_size:4096 wrappedkey_v0 wrappedkey_v0",
            "aes-xts-plain64 private-test-key 0 8:32 0",
            "aes-xts-plain64 private-test-key 0 8:32 0 4 sector_size:4096 iv_large_sectors wrappedkey_v0 foreign_option"}) {
            auto rejected=table(parameters); reject([&]{ure::crypto_policy(rejected,userdata);},"unsupported-userdata-encryption");
            check(rejected.parameters.empty(),"Rejected key-bearing table was retained");
        }
        for(unsigned variant=0;variant<4;++variant) {
            auto rejected=table(); if(variant==0)rejected.open_count=1;
            if(variant==1)rejected.length-=8;
            if(variant==2)rejected.flags=DM_READONLY_FLAG;
            if(variant==3)rejected.type="crypt";
            reject([&]{ure::crypto_policy(rejected,userdata);},"unsupported-userdata-encryption"); check(rejected.parameters.empty(),"Early rejected key table was retained");
        }
        for(const auto* number:{"",":1","1:","-1:2","1:2:3","4294967296:0","0:4294967296"})reject([&]{ure::devnumber(number);},"invalid-dualboot-device");
        for(const auto* number:{"","1x","18446744073709551616","-1"})reject([&]{ure::decimal64(number);},"invalid-dualboot-device");
        ure::bounds(4096,4096,8192);
        reject([&]{ure::bounds(4096,8192,8192);},"invalid-dualboot-range"); reject([&]{ure::bounds(1,4096,8192);},"invalid-dualboot-range");
        reject([&]{ure::bounds(0,0,8192);},"invalid-dualboot-range"); reject([&]{ure::bounds(UINT64_MAX-4095,4096,UINT64_MAX);},"invalid-dualboot-range");
        const auto valid_mount=mount(); ure::metadata_mount_policy(valid_mount,"8:21");
        for(unsigned variant=0;variant<7;++variant) {
            auto bad=valid_mount;
            if(variant==0)bad["device_number"]="8:19";
            if(variant==1)bad["path"]="/foreign";
            if(variant==2)bad["filesystem"]="ext4";
            if(variant==3)bad["options"]="rw,noload";
            if(variant==4)bad["super_options"]="ro";
            if(variant==5)bad["super_options"]="ro,noload,rw";
            if(variant==6)bad["options"]="noro,nosuid";
            reject([&]{ure::metadata_mount_policy(bad,"8:21");},"metadata-not-readonly-norecovery");
        }
        ure::Value metadata; metadata["device_number"]="8:21";
        auto clear=usage(); ure::device_usage_policy(clear,metadata); block(clear,"mounted",valid_mount); block(clear,"mounts",valid_mount); ure::device_usage_policy(clear,metadata);
        for(const auto* category:{"open_users","swaps","usb","unresolved-holder","dependencies-unavailable"}) {
            auto busy=clear; block(busy,category,valid_mount); reject([&]{ure::device_usage_policy(busy,metadata);},"dualboot-device-busy");
        }
        auto busy=clear; busy["coverage"]["processes"]=false; reject([&]{ure::device_usage_policy(busy,metadata);},"dualboot-device-busy");
        busy=clear; busy["blockers_truncated"]=true; reject([&]{ure::device_usage_policy(busy,metadata);},"dualboot-device-busy");
        busy=clear; busy["blocker_count"]=128; reject([&]{ure::device_usage_policy(busy,metadata);},"dualboot-device-busy");
        busy=clear; auto bad_mount=valid_mount; bad_mount["path"]="/data"; block(busy,"mounted",bad_mount); reject([&]{ure::device_usage_policy(busy,metadata);},"dualboot-device-busy");
        Workspace work; repeated_restore_trials(work.path); const auto bcb_file=work.path/"misc-command.bin"; ure::Fd bcb(::open(bcb_file.c_str(),O_RDWR|O_CREAT|O_EXCL,0600));
        check(bcb.get()>=0,"Cannot create private BCB command fixture"); ure::write_exact(bcb.get(),0,recovery_command); ure::recovery_bcb_read(bcb.get());
        check(::ftruncate(bcb.get(),31)==0,"Cannot truncate private command fixture"); reject([&]{ure::recovery_bcb_read(bcb.get());},"recovery-bcb-unavailable");
        const auto disk=work.path/"device-model.img"; fixture(disk); auto plan=preview(disk);
        ure::device_plan(plan); (void)ure::untouched_ranges(plan);
        const auto commands=ure::dualboot_device_commands(plan); check(commands.size()==5,"Missing selected formatter command");
        auto esp_row=plan["gpt"]["layout"]["rows"][1]; esp_row["bytes"]=Json::UInt64(300*mib); ure::formatter_row(esp_row);
        check(ure::format_arguments(esp_row,"/proc/self/fd/99")==std::vector<std::string>{"-F","32","-s","1","-n","ESP","/proc/self/fd/99"},
            "Live ESP preview omits native-sector cluster sizing");
        for(const auto bytes:{64*mib,256*mib,300*mib-4096}) {
            esp_row["bytes"]=Json::UInt64(bytes); reject([&]{ure::formatter_row(esp_row);},"unsupported-live-filesystem");
        }
        std::string bpb(512,'\0'); put(bpb,11,4096,2); put(bpb,13,1,1); put(bpb,14,32,2); put(bpb,16,2,1);
        put(bpb,32,300*mib/4096,4); put(bpb,36,75,4); put(bpb,44,2,4); put(bpb,510,0xaa55,2); bpb.replace(82,8,"FAT32   ");
        ure::esp_bpb_policy(bpb,300*mib);
        reject([&]{ure::esp_bpb_policy(std::string_view(bpb).substr(0,511),300*mib);},"dualboot-esp-readback");
        for(unsigned variant=0;variant<14;++variant) {
            auto bad_bpb=bpb;
            if(variant==0)put(bad_bpb,11,512,2);
            if(variant==1)put(bad_bpb,13,0,1);
            if(variant==2)put(bad_bpb,13,3,1);
            if(variant==3)put(bad_bpb,13,2,1);
            if(variant==4)put(bad_bpb,16,0,1);
            if(variant==5)put(bad_bpb,32,300*mib/4096+1,4);
            if(variant==6)put(bad_bpb,36,1,4);
            if(variant==7)put(bad_bpb,44,1,4);
            if(variant==8)put(bad_bpb,44,300*mib/4096,4);
            if(variant==9)put(bad_bpb,17,1,2);
            if(variant==10)put(bad_bpb,22,1,2);
            if(variant==11)put(bad_bpb,42,1,2);
            if(variant==12)put(bad_bpb,510,0,2);
            if(variant==13)bad_bpb.replace(82,8,"FAT16   ");
            reject([&]{ure::esp_bpb_policy(bad_bpb,300*mib);},"dualboot-esp-readback");
        }
        reject([&]{ure::esp_bpb_policy(bpb,256*mib);},"dualboot-esp-readback");
        const auto ntfs_arguments=ure::format_arguments(plan["gpt"]["layout"]["rows"][4],"/proc/self/fd/99");
        check(ntfs_arguments==std::vector<std::string>{"-Q","-L","Windows","/proc/self/fd/99"},"NTFS preview permits mounted-volume force override");
        check(commands[0]["tool"]=="mkfs.f2fs" || commands[0]["tool"]=="make_f2fs","Wrong Android formatter");
        check(commands[0]["view_backing"]=="existing-verified-metadata-encryption-map" && commands[0]["view_backing_offset_512_sectors"].asUInt64()==0,"Userdata bypasses existing encryption");
        const auto actual_arguments=ure::format_arguments(plan["gpt"]["layout"]["rows"][0],"/proc/self/fd/99");
        check(actual_arguments.size()==commands[0]["argv"].size() && actual_arguments.back()=="/proc/self/fd/99","Preview and execution argv differ");
        for(std::size_t i=0;i+1<actual_arguments.size();++i)check(actual_arguments[i]==commands[0]["argv"][static_cast<Json::ArrayIndex>(i)].asString(),"Preview omits actual formatter options");
        for(unsigned i=1;i<commands.size();++i)check(commands[i]["view_backing"]=="original-userdata-raw-partition" && commands[i]["view_backing_offset_512_sectors"].asUInt64()>0,"OS formatter view begins before its userdata extent");
        auto bad=plan; bad["gpt"]["layout"]["rows"][0]["filesystem"]="ext4"; reseal(bad); reject([&]{ure::device_plan(bad);},"unsupported-live-filesystem");
        bad=plan; bad["gpt"]["layout"]["rows"][2]["bytes"]=Json::UInt64(32*mib); reject([&]{ure::dualboot_device_commands(bad);},"unsupported-live-filesystem");
        bad=plan; bad["gpt"]["layout"]["rows"][1]["role"]="super"; reject([&]{ure::dualboot_device_commands(bad);},"invalid-dualboot-plan");
        bad=plan; bad["gpt"]["layout"]["rows"][3]["enabled"]=false; bad["gpt"]["layout"]["rows"][3]["bytes"]=Json::UInt64(0); reject([&]{ure::dualboot_device_commands(bad);},"invalid-dualboot-plan");
        bad=plan; bad["gpt"]["layout"]["protected_records"][0]["label"]="changed"; reseal(bad); reject([&]{ure::device_plan(bad);},"protected-record-changed");
        bad=plan; bad["device_commands"][0]["argv"][0]="foreign-option"; reseal(bad); ure::Root system("/");
        reject([&]{ure::dualboot_device_execute(system,bad,work.path/"journal",bad["plan_sha256"].asString(),"ERASE USERDATA");},"stale-dualboot-commands");
        check(!ure::fs::exists(work.path/"journal"),"Refused command preview created a journal");
        reject([&]{ure::dualboot_device_execute(system,plan,work.path/"journal","wrong","ERASE USERDATA");},"confirmation-required");
        check(!ure::fs::exists(work.path/"journal"),"Refused confirmation created a journal");
        ure::StorageTarget target=ure::storage_image(disk,4096); const auto preflight=ure::dualboot_device_preflight(system,target,plan);
        check(preflight["eligible"]==false && preflight["read_only"]==true && preflight["physical_test_record"]==false,"Host preflight falsely authorized a tablet operation");
        const auto partial=work.path/"partial-gpt.bin"; ure::Fd observed(::open(partial.c_str(),O_RDWR|O_CREAT|O_EXCL,0600));
        check(observed.get()>=0 && ::ftruncate(observed.get(),8192)==0,"Cannot create interrupted-region fixture");
        const std::vector<ure::Region> regions{{"primary_table","AAAA","BBBB",4096}}; ure::Value state; state["phase"]="GPT_WRITE"; state["region"]="primary_table";
        ure::write_exact(observed.get(),4096,"AABB");
        check(ure::region_inspect(observed.get(),regions,state)[0]["interrupted_region_allowed"]==true,"Recorded old/new interrupted metadata was rejected");
        state["region"]="backup_table"; reject([&]{ure::region_inspect(observed.get(),regions,state);},"foreign-gpt-change");
        state["region"]="primary_table"; ure::write_exact(observed.get(),4096,"AAXB"); reject([&]{ure::region_inspect(observed.get(),regions,state);},"foreign-gpt-change");
        check(ure::region_order("backup_table")<ure::region_order("backup_header") && ure::region_order("backup_header")<ure::region_order("primary_table") &&
            ure::region_order("primary_table")<ure::region_order("primary_header"),"GPT transaction order does not publish the backup first");
        std::cout<<"Dualboot live admission: exact existing recovery BCB command and unique kernel misc selection, exact F2FS RO/norecovery metadata exception, complete ownership refusal, bounded default-key policy and key disposal, exact formatter preview binding, userdata-only plan controls and host execute refusal passed. No mapper, mount, formatter or block-node operation was performed.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
