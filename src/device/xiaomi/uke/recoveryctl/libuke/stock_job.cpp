// SPDX-License-Identifier: Apache-2.0
// Coordinated regular-image stock restoration. Six LUNs are not an atomic disk.
#include "uke.h"
#include "stock_payloads.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/file.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace ure {
namespace {
constexpr std::uint64_t maximum=512ULL*1024*1024*1024,chunk_size=16*1024*1024,margin=64*1024*1024;
constexpr unsigned chunk_limit=4096;
constexpr const char* algorithm="ure-image-range-sha256-tree-v1";
constexpr std::array<const char*,5> metadata_order{"backup_table","backup_header","primary_table","primary_header","protective_mbr"};
std::string seal(Value value,const char* key) { value.removeMember(key); return sha256(json(value)); }
bool number(const Value& value,std::uint64_t expected) { return value.isUInt64() && value.asUInt64()==expected; }
void fields(const Value& value,std::initializer_list<const char*> allowed) {
    require(value.isObject(),"invalid-stock-request","Stock selections must be objects");
    std::set<std::string> keys; for(const auto* key:allowed)keys.insert(key);
    for(const auto& key:value.getMemberNames())require(keys.contains(key),"invalid-stock-request","Unknown stock selection field: "+key);
}
std::string string(const Value& value) {
    require(value.isString() && !value.asString().empty() && value.asString().size()<=4096,"invalid-stock-request","A bounded nonempty stock selection is required"); return value.asString();
}
const stock_source::Payload& pin(const Value& row) {
    require(row["lun"].isUInt() && row["lun"].asUInt()<6 && row["label"].isString() && row["filename"].isString(),"invalid-stock-request","Select an exact stock LUN, label and filename");
    const auto name=row["label"].asString();
    for(const auto& source:stock_source::global)if(row["filename"]==source.filename) {
        require(row["lun"].asUInt()==source.lun && (source.slotted ? name==std::string(source.label)+"_a" || name==std::string(source.label)+"_b" : name==source.label),
            "protected-stock-payload","Stock payload is not authorized for this LUN and label"); return source;
    }
    throw Error("protected-stock-payload","Only reviewed OS payloads are supported; early firmware and unit-bound partitions stay protected");
}
void request_check(const Value& request) {
    fields(request,{"schema","format","model","sku","firmware_profile","stock_inputs_directory","luns","payloads","erase_android_data","zero_sparse_holes"});
    require(request["schema"]==1 && request["format"]=="ure-stock-job-request" &&
        (request["model"]=="xiaomi-pad-7" || request["model"]=="poco-pad-x1") && request["sku"].isString() &&
        request["sku"].asString().size()<=64 && identifier(request["sku"].asString()),"invalid-stock-request","Select Pad 7 or POCO Pad X1 and an explicit declared SKU tag");
    require(request["firmware_profile"]=="global-os3.0.303.0","wrong-profile","Stock jobs require separately reviewed firmware pins; this backend has Global OS3.0.303.0 only");
    require(fs::path(string(request["stock_inputs_directory"])).is_absolute() && request["luns"].isArray() && request["luns"].size()==6 &&
        request["payloads"].isArray() && request["payloads"].size()<=24 && request["erase_android_data"].isBool() && request["zero_sparse_holes"].isBool(),
        "invalid-stock-request","Select all six image LUNs, reviewed payloads and explicit reset/sparse policies");
    for(unsigned i=0;i<6;++i) {
        const auto& row=request["luns"][i]; fields(row,{"lun","image","identity_backup"});
        require(number(row["lun"],i) && fs::path(string(row["image"])).is_absolute(),"invalid-stock-request","Image LUN rows must be ordered 0 through 5 with absolute paths");
        if(row.isMember("identity_backup"))require(row["identity_backup"].isString() && (row["identity_backup"].asString().empty() || fs::path(row["identity_backup"].asString()).is_absolute()),
            "invalid-stock-request","An original identity backup must have an absolute path");
    }
    std::set<std::pair<unsigned,std::string>> seen; bool metadata=false,userdata=false;
    for(const auto& row:request["payloads"]) {
        fields(row,{"lun","label","filename"}); static_cast<void>(pin(row));
        require(seen.emplace(row["lun"].asUInt(),row["label"].asString()).second,"duplicate-stock-payload","A stock destination can be selected only once");
        metadata=metadata || row["label"]=="metadata"; userdata=userdata || row["label"]=="userdata";
    }
    require(metadata==userdata && metadata==request["erase_android_data"].asBool(),"userdata-reset-pair-required","Reset metadata and userdata together and review Android data loss explicitly");
}
struct Targets {
    std::array<StorageTarget,6> values;
    explicit Targets(const Value& request,bool writable) {
        request_check(request); std::set<std::pair<std::uint64_t,std::uint64_t>> seen;
        for(unsigned i=0;i<6;++i) {
            values[i]=storage_image(request["luns"][i]["image"].asString(),4096,writable); const auto& id=values[i].identity;
            require(id["kind"]=="regular-image" && id["bytes"].asUInt64()<=maximum,"firmware-unverified","Stock live writes require verified model, SKU, firmware and UFS ownership");
            require(seen.emplace(id["file_device"].asUInt64(),id["file_inode"].asUInt64()).second,"duplicate-stock-lun","All six LUNs must be different single-link regular images");
            require(::flock(values[i].descriptor.get(),LOCK_EX|LOCK_NB)==0,"busy-target","A cooperating operation owns a selected LUN image");
        }
    }
    ~Targets() { for(auto& target:values)if(target.descriptor.get()>=0)::flock(target.descriptor.get(),LOCK_UN); }
    Targets(const Targets&)=delete; Targets& operator=(const Targets&)=delete;
};
void binding(const StorageTarget& target,const Value& expected) {
    require(expected["kind"]=="regular-image" && expected["path"].isString() && number(expected["logical_sector_bytes"],4096) && expected["bytes"].isUInt64(),
        "invalid-stock-plan","Stock jobs bind regular 4096-byte-sector images");
    const auto current=storage_image(expected["path"].asString(),4096);
    for(const auto* key:{"path","file_device","file_inode","bytes","logical_sector_bytes","mode","uid","gid"})
        require(json(current.identity[key])==json(expected[key]) && json(target.identity[key])==json(expected[key]),"wrong-target","Stock target path, inode, capacity or ownership differs");
    struct stat st{};
    require(::fstat(target.descriptor.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>=0 &&
        static_cast<std::uint64_t>(st.st_dev)==expected["file_device"].asUInt64() && static_cast<std::uint64_t>(st.st_ino)==expected["file_inode"].asUInt64() &&
        static_cast<std::uint64_t>(st.st_size)==expected["bytes"].asUInt64() && st.st_uid==expected["uid"].asUInt() && st.st_gid==expected["gid"].asUInt() &&
        static_cast<unsigned>(st.st_mode&07777)==expected["mode"].asUInt(),"wrong-target","Retained stock target descriptor differs");
}
void bindings(const Targets& targets,const Value& plan) { for(unsigned i=0;i<6;++i)binding(targets.values[i],plan["luns"][i]["identity"]); }
const Value& named(const Value& rows,std::string_view name) {
    require(rows.isArray(),"invalid-stock-plan","Stock metadata descriptions must be arrays"); const Value* found=nullptr;
    for(const auto& row:rows)if(row["name"].isString() && row["name"].asString()==name) { require(found==nullptr,"invalid-stock-plan","Ambiguous stock metadata description"); found=&row; }
    require(found!=nullptr,"invalid-stock-plan","Stock metadata description is missing"); return *found;
}
const Value& partition(const Value& gpt,std::string_view name) {
    require(gpt["desired_table"]["partitions"].isArray(),"invalid-stock-plan","Stock destinations must be a partition array");
    const Value* found=nullptr; for(const auto& row:gpt["desired_table"]["partitions"])if(row["label"].isString() && row["label"].asString()==name) {
        require(found==nullptr,"invalid-stock-plan","Ambiguous stock partition"); found=&row;
    }
    require(found!=nullptr && found->operator[]("start_lba").isUInt64() && found->operator[]("bytes").isUInt64(),"invalid-stock-plan","Stock destination is missing or unbounded"); return *found;
}
std::string little(std::uint64_t value) { std::string out(8,'\0'); for(unsigned i=0;i<8;++i)out[i]=static_cast<char>((value>>(8*i))&255); return out; }
std::string small_tree(const std::string& hash,std::uint64_t bytes) {
    require(bytes>0 && bytes<=4*1024*1024 && hash_valid(hash),"invalid-stock-plan","Small metadata digest is invalid");
    return sha256(std::string(algorithm)+little(bytes)+little(0)+little(bytes)+hash);
}
std::string file_name(unsigned region,bool after) { return std::string(after ? "after-" : "before-")+std::to_string(region)+".img"; }
void sync(int fd) { require(::fsync(fd)==0,"uncertain-write","Cannot synchronize stock transaction bytes"); }
void write_bytes(int fd,std::string_view bytes,std::uint64_t offset) {
    while(!bytes.empty()) { const auto count=::pwrite(fd,bytes.data(),bytes.size(),static_cast<off_t>(offset));
        if(count<0 && errno==EINTR)continue;
        require(count>0,"io-error","Cannot write stock image bytes"); offset+=static_cast<std::uint64_t>(count); bytes.remove_prefix(static_cast<std::size_t>(count)); }
}
Fd private_file(const Root& store,const std::string& name,std::uint64_t bytes=UINT64_MAX) {
    auto file=store.open(name,O_RDONLY|O_NONBLOCK); struct stat st{};
    require(::fstat(file.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode&07777)==0600 && st.st_size>=0 &&
        (bytes==UINT64_MAX || static_cast<std::uint64_t>(st.st_size)==bytes),"unsafe-stock-journal","Stock recovery files must be private single-link regular files of the reviewed sizes"); return file;
}
Value record(const Root& store,const std::string& name) {
    auto file=private_file(store,name); const auto bytes=storage_bytes(file.get());
    require(bytes>0 && bytes<=4*1024*1024,"size-limit","Stock journal record exceeds its bounded size"); return parse_json(storage_read(file.get(),0,static_cast<std::size_t>(bytes)));
}
Fd journal_lock(const Root& store) {
    auto file=store.open("operation.lock",O_RDWR|O_CREAT,0600); auto checked=private_file(store,"operation.lock",0);
    require(::flock(file.get(),LOCK_EX|LOCK_NB)==0,"operation-busy","Another stock operation owns this journal"); return file;
}
void journal_binding(const Root& store,const fs::path& path) {
    auto current=private_directory(path,false); struct stat before{},after{};
    require(::fstat(store.fd(),&before)==0 && ::fstat(current.fd(),&after)==0 && before.st_dev==after.st_dev && before.st_ino==after.st_ino,
        "changed-journal","Stock journal path was replaced");
}
void phase(const Root& store,Value& state,const char* next) { state["state"]=next; state["timestamp_utc"]=utc(); store.save_record("state.json",state,true); }

Value protected_spans(const Value& plan) {
    Value out(Json::arrayValue);
    for(unsigned lun=0;lun<6;++lun) {
        std::vector<std::pair<std::uint64_t,std::uint64_t>> used;
        const auto capacity=plan["luns"][lun]["identity"]["bytes"].asUInt64();
        for(const auto& row:plan["regions"])if(row["lun"].asUInt()==lun) {
            const auto offset=row["offset"].asUInt64(),bytes=row["bytes"].asUInt64();
            require(bytes>0 && offset<=capacity && bytes<=capacity-offset && offset%4096==0 && bytes%4096==0,
                "invalid-stock-plan","Stock write range exceeds its measured LUN or sector boundaries");
            used.emplace_back(offset,offset+bytes);
        }
        std::sort(used.begin(),used.end()); std::uint64_t cursor=0;
        auto append=[&](std::uint64_t begin,std::uint64_t bytes) { Value row; row["lun"]=lun; row["offset"]=Json::UInt64(begin); row["bytes"]=Json::UInt64(bytes); out.append(row); };
        for(const auto& [begin,end]:used) { require(begin>=cursor,"invalid-stock-plan","Stock write ranges overlap"); if(cursor<begin)append(cursor,begin-cursor); cursor=end; }
        if(cursor<capacity)append(cursor,capacity-cursor);
    }
    return out;
}
void check_plan(const Value& plan) {
    require(plan["schema"]==1 && plan["operation"]=="stock.restore-images" && plan["plan_sha256"].isString() &&
        hash_valid(plan["plan_sha256"].asString()) && plan["plan_sha256"].asString()==seal(plan,"plan_sha256") &&
        plan["operation_id"].isString() && identifier(plan["operation_id"].asString()) && plan["live_write_backend_ready"]==false &&
        plan["physical_test_record"]==false && plan["model_identity_verified"]==false && plan["sku_capacity_verified"]==false &&
        plan["digest_algorithm"]==algorithm && plan["atomic_all_luns"]==false,"invalid-stock-plan","Invalid sealed image-only stock coordinator plan");
    request_check(plan["request"]);
    require(plan["luns"].isArray() && plan["luns"].size()==6 && plan["regions"].isArray() && plan["regions"].size()==plan["request"]["payloads"].size()+30 &&
        number(plan["chunk_bytes"],chunk_size),"invalid-stock-plan","Stock job must bind all six LUNs and every intended write range");
    for(unsigned lun=0;lun<6;++lun) {
        const auto& item=plan["luns"][lun]; const auto& id=item["identity"]; const auto& gpt=item["gpt"];
        require(number(item["lun"],lun) && id["kind"]=="regular-image" && id["path"]==plan["request"]["luns"][lun]["image"] &&
            id["bytes"].isUInt64() && id["bytes"].asUInt64()<=maximum && id["bytes"].asUInt64()>45056 && number(id["logical_sector_bytes"],4096) &&
            gpt["operation"]=="gpt.stock" && number(gpt["stock_lun"],lun) && gpt["firmware_profile"]==plan["request"]["firmware_profile"] &&
            gpt["plan_sha256"].isString() && hash_valid(gpt["plan_sha256"].asString()) && gpt["plan_sha256"].asString()==seal(gpt,"plan_sha256") &&
            json(gpt["target_identity"])==json(id) && gpt["stock_source"]["source_archive_sha256"]==stock_source::archive_sha256 &&
            gpt["desired_table"]["healthy"]==true && number(gpt["desired_table"]["bytes"],id["bytes"].asUInt64()) &&
            gpt["before"].isArray() && gpt["after"].isArray() && gpt["before"].size()==5 && gpt["after"].size()==5,
            "invalid-stock-plan","Stock LUN identity or separately sealed GPT proposal is invalid");
    }
    std::uint64_t total=0,chunks=0; Json::ArrayIndex index=0;
    for(const auto& selected:plan["request"]["payloads"]) {
        const auto& source=pin(selected); const auto& row=plan["regions"][index++]; const auto& part=partition(plan["luns"][source.lun]["gpt"],selected["label"].asString());
        require(row["role"]=="payload" && row["name"]==selected["label"] && row["filename"]==source.filename && number(row["lun"],source.lun) &&
            number(row["source_bytes"],source.source_bytes) && number(row["bytes"],source.expanded_bytes) && row["source_sha256"]==source.sha256 &&
            row["encoding"]==source.encoding && number(row["offset"],part["start_lba"].asUInt64()*4096) && number(row["destination_capacity"],part["bytes"].asUInt64()) &&
            source.expanded_bytes<=part["bytes"].asUInt64() && row["source_observation"]["expanded_digest_algorithm"]==algorithm &&
            row["source_observation"]["expanded_digest"]==row["after_digest"] && number(row["source_observation"]["expanded_bytes"],source.expanded_bytes) &&
            number(row["source_observation"]["source_bytes"],source.source_bytes) && row["source_observation"]["encoding"]==source.encoding &&
            row["source_observation"]["dont_care_bytes"].isUInt64() && (row["source_observation"]["dont_care_bytes"].asUInt64()==0 || plan["request"]["zero_sparse_holes"]==true),
            "invalid-stock-plan","Stock payload programming extent or decoded source binding differs");
    }
    for(const auto* name:metadata_order)for(unsigned lun=0;lun<6;++lun) {
        const auto& gpt=plan["luns"][lun]["gpt"]; const auto& before=named(gpt["before"],name); const auto& after=named(gpt["after"],name); const auto& row=plan["regions"][index++];
        const auto capacity=plan["luns"][lun]["identity"]["bytes"].asUInt64();
        const std::map<std::string,std::pair<std::uint64_t,std::uint64_t>> shape{
            {"backup_table",{capacity-20480,16384}},{"backup_header",{capacity-4096,4096}},{"primary_table",{8192,16384}},{"primary_header",{4096,4096}},{"protective_mbr",{0,4096}}};
        const auto expected=shape.at(name);
        require(row["role"]=="gpt" && row["name"]==name && number(row["lun"],lun) && number(row["offset"],expected.first) && number(row["bytes"],expected.second) &&
            number(before["offset"],expected.first) && number(after["offset"],expected.first) && number(before["bytes"],expected.second) && number(after["bytes"],expected.second) &&
            before["sha256"].isString() && after["sha256"].isString() && hash_valid(before["sha256"].asString()) && hash_valid(after["sha256"].asString()) &&
            row["before_sha256"]==before["sha256"] && row["after_sha256"]==after["sha256"] &&
            row["before_digest"]==small_tree(before["sha256"].asString(),expected.second) && row["after_digest"]==small_tree(after["sha256"].asString(),expected.second),
            "invalid-stock-plan","Stock GPT programming shape differs from its canonical six-LUN geometry");
    }
    for(const auto& row:plan["regions"]) {
        require(row["offset"].isUInt64() && row["bytes"].isUInt64() && row["before_digest"].isString() && row["after_digest"].isString() &&
            hash_valid(row["before_digest"].asString()) && hash_valid(row["after_digest"].asString()),"invalid-stock-plan","Stock range digest is missing");
        total+=row["bytes"].asUInt64(); chunks+=(row["bytes"].asUInt64()+chunk_size-1)/chunk_size;
    }
    require(chunks<=chunk_limit && number(plan["estimated_journal_bytes"],2*total+margin),"invalid-stock-plan","Stock journal budget or bounded chunk count differs");
    const auto protected_rows=protected_spans(plan);
    require(plan["protected"].isArray() && plan["protected"].size()==protected_rows.size(),"invalid-stock-plan","All unprogrammed bytes on every LUN must stay protected");
    for(Json::ArrayIndex i=0;i<protected_rows.size();++i) {
        const auto& row=plan["protected"][i]; for(const auto* key:{"lun","offset","bytes"})require(json(row[key])==json(protected_rows[i][key]),"invalid-stock-plan","Protected stock extent differs");
        require(row["digest"].isString() && hash_valid(row["digest"].asString()),"invalid-stock-plan","Protected stock extent digest is missing");
    }
}
void protected_verify(const Targets& targets,const Value& plan) {
    bindings(targets,plan);
    for(const auto& row:plan["protected"])require(storage_image_range_digest(targets.values[row["lun"].asUInt()].descriptor.get(),row["offset"].asUInt64(),row["bytes"].asUInt64())==row["digest"].asString(),
        "protected-content-changed","An unprogrammed firmware, calibration, tail or other protected range changed");
}
void original_verify(const Targets& targets,const Value& plan) {
    protected_verify(targets,plan);
    for(const auto& row:plan["regions"])require(storage_image_range_digest(targets.values[row["lun"].asUInt()].descriptor.get(),row["offset"].asUInt64(),row["bytes"].asUInt64())==row["before_digest"].asString(),
        "changed-target","Original stock programming bytes changed after review");
}
Fd source_file(const Root& inputs,const Value& row) {
    auto file=inputs.open(row["filename"].asString(),O_RDONLY|O_NONBLOCK); struct stat st{};
    require(::fstat(file.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>=0 &&
        static_cast<std::uint64_t>(st.st_size)==row["source_bytes"].asUInt64() && sha256(file.get())==row["source_sha256"].asString(),
        "stock-source-mismatch","Stock OS input differs from its reviewed size or OEM SHA-256"); return file;
}
void prepare(Targets& targets,const Root& store,const fs::path& path,const Value& plan,Value& state) {
    for(Json::ArrayIndex i=0;i<plan["regions"].size();++i) {
        journal_binding(store,path); const auto& row=plan["regions"][i]; auto before=store.open(file_name(i,false),O_RDWR|O_CREAT|O_EXCL,0600);
        storage_copy_image_range(targets.values[row["lun"].asUInt()].descriptor.get(),before.get(),row["offset"].asUInt64(),row["bytes"].asUInt64());
        require(storage_image_range_digest(before.get(),0,row["bytes"].asUInt64())==row["before_digest"].asString(),"changed-target","Complete original stock range backup differs");
    }
    original_verify(targets,plan); phase(store,state,"BEFORE_VERIFIED");
    std::array<std::vector<StorageRange>,6> metadata;
    for(unsigned lun=0;lun<6;++lun)metadata[lun]=gpt_stock_plan_regions(targets.values[lun],plan["luns"][lun]["gpt"]);
    Root inputs(plan["request"]["stock_inputs_directory"].asString());
    for(Json::ArrayIndex i=0;i<plan["regions"].size();++i) {
        journal_binding(store,path); const auto& row=plan["regions"][i]; auto after=store.open(file_name(i,true),O_RDWR|O_CREAT|O_EXCL,0600);
        if(row["role"]=="payload") {
            auto source=source_file(inputs,row); const auto observation=stock_image_expand(source.get(),after.get(),plan["request"]["zero_sparse_holes"].asBool());
            require(json(observation)==json(row["source_observation"]) && sha256(source.get())==row["source_sha256"].asString(),"stale-source","Stock decoded image differs from its review");
        } else {
            const auto& ranges=metadata[row["lun"].asUInt()]; const StorageRange* wanted=nullptr;
            for(const auto& range:ranges)if(range.name==row["name"].asString())wanted=&range;
            require(wanted!=nullptr && sha256(wanted->bytes)==row["after_sha256"].asString(),"stale-plan","Prepared stock GPT differs from its review");
            write_bytes(after.get(),wanted->bytes,0); sync(after.get());
        }
        require(storage_image_range_digest(after.get(),0,row["bytes"].asUInt64())==row["after_digest"].asString(),"verification-error","Prepared replacement differs from its reviewed logical bytes");
    }
    Value application; application["schema"]=1; application["format"]="ure-stock-application"; application["plan_sha256"]=plan["plan_sha256"];
    application["digest_algorithm"]=algorithm; application["chunks"]=Value(Json::arrayValue);
    for(Json::ArrayIndex i=0;i<plan["regions"].size();++i) {
        const auto& row=plan["regions"][i]; auto before=private_file(store,file_name(i,false),row["bytes"].asUInt64()),after=private_file(store,file_name(i,true),row["bytes"].asUInt64());
        for(std::uint64_t at=0;at<row["bytes"].asUInt64();) {
            const auto bytes=std::min(chunk_size,row["bytes"].asUInt64()-at); Value chunk; chunk["region"]=i; chunk["relative_offset"]=Json::UInt64(at); chunk["bytes"]=Json::UInt64(bytes);
            chunk["before_digest"]=storage_image_range_digest(before.get(),at,bytes); chunk["after_digest"]=storage_image_range_digest(after.get(),at,bytes);
            application["chunks"].append(chunk); at+=bytes;
        }
    }
    require(application["chunks"].size()<=chunk_limit,"size-limit","Stock application exceeds the bounded journal chunk count");
    application["manifest_sha256"]=seal(application,"manifest_sha256"); store.save_record("application.json",application); sync(store.fd());
    original_verify(targets,plan); state["manifest_sha256"]=application["manifest_sha256"]; phase(store,state,"READY");
}

struct Review { Value plan,state,application,result; std::vector<std::string> current; };
bool mixed_expected(int target,int before,int after,std::uint64_t target_offset,std::uint64_t source_offset,std::uint64_t bytes) {
    for(std::uint64_t at=0;at<bytes;) {
        const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes-at));
        const auto now=storage_read(target,target_offset+at,size),old=storage_read(before,source_offset+at,size),next=storage_read(after,source_offset+at,size);
        for(std::size_t i=0;i<size;++i)if(now[i]!=old[i] && now[i]!=next[i])return false;
        at+=size;
    }
    return true;
}
Review inspect(const Targets& targets,const Root& store,const Value& plan) {
    check_plan(plan); protected_verify(targets,plan); Review review; review.plan=plan; review.state=record(store,"state.json");
    require(review.state["schema"]==1 && review.state["plan_sha256"]==plan["plan_sha256"] && review.state["state"].isString(),"invalid-stock-journal","Stock state has another plan binding");
    const std::set<std::string> phases{"STAGING","BEFORE_VERIFIED","READY","APPLYING","ROLLBACK_REQUIRED","VERIFYING","RECOVERY_REQUIRED","COMMITTED","ROLLED_BACK","FAILED_SAFE","CANCELLED_SAFE"};
    require(phases.contains(review.state["state"].asString()),"invalid-stock-journal","Unknown stock journal phase");
    auto& result=review.result; result["operation"]=plan["operation"]; result["plan_sha256"]=plan["plan_sha256"]; result["state"]=review.state["state"];
    result["recovery_actions"]=Value(Json::arrayValue); result["physical_test_record"]=false; result["live_write_backend_ready"]=false;
    result["private_record"]=true; result["atomic_all_luns"]=false; result["protected_ranges_verified"]=true; result["cooperating_locks_only"]=true;
    if(!store.exists("application.json")) {
        original_verify(targets,plan); result["classification"]="ORIGINAL"; result["all_six_originals_verified"]=true; result["before_and_after_verified"]=false;
        if(review.state["state"]!="CANCELLED_SAFE")result["recovery_actions"].append("cancel");
        return review;
    }
    review.application=record(store,"application.json"); const auto& app=review.application;
    require(app["schema"]==1 && app["format"]=="ure-stock-application" && app["plan_sha256"]==plan["plan_sha256"] && app["digest_algorithm"]==algorithm &&
        app["manifest_sha256"].isString() && hash_valid(app["manifest_sha256"].asString()) && app["manifest_sha256"].asString()==seal(app,"manifest_sha256") &&
        app["chunks"].isArray() && !app["chunks"].empty() && app["chunks"].size()<=chunk_limit,"invalid-stock-journal","Invalid sealed stock application");
    bool all_before=true,all_after=true,expected=true; Json::ArrayIndex chunk_index=0; result["chunks"]=Value(Json::arrayValue);
    for(Json::ArrayIndex region=0;region<plan["regions"].size();++region) {
        const auto& row=plan["regions"][region]; const auto bytes=row["bytes"].asUInt64();
        auto before=private_file(store,file_name(region,false),bytes),after=private_file(store,file_name(region,true),bytes);
        require(storage_image_range_digest(before.get(),0,bytes)==row["before_digest"].asString() &&
            storage_image_range_digest(after.get(),0,bytes)==row["after_digest"].asString(),"backup-corrupt","Complete stock original or decoded replacement mirror differs");
        if(row["role"]=="gpt")require(sha256(before.get())==row["before_sha256"].asString() && sha256(after.get())==row["after_sha256"].asString(),"backup-corrupt","Stock GPT mirror differs from its reviewed metadata");
        for(std::uint64_t at=0;at<bytes;) {
            const auto size=std::min(chunk_size,bytes-at); require(chunk_index<app["chunks"].size(),"invalid-stock-journal","Stock application is incomplete");
            const auto& chunk=app["chunks"][chunk_index++];
            require(number(chunk["region"],region) && number(chunk["relative_offset"],at) && number(chunk["bytes"],size) &&
                chunk["before_digest"].isString() && chunk["after_digest"].isString() &&
                storage_image_range_digest(before.get(),at,size)==chunk["before_digest"].asString() &&
                storage_image_range_digest(after.get(),at,size)==chunk["after_digest"].asString(),"invalid-stock-journal","Stock chunks do not cover the exact reviewed ranges");
            const auto descriptor=targets.values[row["lun"].asUInt()].descriptor.get();
            const auto now=storage_image_range_digest(descriptor,row["offset"].asUInt64()+at,size); review.current.push_back(now);
            const bool original=now==chunk["before_digest"].asString(),desired=now==chunk["after_digest"].asString();
            all_before=all_before && original; all_after=all_after && desired;
            const bool known=original || desired || mixed_expected(descriptor,before.get(),after.get(),row["offset"].asUInt64()+at,at,size);
            expected=expected && known; Value observed; observed["region"]=region; observed["lun"]=row["lun"]; observed["name"]=row["name"]; observed["relative_offset"]=Json::UInt64(at);
            observed["classification"]=original && desired ? "UNCHANGED" : original ? "ORIGINAL" : desired ? "TARGET" : known ? "EXPECTED_PARTIAL_WRITE" : "DIVERGED";
            result["chunks"].append(observed); at+=size;
        }
    }
    require(chunk_index==app["chunks"].size(),"invalid-stock-journal","Stock application contains extra chunks");
    result["before_and_after_verified"]=true; result["all_six_originals_verified"]=true;
    result["classification"]=!expected ? "DIVERGED" : all_before && all_after ? "UNCHANGED" : all_before ? "ORIGINAL" : all_after ? "TARGET_CONTENT_VERIFIED" : "EXPECTED_PARTIAL_WRITE";
    const auto state=review.state["state"].asString();
    if(expected && state!="ROLLED_BACK" && state!="CANCELLED_SAFE") {
        if(state!="ROLLBACK_REQUIRED" && review.state["direction"]!="rollback")result["recovery_actions"].append("resume");
        result["recovery_actions"].append("rollback");
    }
    return review;
}
bool action(const Value& result,const std::string& name) { for(const auto& value:result["recovery_actions"])if(value==name)return true; return false; }
Value apply(Targets& targets,const Root& store,const fs::path& path,Review review,bool rollback) {
    auto state=review.state; state["direction"]=rollback ? "rollback" : "apply"; state["written_bytes"]=Json::UInt64(0); state["verified"]=false;
    try {
        protected_verify(targets,review.plan); phase(store,state,rollback ? "ROLLBACK_REQUIRED" : "APPLYING");
        for(Json::ArrayIndex i=0;i<review.application["chunks"].size();++i) {
            journal_binding(store,path); bindings(targets,review.plan); const auto& chunk=review.application["chunks"][i]; const auto& row=review.plan["regions"][chunk["region"].asUInt()];
            auto& target=targets.values[row["lun"].asUInt()]; storage_write_gate(target);
            const auto bytes=chunk["bytes"].asUInt64(),relative=chunk["relative_offset"].asUInt64(),offset=row["offset"].asUInt64()+relative;
            const auto wanted=chunk[rollback ? "before_digest" : "after_digest"].asString();
            const auto now=storage_image_range_digest(target.descriptor.get(),offset,bytes);
            require(now==review.current[i],"changed-target","A stock range changed after recovery inspection");
            if(now!=wanted) {
                auto source=private_file(store,file_name(chunk["region"].asUInt(),!rollback),row["bytes"].asUInt64());
                require(storage_image_range_digest(source.get(),relative,bytes)==wanted,"backup-corrupt","Stock recovery input changed before writing");
                state["active_chunk"]=i; state["active_lun"]=row["lun"]; phase(store,state,rollback ? "ROLLBACK_REQUIRED" : "APPLYING");
                for(std::uint64_t at=0;at<bytes;) {
                    const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(65536,bytes-at)); const auto data=storage_read(source.get(),relative+at,size);
                    // Skip only independently read equal bytes, including zeros.
                    // Sparse allocation never establishes existing target content.
                    if(data!=storage_read(target.descriptor.get(),offset+at,size)) {
                        write_bytes(target.descriptor.get(),data,offset+at); state["written_bytes"]=Json::UInt64(state["written_bytes"].asUInt64()+size);
                    }
                    at+=size;
                }
                sync(target.descriptor.get()); require(storage_image_range_digest(target.descriptor.get(),offset,bytes)==wanted,"verification-error","Stock range readback differs");
            }
            state.removeMember("active_chunk"); state.removeMember("active_lun"); state["last_verified_chunk"]=i; phase(store,state,rollback ? "ROLLBACK_REQUIRED" : "APPLYING");
        }
        phase(store,state,"VERIFYING"); journal_binding(store,path); protected_verify(targets,review.plan);
        for(const auto& row:review.plan["regions"])require(storage_image_range_digest(targets.values[row["lun"].asUInt()].descriptor.get(),row["offset"].asUInt64(),row["bytes"].asUInt64())==row[rollback ? "before_digest" : "after_digest"].asString(),
            "verification-error","Complete stock range differs after application");
        for(unsigned lun=0;lun<6;++lun)require(json(gpt_inspect(targets.values[lun].descriptor.get(),4096))==json(review.plan["luns"][lun]["gpt"][rollback ? "current_table" : "desired_table"]),
            "verification-error","A final LUN GPT differs from the reviewed complete table");
        state["verified"]=true; state["all_six_luns_verified"]=true; state["protected_ranges_verified"]=true; state["complete_stock_image_job"]=!rollback;
        state["physical_test_record"]=false; state["live_write_backend_ready"]=false; state["atomic_all_luns"]=false;
        phase(store,state,rollback ? "ROLLED_BACK" : "COMMITTED"); return state;
    } catch(const Error& error) { state["error_code"]=error.code; state["verified"]=false; try { phase(store,state,"RECOVERY_REQUIRED"); } catch(...) {} throw; }
}
} // namespace

Value stock_job_plan(const Value& request) {
    request_check(request); Value chosen=request;
    chosen["stock_inputs_directory"]=fs::path(chosen["stock_inputs_directory"].asString()).lexically_normal().string();
    for(auto& row:chosen["luns"]) {
        row["image"]=fs::path(row["image"].asString()).lexically_normal().string();
        if(row.isMember("identity_backup") && !row["identity_backup"].asString().empty())row["identity_backup"]=fs::path(row["identity_backup"].asString()).lexically_normal().string();
    }
    Targets targets(chosen,false); Value plan; plan["schema"]=1; plan["operation"]="stock.restore-images"; plan["operation_id"]=operation_id(); plan["created_utc"]=utc(); plan["request"]=chosen;
    plan["luns"]=Value(Json::arrayValue); plan["regions"]=Value(Json::arrayValue);
    for(unsigned lun=0;lun<6;++lun) {
        const auto& row=chosen["luns"][lun]; Value item; item["lun"]=lun; item["identity"]=targets.values[lun].identity;
        item["gpt"]=gpt_stock_plan(targets.values[lun],chosen["stock_inputs_directory"].asString(),lun,chosen["firmware_profile"].asString(),
            row.isMember("identity_backup") ? fs::path(row["identity_backup"].asString()) : fs::path());
        plan["luns"].append(item);
    }
    Root inputs(chosen["stock_inputs_directory"].asString());
    for(const auto& selected:chosen["payloads"]) {
        const auto& source=pin(selected); const auto& destination=partition(plan["luns"][source.lun]["gpt"],selected["label"].asString());
        require(source.expanded_bytes<=destination["bytes"].asUInt64(),"stock-capacity-mismatch","Expanded stock payload exceeds the measured destination; ROM userdata size is not device capacity");
        Value row; row["role"]="payload"; row["name"]=selected["label"]; row["lun"]=source.lun; row["filename"]=source.filename;
        row["source_bytes"]=Json::UInt64(source.source_bytes); row["source_sha256"]=source.sha256; row["encoding"]=source.encoding;
        row["offset"]=Json::UInt64(destination["start_lba"].asUInt64()*4096); row["bytes"]=Json::UInt64(source.expanded_bytes); row["destination_capacity"]=destination["bytes"];
        auto file=source_file(inputs,row); row["source_observation"]=stock_image_inspect(file.get());
        require(number(row["source_observation"]["expanded_bytes"],source.expanded_bytes) && row["source_observation"]["encoding"]==source.encoding,
            "stock-source-mismatch","Pinned payload expansion differs from its separately reviewed source contract");
        require(row["source_observation"]["dont_care_bytes"].asUInt64()==0 || chosen["zero_sparse_holes"]==true,"sparse-zero-policy-required","Review zero filling of sparse stock DONT_CARE ranges explicitly");
        row["after_digest"]=row["source_observation"]["expanded_digest"];
        row["before_digest"]=storage_image_range_digest(targets.values[source.lun].descriptor.get(),row["offset"].asUInt64(),source.expanded_bytes);
        plan["regions"].append(row);
    }
    for(const auto* name:metadata_order)for(unsigned lun=0;lun<6;++lun) {
        const auto& gpt=plan["luns"][lun]["gpt"]; const auto& before=named(gpt["before"],name); const auto& after=named(gpt["after"],name);
        Value row; row["role"]="gpt"; row["name"]=name; row["lun"]=lun; row["offset"]=after["offset"]; row["bytes"]=after["bytes"];
        row["before_sha256"]=before["sha256"]; row["after_sha256"]=after["sha256"];
        row["before_digest"]=small_tree(before["sha256"].asString(),row["bytes"].asUInt64()); row["after_digest"]=small_tree(after["sha256"].asString(),row["bytes"].asUInt64()); plan["regions"].append(row);
    }
    plan["protected"]=protected_spans(plan);
    for(auto& row:plan["protected"])row["digest"]=storage_image_range_digest(targets.values[row["lun"].asUInt()].descriptor.get(),row["offset"].asUInt64(),row["bytes"].asUInt64());
    std::uint64_t total=0; for(const auto& row:plan["regions"])total+=row["bytes"].asUInt64();
    plan["estimated_journal_bytes"]=Json::UInt64(total*2+margin); plan["chunk_bytes"]=Json::UInt64(chunk_size); plan["digest_algorithm"]=algorithm;
    plan["model_identity_verified"]=false; plan["sku_capacity_verified"]=false; plan["live_write_backend_ready"]=false; plan["physical_test_record"]=false;
    plan["android_boot_compatibility_verified"]=false; plan["private_record"]=true; plan["atomic_all_luns"]=false; plan["interrupt_scenario"]="FORCED_REBOOT";
    plan["risk"]=chosen["erase_android_data"]==true ? "RESET_ANDROID_METADATA_AND_USERDATA_AND_RESTORE_STOCK_TABLES" : chosen["payloads"].empty() ? "RESTORE_SIX_STOCK_TABLES_DATA_RETAINED" : "RESTORE_SELECTED_STOCK_OS_CONTENTS_AND_SIX_TABLES";
    plan["warnings"]=Value(Json::arrayValue);
    plan["warnings"].append("All six originals and decoded replacements are verified before the first target write. This coordinates recovery; it does not make six LUNs atomic.");
    plan["warnings"].append("Pad 7/POCO Pad X1 and SKU tags are declarations for image fixtures. Installed model, firmware, capacity profile, Android trust and physical rollback are unverified; live writes stay blocked.");
    plan["warnings"].append("Only selected OS payload programming extents are overwritten. Unselected slot contents, early firmware, calibration, unit-bound data and unprogrammed tails stay byte-identical.");
    plan["warnings"].append("Stock table restoration removes custom OS partition visibility. Their bytes remain unless they intersect an explicitly selected OS payload extent. Slot selection and boot registration are separate operations.");
    if(chosen["erase_android_data"]==true)plan["warnings"].append("Metadata and userdata are replaced together. Sparse DONT_CARE bytes in the reviewed programming extent are zeroed. The rest of a larger userdata partition remains protected; this is not secure erasure or verified Android boot compatibility.");
    plan["plan_sha256"]=seal(plan,"plan_sha256"); check_plan(plan); original_verify(targets,plan);
    for(const auto& target:targets.values)storage_revalidate(target);
    return plan;
}
Value stock_job_execute(const Value& plan,const fs::path& path,const std::string& confirmation) {
    check_plan(plan); require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the complete six-LUN stock job hash");
    Targets targets(plan["request"],true);
    for(unsigned lun=0;lun<6;++lun) {
        require(json(targets.values[lun].identity)==json(plan["luns"][lun]["identity"]),"stale-plan","A stock LUN selection changed since review"); storage_write_gate(targets.values[lun]);
        const auto& row=plan["request"]["luns"][lun];
        const auto rebuilt=gpt_stock_plan(targets.values[lun],plan["request"]["stock_inputs_directory"].asString(),lun,plan["request"]["firmware_profile"].asString(),
            row.isMember("identity_backup") ? fs::path(row["identity_backup"].asString()) : fs::path());
        for(const auto* key:{"before","after","current_table","desired_table","stock_source"})require(json(rebuilt[key])==json(plan["luns"][lun]["gpt"][key]),"stale-plan","Stock GPT policy or original identity changed after review");
    }
    original_verify(targets,plan); auto store=private_directory(path,true); auto lock=journal_lock(store); store.save_record("plan.json",plan);
    Value state; state["schema"]=1; state["plan_sha256"]=plan["plan_sha256"]; state["direction"]="apply"; state["state"]="STAGING"; state["verified"]=false; store.save_record("state.json",state);
    try {
        struct statvfs space{}; require(::fstatvfs(store.fd(),&space)==0 && space.f_frsize && space.f_bavail>plan["estimated_journal_bytes"].asUInt64()/space.f_frsize,
            "insufficient-space","Keep space for complete original and decoded replacement ranges plus the journal margin");
        prepare(targets,store,path,plan,state); return apply(targets,store,path,inspect(targets,store,plan),false);
    } catch(const Error& error) {
        if(!store.exists("application.json")) { state["error_code"]=error.code; try { phase(store,state,"FAILED_SAFE"); } catch(...) {} }
        throw;
    }
}
Value stock_job_recover(const fs::path& path,const std::string& operation,const std::string& confirmation) {
    require(operation=="inspect" || operation=="resume" || operation=="rollback" || operation=="cancel","unsupported-stock-operation","Select stock inspection, resume, rollback or cancel");
    auto store=private_directory(path,false); auto lock=journal_lock(store); const auto plan=record(store,"plan.json"); check_plan(plan);
    Targets targets(plan["request"],operation=="resume" || operation=="rollback"); auto review=inspect(targets,store,plan);
    if(operation=="inspect")return review.result;
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact stock job plan hash");
    require(action(review.result,operation),"unsafe-recovery","Actual bytes do not authorize this action; unrelated divergence requires investigation");
    journal_binding(store,path);
    if(operation=="cancel") { auto state=review.state; state["original_unchanged_verified"]=true; phase(store,state,"CANCELLED_SAFE"); return state; }
    return apply(targets,store,path,std::move(review),operation=="rollback");
}
} // namespace ure
