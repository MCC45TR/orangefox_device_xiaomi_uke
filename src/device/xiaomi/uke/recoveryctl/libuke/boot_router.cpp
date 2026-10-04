// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "operation_guard.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fcntl.h>
#include <linux/fs.h>
#include <set>
#include <sys/file.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ure {
namespace {
constexpr const char* global_guid="8be4df61-93ca-11d2-aa0d-00e098032b8c";
constexpr std::size_t variable_limit=65536;
std::string variable(const std::string& name) { return name+"-"+global_guid; }
std::uint64_t le(std::string_view bytes,std::size_t at,unsigned count) {
    require(count<=8 && at<=bytes.size() && count<=bytes.size()-at,"invalid-efi-variable","Truncated EFI field");
    std::uint64_t result=0;
    for(unsigned i=0;i<count;++i)result|=static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[at+i]))<<(i*8);
    return result;
}
std::string hex(std::uint64_t value,unsigned digits) {
    const char* alphabet="0123456789abcdef"; std::string out(digits,'0');
    for(unsigned i=0;i<digits;++i)out[digits-1-i]=alphabet[(value>>(i*4))&15];
    return out;
}
std::string option_name(const std::string& number) {
    require(number.size()==4 && std::all_of(number.begin(),number.end(),[](unsigned char c) { return std::isxdigit(c)!=0; }),
        "invalid-boot-option","Use an exact four-digit hexadecimal EFI option number");
    std::string canonical=number; std::transform(canonical.begin(),canonical.end(),canonical.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return canonical;
}
std::uint64_t option_number(const std::string& number) {
    std::uint64_t out=0;
    for(const auto c:option_name(number))out=(out<<4)|static_cast<unsigned>(c<='9' ? c-'0' : c-'a'+10);
    return out;
}
std::string option_variable(const std::string& number) {
    auto name=option_name(number); std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return variable("Boot"+name);
}
std::string next_bytes(const std::string& number) {
    const auto value=option_number(number); std::string out(6,'\0'); out[0]=7;
    out[4]=static_cast<char>(value&255); out[5]=static_cast<char>((value>>8)&255); return out;
}
std::string guid(std::string_view bytes,std::size_t at) {
    return hex(le(bytes,at,4),8)+"-"+hex(le(bytes,at+4,2),4)+"-"+hex(le(bytes,at+6,2),4)+"-"+
        hex(le(bytes,at+8,1),2)+hex(le(bytes,at+9,1),2)+"-"+hex(le(bytes,at+10,1),2)+hex(le(bytes,at+11,1),2)+
        hex(le(bytes,at+12,1),2)+hex(le(bytes,at+13,1),2)+hex(le(bytes,at+14,1),2)+hex(le(bytes,at+15,1),2);
}
std::string ascii_utf16(std::string_view bytes,std::size_t& at,std::size_t end) {
    std::string result;
    while(at+2<=end) {
        const auto code=le(bytes,at,2); at+=2;
        if(code==0)return result;
        require(code>=32 && code<=126 && result.size()<1024,"unsupported-efi-path","Only bounded printable ASCII EFI descriptions and paths are supported");
        result.push_back(static_cast<char>(code));
    }
    throw Error("invalid-efi-variable","Unterminated EFI UTF-16 string");
}
Value load_option(const std::string& bytes,const std::string& number) {
    require(bytes.size()>=16 && le(bytes,0,4)==7,"invalid-efi-variable","EFI load options require NV/BS/RT variable attributes");
    const auto attributes=le(bytes,4,4); require((attributes&1)!=0 && (attributes&~std::uint64_t{3})==0,
        "inactive-boot-option","Select an active boot option with supported attributes; hidden, non-boot and reserved bits are refused");
    std::size_t at=10; Value out; out["number"]=number; out["description"]=ascii_utf16(bytes,at,bytes.size());
    const auto path_bytes=le(bytes,8,2); require(path_bytes>=4 && path_bytes<=bytes.size()-at,"invalid-efi-variable","Invalid EFI device-path length");
    const auto end=at+static_cast<std::size_t>(path_bytes); bool ended=false,hd=false,file=false;
    while(at<end) {
        require(end-at>=4,"invalid-efi-variable","Truncated device-path node");
        const auto type=le(bytes,at,1),subtype=le(bytes,at+1,1),size=le(bytes,at+2,2);
        require(size>=4 && size<=end-at,"invalid-efi-variable","Invalid device-path node size");
        if(type==0x7f) {
            require(subtype==0xff && size==4 && at+size==end,"unsupported-efi-path","Multiple-instance or trailing EFI paths are not supported"); ended=true;
        } else if(type==4 && subtype==1) {
            require(!hd && !file && size==42 && le(bytes,at+40,1)==2 && le(bytes,at+41,1)==2 &&
                le(bytes,at+4,4)>0 && le(bytes,at+16,8)>0,"unsupported-efi-path","Select a single GPT hard-drive device path");
            hd=true; out["esp_partuuid"]=guid(bytes,at+24);
            out["partition_number"]=Json::UInt64(le(bytes,at+4,4));
            out["partition_start_lba"]=Json::UInt64(le(bytes,at+8,8)); out["partition_size_lba"]=Json::UInt64(le(bytes,at+16,8));
        } else if(type==4 && subtype==4) {
            require(hd && !file && size>=8 && size%2==0,"unsupported-efi-path","A single file path must follow the GPT node");
            file=true; auto pos=at+4; auto path=ascii_utf16(bytes,pos,at+static_cast<std::size_t>(size));
            require(pos==at+size && !path.empty() && path.front()=='\\',"unsupported-efi-path","Use one absolute ESP file path");
            std::replace(path.begin(),path.end(),'\\','/'); path.erase(0,1); components(path);
            require(fs::path(path).lexically_normal().generic_string()==path,"unsupported-efi-path","Non-canonical EFI paths are refused"); out["loader"]=path;
        } else require(!hd && (type==1 || type==2 || type==3),"unsupported-efi-path","Unknown or out-of-order device-path node");
        at+=static_cast<std::size_t>(size);
    }
    require(ended && hd && file,"unsupported-efi-path","A complete GPT/file/end device path is required");
    // OptionalData is deliberately opaque. Its digest binds it to this request.
    out["optional_data_bytes"]=Json::UInt64(bytes.size()-end); out["variable_sha256"]=sha256(bytes); out["variable_bytes"]=Json::UInt64(bytes.size());
    return out;
}
Value loader(const Root& esp,const Value& option) {
    auto fd=esp.open(option["loader"].asString(),O_RDONLY|O_NONBLOCK); struct stat before{},after{};
    require(::fstat(fd.get(),&before)==0 && S_ISREG(before.st_mode) && before.st_nlink==1 && before.st_size>=256 &&
        before.st_size<=512LL*1024*1024,"invalid-efi-loader","EFI loader must be a bounded, single-link regular file");
    std::array<char,64> dos{};
    require(::pread(fd.get(),dos.data(),dos.size(),0)==static_cast<ssize_t>(dos.size()) && dos[0]=='M' && dos[1]=='Z',
        "invalid-efi-loader","EFI loader is not a DOS/PE image");
    const auto pe=le(std::string_view(dos.data(),dos.size()),60,4); std::array<char,94> header{};
    require(pe>=64 && pe<=1024*1024 && pe+header.size()<=static_cast<std::uint64_t>(before.st_size) &&
        ::pread(fd.get(),header.data(),header.size(),static_cast<off_t>(pe))==static_cast<ssize_t>(header.size()),"invalid-efi-loader","Truncated PE header");
    const std::string_view data(header.data(),header.size());
    require(data.substr(0,4)==std::string_view("PE\0\0",4) && le(data,4,2)==0xaa64 && le(data,20,2)>=112 &&
        pe+24+le(data,20,2)<=static_cast<std::uint64_t>(before.st_size) && le(data,24,2)==0x20b && le(data,92,2)==10,
        "invalid-efi-loader","Select an AArch64 PE32+ EFI application");
    Value result; result["sha256"]=sha256(fd.get()); result["bytes"]=Json::Int64(before.st_size); result["architecture"]="aarch64";
    require(::fstat(fd.get(),&after)==0 && before.st_dev==after.st_dev && before.st_ino==after.st_ino && before.st_size==after.st_size &&
        before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec &&
        before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec,
        "stale-boot-assets","EFI loader changed while hashing");
    const auto path=option["loader"].asString();
    result["target"]=path=="EFI/Microsoft/Boot/bootmgfw.efi" ? "windows" : path=="EFI/UKE/android.efi" ? "android" :
        path.starts_with("EFI/Linux/") && path.ends_with(".efi") ? "linux" : "unknown";
    result["signature_validated"]=false; result["boot_validated"]=false; return result;
}
bool fixture(const Root& variables) {
    struct stat st{}; struct statfs fsinfo{};
    if(::fstat(variables.fd(),&st)!=0 || ::fstatfs(variables.fd(),&fsinfo)!=0 ||
        st.st_uid!=::geteuid() || (st.st_mode&07777)!=0700 || static_cast<unsigned long>(fsinfo.f_type)==0xde5e81e4UL)return false;
    if(!variables.exists(".ure-efi-fixture.json"))return false;
    auto marker=variables.open(".ure-efi-fixture.json",O_RDONLY|O_NONBLOCK); struct stat info{};
    require(::fstat(marker.get(),&info)==0 && S_ISREG(info.st_mode) && info.st_nlink==1 && info.st_uid==::geteuid() &&
        (info.st_mode&0777)==0600,"unsafe-efi-fixture","Fixture marker must be a private single-link file");
    const auto record=parse_json(variables.read(".ure-efi-fixture.json",1024));
    require(record.isObject() && record.size()==2 && record["schema"]==1 && record["kind"]=="ure-uefi-variable-fixture",
        "unsafe-efi-fixture","Unrecognized EFI fixture marker"); return true;
}
void write_gate(const Root& variables) {
    require(fixture(variables),"boot-backend-unverified","No accepted Uke/Aloha routing backend exists. Only private regular-file EFI fixtures may be changed; real EFI variables and reboot remain blocked");
}
Fd lock(const Root& root,bool create=true) {
    auto fd=root.open(".ure-boot-lock",(create ? O_RDWR|O_CREAT : O_RDONLY)|O_NONBLOCK,0600); struct stat info{};
    require(::fstat(fd.get(),&info)==0 && S_ISREG(info.st_mode) && info.st_nlink==1 && info.st_uid==::geteuid() &&
        (info.st_mode&0777)==0600,"unsafe-boot-lock","Boot lock must be a private single-link file");
    require(::flock(fd.get(),LOCK_EX|LOCK_NB)==0,"boot-busy","Another operation owns this boot store"); return fd;
}
std::string digest(Value value,const char* field) { value.removeMember(field); return sha256(json(value)); }
void check_plan(const Value& plan) {
    require(plan.isObject() && plan["schema"]==1 && plan["kind"]=="ure-uefi-one-shot" && plan["one_shot"]==true &&
        plan["max_attempts"]==1 && plan["physical_device_write_allowed"]==false && identifier(plan["operation_id"].asString()) &&
        hash_valid(plan["plan_sha256"].asString()) && digest(plan,"plan_sha256")==plan["plan_sha256"].asString(),
        "invalid-boot-plan","Invalid, modified or unsupported one-shot boot plan");
    option_name(plan["selected"]["number"].asString()); option_name(plan["fallback"]["number"].asString());
}
Value read_record(const Root& store,const std::string& name) {
    auto fd=store.open(name,O_RDONLY|O_NONBLOCK); struct stat st{};
    require(::fstat(fd.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==::geteuid() && (st.st_mode&0777)==0600,
        "unsafe-boot-record","Boot records must be private single-link files"); return parse_json(store.read(name,4*1024*1024));
}
std::string owner_state(const Root& variables,const Value& plan) {
    if(!variables.exists(".ure-boot-owner.json"))return "absent";
    const auto owner=read_record(variables,".ure-boot-owner.json");
    require(owner.isObject() && owner.size()==3 && owner["schema"]==1 && owner["request_id"].isString() &&
        hash_valid(owner["plan_sha256"].asString()),"invalid-boot-owner","Incomplete boot ownership record must be inspected before any new request");
    return owner["request_id"]==plan["operation_id"] && owner["plan_sha256"]==plan["plan_sha256"] ? "owned" : "foreign";
}
std::string retirement_name(const Value& plan) { return ".ure-boot-retired-"+plan["operation_id"].asString()+".json"; }
void release_owner(const Root& variables,const Value& plan) {
    const auto owner=owner_state(variables,plan); if(owner=="absent")return;
    require(owner=="owned","boot-request-conflict","Another request owns the variable store");
    // Keep the request ID retired before releasing exclusive ownership. A
    // different journal directory must not permit a second use of this plan.
    const auto name=retirement_name(plan);
    if(variables.exists(name)) {
        const auto retired=read_record(variables,name);
        require(retired.isObject() && retired.size()==4 && retired["schema"]==1 && retired["kind"]=="ure-uefi-retired-request" &&
            retired["request_id"]==plan["operation_id"] && retired["plan_sha256"]==plan["plan_sha256"],
            "invalid-boot-retirement","Retirement record must match the owned request before ownership can be released");
    } else {
        Value retired; retired["schema"]=1; retired["kind"]="ure-uefi-retired-request";
        retired["request_id"]=plan["operation_id"]; retired["plan_sha256"]=plan["plan_sha256"];
        // Publishing a newly created JSON file directly can expose an empty
        // retirement record after SIGKILL. Sync a private temporary record,
        // then publish it atomically without replacing any existing record.
        const auto temporary=".ure-boot-retiring-"+operation_id()+".json";
        variables.save_record(temporary,retired);
        require(::syscall(SYS_renameat2,variables.fd(),temporary.c_str(),variables.fd(),name.c_str(),RENAME_NOREPLACE)==0 &&
            ::fsync(variables.fd())==0,"uncertain-boot-retirement","Cannot atomically retain the retirement record; ownership stays held for inspection");
    }
    require(::unlinkat(variables.fd(),".ure-boot-owner.json",0)==0 && ::fsync(variables.fd())==0,"uncertain-boot-owner","Cannot durably release this fixture request");
}
Value state(const Root& store,const Value& plan) {
    auto out=read_record(store,"state.json");
    static const std::set<std::string> phases{"ARMING","ARMED","CONSUMING","CONSUMED","ACKNOWLEDGED","UNKNOWN","CANCELLING","CANCELLED","FALLBACK_PENDING","FALLBACK_SELECTED"};
    require(out["schema"]==1 && out["request_id"]==plan["operation_id"] && out["plan_sha256"]==plan["plan_sha256"] &&
        out["evidence_scope"]=="regular-file-fixture" && out["physical_boot_success"]==false && phases.contains(out["phase"].asString()) &&
        out["events"].isArray() && !out["events"].empty() && out["events"].size()<=64 && out["state_sha256"]==digest(out,"state_sha256"),
        "invalid-boot-state","Boot state is inconsistent or corrupted");
    unsigned sequence=0; std::string previous;
    for(const auto& event:out["events"]) {
        require(event["sequence"].asUInt()==++sequence && event["request_id"]==plan["operation_id"] && event["previous_sha256"]==previous &&
            event["event_sha256"]==digest(event,"event_sha256"),"invalid-boot-state","Broken boot history sequence or digest chain"); previous=event["event_sha256"].asString();
    }
    require(out["events"][out["events"].size()-1]["phase"]==out["phase"],"invalid-boot-state","Boot phase and final event disagree"); return out;
}
void event(const Root& store,Value& current,const std::string& phase,const std::string& reason) {
    require(current["events"].size()<64,"boot-history-full","Boot event limit reached; retain this journal and start a separately reviewed request");
    Value item; item["sequence"]=current["events"].size()+1; item["request_id"]=current["request_id"]; item["phase"]=phase;
    item["created_utc"]=utc(); item["reason"]=reason; item["attempt_id"]=current.get("attempt_id","");
    item["previous_sha256"]=current["events"].empty() ? "" : current["events"][current["events"].size()-1]["event_sha256"].asString();
    item["event_sha256"]=digest(item,"event_sha256"); current["events"].append(item); current["phase"]=phase;
    current["state_sha256"]=digest(current,"state_sha256"); store.save_record("state.json",current,true);
}
bool same_directory(const Root& root,const Value& identity) { return json(descriptor_identity(root.fd()))==json(identity); }
void validate_bindings(const Root& esp,const Root& variables,const Value& plan) {
    check_plan(plan); write_gate(variables);
    require(same_directory(esp,plan["esp_identity"]) && same_directory(variables,plan["variables_identity"]),"stale-boot-context","ESP or variable-store identity changed");
    require(sha256(variables.read(variable("BootOrder"),variable_limit))==plan["boot_order_sha256"].asString(),"stale-boot-default","BootOrder changed; a new explicit review is required");
    for(const auto* key:{"selected","fallback"}) {
        const auto& entry=plan[key]; const auto bytes=variables.read(option_variable(entry["number"].asString()),variable_limit);
        require(sha256(bytes)==entry["variable_sha256"].asString(),"stale-boot-assets","EFI option changed since review");
        const auto inspected=load_option(bytes,entry["number"].asString());
        require(inspected["loader"]==entry["loader"] && inspected["esp_partuuid"]==plan["esp_partuuid"] &&
            json(loader(esp,inspected))==json(entry["loader_identity"]),"stale-boot-assets","Selected or fallback EFI loader changed since review");
    }
}
std::string next_state(const Root& variables,const Value& plan) {
    if(!variables.exists(variable("BootNext")))return "absent";
    return variables.read(variable("BootNext"),variable_limit)==next_bytes(plan["selected"]["number"].asString()) ? "owned" : "foreign";
}
void arm(const Root& variables,const Value& plan) {
    require(!variables.exists(variable("BootNext")),"boot-next-conflict","An existing BootNext request must be resolved by its owner");
    auto output=variables.open(variable("BootNext"),O_WRONLY|O_CREAT|O_EXCL,0600);
    const auto bytes=next_bytes(plan["selected"]["number"].asString());
    require(::write(output.get(),bytes.data(),bytes.size())==static_cast<ssize_t>(bytes.size()) && ::fsync(output.get())==0 && ::fsync(variables.fd())==0,
        "uncertain-boot-arm","Cannot durably arm the fixture BootNext; inspect the journal before proceeding");
    require(next_state(variables,plan)=="owned","uncertain-boot-arm","BootNext readback differs from the reviewed option");
}
void remove_owned(const Root& variables,const Value& plan) {
    require(next_state(variables,plan)=="owned","boot-next-conflict","BootNext is absent or belongs to another operation");
    auto file=variables.open(variable("BootNext"),O_RDONLY|O_NONBLOCK); struct stat held{},named{};
    require(::fstat(file.get(),&held)==0 && S_ISREG(held.st_mode) && held.st_nlink==1 && held.st_uid==::geteuid() && (held.st_mode&0777)==0600 &&
        ::fstatat(variables.fd(),variable("BootNext").c_str(),&named,AT_SYMLINK_NOFOLLOW)==0 && held.st_dev==named.st_dev && held.st_ino==named.st_ino,
        "unsafe-efi-fixture","BootNext was replaced, linked or exposed");
    require(::unlinkat(variables.fd(),variable("BootNext").c_str(),0)==0 && ::fsync(variables.fd())==0,
        "uncertain-boot-consume","Cannot durably remove fixture BootNext; it must never be automatically replayed");
}
Value decision(const Value& plan,const Value& current,bool fallback=false) {
    Value result; result["request_id"]=plan["operation_id"]; result["plan_sha256"]=plan["plan_sha256"]; result["state"]=current;
    result["decision"]=plan[fallback || current["phase"]=="FALLBACK_SELECTED" ? "fallback" : "selected"];
    result["fallback_candidate"]=plan["fallback"]; result["default_preserved"]=true;
    result["reboot_performed"]=false; result["efi_application_started"]=false; result["physical_device_write_allowed"]=false;
    result["evidence_scope"]="regular-file-fixture"; return result;
}
} // namespace

Value boot_route_inventory(const Root& esp,const Root& variables) {
    Value out; out["schema"]=1; out["options"]=Value(Json::arrayValue); out["boot_order"]=Value(Json::arrayValue);
    out["backend"]=fixture(variables) ? "uefi-variable-fixture" : "unaccepted-uefi-runtime";
    out["physical_device_write_allowed"]=false; out["aloha_uke_port_accepted"]=false;
    const auto bytes=variables.read(variable("BootOrder"),variable_limit);
    require(bytes.size()>=6 && (bytes.size()-4)%2==0 && (bytes.size()-4)/2<=128 && le(bytes,0,4)==7,
        "invalid-boot-order","BootOrder must be a bounded NV/BS/RT array of EFI option numbers");
    std::set<std::string> seen;
    for(std::size_t at=4;at<bytes.size();at+=2) {
        const auto number=hex(le(bytes,at,2),4); require(seen.insert(number).second,"invalid-boot-order","Duplicate BootOrder option");
        out["boot_order"].append(number); Value entry; entry["number"]=number;
        try {
            entry=load_option(variables.read(option_variable(number),variable_limit),number); entry["loader_identity"]=loader(esp,entry);
            entry["components_verified"]=true;
        } catch(const Error& error) { entry["components_verified"]=false; entry["error"]["code"]=error.code; entry["error"]["message"]=error.what(); }
        out["options"].append(entry);
    }
    out["default_option"]=out["boot_order"][0]; out["boot_order_sha256"]=sha256(bytes);
    out["boot_next_present"]=variables.exists(variable("BootNext"));
    out["request_owner_present"]=variables.exists(".ure-boot-owner.json");
    if(out["boot_next_present"].asBool()) {
        const auto next=variables.read(variable("BootNext"),variable_limit);
        require(next.size()==6 && le(next,0,4)==7,"invalid-boot-next","Malformed existing BootNext must be resolved before planning"); out["boot_next"]=hex(le(next,4,2),4);
    }
    return out;
}
Value boot_route_plan(const Root& esp,const Root& variables,const Value& request) {
    const std::set<std::string> keys{"schema","target","boot_option","fallback_option","esp_partuuid","profile","model"};
    require(request.isObject() && request.size()==keys.size() && request["schema"]==1,"invalid-boot-request","Use the complete versioned boot request");
    for(const auto& key:request.getMemberNames())require(keys.contains(key),"invalid-boot-request","Unknown boot request field");
    for(const auto& key:keys)if(key!="schema")require(request[key].isString(),"invalid-boot-request","Boot request values must be explicit strings");
    require(request["profile"]=="global-os3.0.303.0" || request["profile"]=="cn-os3.0.302.0","invalid-profile","Select a separate pinned Global or CN firmware profile");
    require(request["model"]=="xiaomi-pad-7" || request["model"]=="poco-pad-x1","invalid-model","Select a declared Uke model; this does not prove installed identity");
    const auto target=request["target"].asString(); require(target=="linux" || target=="windows" || target=="android","invalid-boot-target","Select Android, Linux or Windows");
    require(uuid(request["esp_partuuid"].asString()),"invalid-esp-identity","Supply the exact GPT ESP partition UUID");
    const auto selected=option_name(request["boot_option"].asString()),fallback=option_name(request["fallback_option"].asString());
    const auto inventory=boot_route_inventory(esp,variables);
    require(!inventory["boot_next_present"].asBool(),"boot-next-conflict","A prior one-shot request is still present");
    require(!inventory["request_owner_present"].asBool(),"boot-request-conflict","An earlier request still owns the fixture store; inspect and resolve its journal first");
    require(fallback==inventory["default_option"].asString() && selected!=fallback,"unsafe-boot-fallback","Fallback must be the unchanged first default; select a distinct one-shot option");
    Value plan; bool found=false,found_fallback=false;
    for(const auto& entry:inventory["options"]) {
        if(entry["number"]!=selected && entry["number"]!=fallback)continue;
        require(entry["components_verified"].asBool() && entry["esp_partuuid"]==request["esp_partuuid"],"boot-components-missing","Selected/default loader is absent, incompatible or belongs to another ESP");
        if(entry["number"]==selected) {
            require(entry["loader_identity"]["target"]==target,"boot-target-mismatch","The registered EFI path does not identify the selected target"); plan["selected"]=entry; found=true;
        } else { plan["fallback"]=entry; found_fallback=true; }
    }
    require(found && found_fallback,"boot-components-missing","Both selected and preserved default options must already be registered");
    require(plan["fallback"]["loader_identity"]["target"]=="android" || plan["fallback"]["loader_identity"]["target"]=="linux",
        "unsafe-boot-fallback","Retain an explicit Android return or Linux default path");
    plan["schema"]=1; plan["kind"]="ure-uefi-one-shot"; plan["operation_id"]=operation_id(); plan["created_utc"]=utc();
    plan["request"]=request; plan["esp_partuuid"]=request["esp_partuuid"]; plan["one_shot"]=true; plan["max_attempts"]=1;
    plan["esp_identity"]=descriptor_identity(esp.fd()); plan["variables_identity"]=descriptor_identity(variables.fd());
    plan["boot_order_sha256"]=inventory["boot_order_sha256"]; plan["backend"]=inventory["backend"];
    plan["fixture_execute_allowed"]=inventory["backend"]=="uefi-variable-fixture"; plan["physical_device_write_allowed"]=false;
    plan["model_identity_verified"]=false; plan["firmware_identity_verified"]=false; plan["fallback_boot_validated"]=false;
    plan["risk"]="Fixture simulation only: PE shape and hashes do not establish signatures, installed roots, Android return or Uke/Aloha boot support. BootOrder and stock partitions are never written.";
    plan["plan_sha256"]=digest(plan,"plan_sha256"); return plan;
}
Value boot_route_execute(const Root& esp,const Root& variables,const Value& plan,const fs::path& directory,const std::string& confirmation) {
    check_plan(plan); require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm this exact reviewed boot plan");
    Value targets=operation_targets(plan["esp_identity"]); targets.append(plan["variables_identity"]);
    ManagedOperation operation(operation_binding("boot.route",plan,directory,targets));
    write_gate(variables); auto variable_lock=lock(variables); validate_bindings(esp,variables,plan);
    require(next_state(variables,plan)=="absent","boot-next-conflict","Another one-shot request is present");
    require(owner_state(variables,plan)=="absent","boot-request-conflict","An earlier request owns the fixture store");
    require(!variables.exists(retirement_name(plan)),"boot-plan-replayed","This request ID is retired; generate and review a new plan instead of replaying it through another journal");
    auto store=private_directory(directory,true); auto journal_lock=lock(store); store.save_record("plan.json",plan);
    Value current; current["schema"]=1; current["request_id"]=plan["operation_id"]; current["plan_sha256"]=plan["plan_sha256"];
    current["events"]=Value(Json::arrayValue); current["attempts"]=0; current["evidence_scope"]="regular-file-fixture"; current["physical_boot_success"]=false;
    event(store,current,"ARMING","The full request and original default were synced before creating fixture BootNext");
    operation.begin("boot-arm");
    Value owner; owner["schema"]=1; owner["request_id"]=plan["operation_id"]; owner["plan_sha256"]=plan["plan_sha256"];
    variables.save_record(".ure-boot-owner.json",owner);
    arm(variables,plan); event(store,current,"ARMED","Fixture BootNext was synced and read back; default order is unchanged"); return decision(plan,current);
}
Value boot_route_history(const fs::path& directory) {
    auto store=private_directory(directory,false); const auto plan=read_record(store,"plan.json"); check_plan(plan); const auto current=state(store,plan);
    Value out; out["request_id"]=plan["operation_id"]; out["plan_sha256"]=plan["plan_sha256"]; out["phase"]=current["phase"]; out["events"]=current["events"];
    out["attempts"]=current["attempts"]; out["physical_boot_success"]=false; out["evidence_scope"]="regular-file-fixture"; return out;
}
Value boot_route_action(const Root& esp,const Root& variables,const fs::path& directory,const std::string& action,const std::string& confirmation,const Value& receipt) {
    require(action=="inspect" || action=="recover" || action=="consume-fixture" || action=="ack-fixture" || action=="cancel" || action=="fallback-fixture",
        "invalid-boot-action","Unknown boot journal action");
    auto store=private_directory(directory,false); const auto plan=read_record(store,"plan.json"); check_plan(plan);
    std::unique_ptr<ManagedOperation> operation;
    if(action!="inspect") { Value targets=operation_targets(plan["esp_identity"]); targets.append(plan["variables_identity"]);
        operation=std::make_unique<ManagedOperation>(operation_binding("boot.route",plan,directory,targets),true); }
    write_gate(variables);
    auto variable_lock=lock(variables,false); auto journal_lock=lock(store,false);
    require(json(read_record(store,"plan.json"))==json(plan),"changed-journal","Boot plan changed during ownership admission"); auto current=state(store,plan);
    validate_bindings(esp,variables,plan); const auto observed=next_state(variables,plan),phase=current["phase"].asString();
    const auto ownership=owner_state(variables,plan);
    auto out=decision(plan,current); out["boot_next_observation"]=observed; out["recovery_actions"]=Value(Json::arrayValue);
    out["request_owner_observation"]=ownership;
    if(action=="inspect") {
        out["observation_is_boot_failure"]=false;
        if(observed!="foreign" && ownership!="foreign") {
            if(phase=="ARMING" || phase=="CONSUMING" || phase=="CONSUMED" || phase=="CANCELLING" || (phase=="ARMED" && observed=="absent"))out["recovery_actions"].append("recover");
            if(phase=="ARMED" && observed=="owned") { out["recovery_actions"].append("consume-fixture"); out["recovery_actions"].append("cancel"); }
            if(phase=="FALLBACK_PENDING" || phase=="UNKNOWN")out["recovery_actions"].append("fallback-fixture");
            if((phase=="ACKNOWLEDGED" || phase=="CANCELLED" || phase=="FALLBACK_SELECTED") && ownership=="owned" && observed=="absent")out["recovery_actions"].append("recover");
        } return out;
    }
    require(confirmation==plan["plan_sha256"].asString(),"confirmation-required","Confirm the exact inspected request before changing its fixture state");
    if(action=="consume-fixture")require(phase=="ARMED" && current["attempts"]==0,"boot-already-consumed","Only an armed, unconsumed request may be handed off once");
    if(action=="ack-fixture")require(phase=="CONSUMED","invalid-boot-transition","An acknowledgement cannot be replayed or precede consumption");
    if(action=="cancel")require(phase=="ARMED","invalid-boot-transition","Only an armed request can be cancelled");
    require(observed!="foreign","boot-next-conflict","Another BootNext is present; it must not be removed or replaced");
    require(ownership!="foreign","boot-request-conflict","Another request owns this variable store, even if its BootNext bytes match");
    const bool terminal=phase=="ACKNOWLEDGED" || phase=="CANCELLED" || phase=="FALLBACK_SELECTED";
    require(ownership=="owned" || (action=="recover" && observed=="absent" &&
        (phase=="ARMING" || (terminal && operation->token().has_retained_intent()))),"boot-owner-missing","No matching durable ownership record exists for this operation");
    operation->begin("boot-transition");
    if(ownership=="absent" && !terminal) {
        Value owner; owner["schema"]=1; owner["request_id"]=plan["operation_id"]; owner["plan_sha256"]=plan["plan_sha256"];
        variables.save_record(".ure-boot-owner.json",owner);
    }
    if(action=="recover") {
        if(phase=="ARMING" && observed=="owned")event(store,current,"ARMED","Recovered a complete matching BootNext after interruption; no loader was started");
        else if(phase=="CANCELLING" && observed=="absent") { event(store,current,"CANCELLED","Confirmed that the interrupted cancellation removed only the owned BootNext"); release_owner(variables,plan); }
        else if(terminal && observed=="absent") { if(ownership=="owned")release_owner(variables,plan); }
        else {
            require(phase=="ARMING" || phase=="CONSUMING" || phase=="CONSUMED" || phase=="CANCELLING" || (phase=="ARMED" && observed=="absent"),"invalid-boot-transition","This stable phase does not require recovery");
            if(observed=="owned")remove_owned(variables,plan);
            event(store,current,"UNKNOWN","Interrupted handoff or missing BootNext has no trusted result; the request is not replayed and no boot failure is inferred");
        }
    } else if(action=="consume-fixture") {
        require(phase=="ARMED" && observed=="owned" && current["attempts"]==0,"boot-already-consumed","Only an armed, unconsumed request may be handed off once");
        current["attempt_id"]=operation_id(); current["attempts"]=1; const auto token=operation_id(); current["handoff_token_sha256"]=sha256(token);
        event(store,current,"CONSUMING","Consumption intent was synced before removing fixture BootNext"); remove_owned(variables,plan);
        event(store,current,"CONSUMED","Fixture request was durably consumed once; this decision does not start an EFI application or prove an OS boot");
        out=decision(plan,current); out["handoff_token"]=token; return out;
    } else if(action=="ack-fixture") {
        require(phase=="CONSUMED" && observed=="absent","invalid-boot-transition","A fixture result may acknowledge exactly one completed consumption");
        const std::set<std::string> keys{"schema","request_id","attempt_id","handoff_token","boot_id","loader_sha256","result"};
        require(receipt.isObject() && receipt.size()==keys.size() && receipt["schema"]==1,"invalid-boot-receipt","Use the exact versioned fixture acknowledgement");
        for(const auto& key:receipt.getMemberNames())require(keys.contains(key),"invalid-boot-receipt","Unknown acknowledgement field");
        require(receipt["request_id"]==plan["operation_id"] && receipt["attempt_id"]==current["attempt_id"] &&
            receipt["handoff_token"].isString() && receipt["handoff_token"].asString().size()==32 &&
            sha256(receipt["handoff_token"].asString())==current["handoff_token_sha256"].asString() && uuid(receipt["boot_id"].asString()) &&
            receipt["loader_sha256"]==plan["selected"]["loader_identity"]["sha256"] && (receipt["result"]=="success" || receipt["result"]=="failure"),
            "boot-receipt-mismatch","Acknowledgement must match the request, single attempt, private handoff token and exact loader");
        auto retained=receipt; retained.removeMember("handoff_token"); current["acknowledgement"]=retained;
        event(store,current,receipt["result"]=="success" ? "ACKNOWLEDGED" : "FALLBACK_PENDING",
            receipt["result"]=="success" ? "A matching fixture success receipt was accepted; no physical result is established" : "A matching fixture failure receipt selected the preserved default as the next candidate");
        if(receipt["result"]=="success")release_owner(variables,plan);
    } else if(action=="cancel") {
        require(phase=="ARMED" && observed=="owned","invalid-boot-transition","Only an owned armed request can be cancelled");
        event(store,current,"CANCELLING","Cancellation intent was synced before removing the owned fixture request"); remove_owned(variables,plan);
        event(store,current,"CANCELLED","Owned fixture request removed; default and all OS assets were preserved");
        release_owner(variables,plan);
    } else {
        require((phase=="FALLBACK_PENDING" || phase=="UNKNOWN") && observed=="absent","invalid-boot-transition","Fallback requires a failed fixture acknowledgement or an explicitly unknown interrupted attempt");
        event(store,current,"FALLBACK_SELECTED","The unchanged first BootOrder option is the fixture fallback decision; no new BootNext, boot-set restoration or reboot is performed");
        release_owner(variables,plan);
        validate_bindings(esp,variables,plan);
        require(next_state(variables,plan)=="absent" && owner_state(variables,plan)=="absent","boot-owner-release-unverified","Boot fixture retirement did not verify");
        return operation->finish(decision(plan,current,true),true,true,"COMPLETE");
    }
    if(current["phase"]=="ACKNOWLEDGED" || current["phase"]=="CANCELLED" || current["phase"]=="FALLBACK_SELECTED") {
        validate_bindings(esp,variables,plan);
        require(next_state(variables,plan)=="absent" && owner_state(variables,plan)=="absent","boot-owner-release-unverified","Boot fixture retirement did not verify");
        return operation->finish(decision(plan,current),true,true,"COMPLETE");
    }
    return decision(plan,current);
}
} // namespace ure
