// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"
#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <map>
#include <unistd.h>

namespace ure {
// The inherited ADB pipes carry complete checkpoints. An acknowledgement is
// accepted only after the PC has synced the files and their parent directories.
inline constexpr std::size_t dualboot_checkpoint_limit=4*1024*1024;
inline bool dualboot_journal_name(const std::string& name) {
    if(name=="plan.json" || name=="state.json")return true;
    for(const auto* region:{"protective_mbr","primary_header","primary_table","backup_header","backup_table"})
        if(name==std::string("before-")+region+".bin" || name==std::string("after-")+region+".bin")return true;
    return false;
}
inline void dualboot_pipe_write(int fd,std::string_view bytes,int timeout_ms=30000) {
    require(timeout_ms>0 && timeout_ms<=600000,"invalid-host-checkpoint","Invalid checkpoint transport timeout");
    const auto deadline=monotonic_ms()+static_cast<std::uint64_t>(timeout_ms);
    const auto flags=::fcntl(fd,F_GETFL);
    require(flags>=0 && ::fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0,"host-journal-disconnected","Cannot bound checkpoint transport writes");
    try {
        while(!bytes.empty()) {
            const auto now=monotonic_ms();
            require(now<deadline,"host-journal-disconnected","PC checkpoint transport timed out");
            pollfd p{fd,POLLOUT,0}; const auto ready=::poll(&p,1,static_cast<int>(deadline-now));
            if(ready<0 && errno==EINTR)continue;
            require(ready>0 && (p.revents&POLLOUT),"host-journal-disconnected","PC checkpoint transport is unavailable");
            const auto n=::write(fd,bytes.data(),bytes.size());
            if(n<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK))continue;
            require(n>0,"host-journal-disconnected","PC checkpoint transport closed"); bytes.remove_prefix(static_cast<std::size_t>(n));
        }
    } catch(...) { (void)::fcntl(fd,F_SETFL,flags); throw; }
    require(::fcntl(fd,F_SETFL,flags)==0,"host-journal-disconnected","Cannot restore checkpoint descriptor flags");
}
inline std::string dualboot_pipe_read(int fd,std::size_t count,int timeout_ms=30000) {
    require(count<=dualboot_checkpoint_limit,"invalid-host-checkpoint","Oversized PC checkpoint");
    require(timeout_ms>0 && timeout_ms<=600000,"invalid-host-checkpoint","Invalid checkpoint transport timeout");
    const auto deadline=monotonic_ms()+static_cast<std::uint64_t>(timeout_ms);
    std::string result(count,'\0'); std::size_t at=0;
    while(at<count) {
        const auto now=monotonic_ms(); require(now<deadline,"host-journal-disconnected","PC checkpoint acknowledgement timed out");
        pollfd p{fd,POLLIN,0}; const auto ready=::poll(&p,1,static_cast<int>(deadline-now));
        if(ready<0 && errno==EINTR)continue;
        require(ready>0 && (p.revents&(POLLIN|POLLHUP)),"host-journal-disconnected","PC checkpoint acknowledgement timed out");
        const auto n=::read(fd,result.data()+at,count-at);
        if(n<0 && errno==EINTR)continue;
        require(n>0,"host-journal-disconnected","PC checkpoint transport closed"); at+=static_cast<std::size_t>(n);
    }
    return result;
}
inline std::string dualboot_pipe_line(int fd,std::size_t maximum,int timeout_ms=30000) {
    require(timeout_ms>0 && timeout_ms<=600000,"invalid-host-checkpoint","Invalid checkpoint transport timeout");
    const auto deadline=monotonic_ms()+static_cast<std::uint64_t>(timeout_ms);
    std::string line;
    while(line.size()<maximum) {
        const auto now=monotonic_ms(); require(now<deadline,"host-journal-disconnected","PC checkpoint header timed out");
        const auto c=dualboot_pipe_read(fd,1,static_cast<int>(deadline-now)); if(c=="\n")return line; line+=c;
    }
    throw Error("invalid-host-checkpoint","Oversized PC checkpoint header");
}
inline std::string dualboot_pipe_completion(int fd,int timeout_ms=30000) {
    require(timeout_ms>0 && timeout_ms<=600000,"invalid-host-checkpoint","Invalid checkpoint transport timeout");
    const auto deadline=monotonic_ms()+static_cast<std::uint64_t>(timeout_ms);
    std::string result; std::array<char,4096> buffer{};
    while(true) {
        const auto now=monotonic_ms(); require(now<deadline,"host-journal-disconnected","Final device result timed out");
        pollfd p{fd,POLLIN,0}; const auto ready=::poll(&p,1,static_cast<int>(deadline-now));
        if(ready<0 && errno==EINTR)continue;
        require(ready>0 && (p.revents&(POLLIN|POLLHUP)),"host-journal-disconnected","Final device result timed out");
        const auto n=::read(fd,buffer.data(),buffer.size());
        if(n<0 && errno==EINTR)continue;
        require(n>=0,"host-journal-disconnected","Final device result could not be read");
        if(n==0)return result;
        result.append(buffer.data(),static_cast<std::size_t>(n));
        require(result.size()<=1024*1024,"invalid-host-checkpoint","Oversized final device result");
    }
}
inline std::string checkpoint_bytes(const Value& row) {
    require(row.isObject() && row["name"].isString() && dualboot_journal_name(row["name"].asString()) &&
        row["hex"].isString() && hash_valid(row["sha256"].asString()),"invalid-host-checkpoint","Invalid checkpoint member");
    const auto hex=row["hex"].asString();
    require(!hex.empty() && hex.size()%2==0 && hex.size()<=2*1024*1024,"invalid-host-checkpoint","Invalid checkpoint byte count");
    auto digit=[](char c)->unsigned { require((c>='0'&&c<='9') || (c>='a'&&c<='f'),"invalid-host-checkpoint","Invalid checkpoint encoding");
        return static_cast<unsigned>(c<='9' ? c-'0' : c-'a'+10); };
    std::string bytes; bytes.reserve(hex.size()/2);
    for(std::size_t at=0;at<hex.size();at+=2)bytes+=static_cast<char>((digit(hex[at])<<4)|digit(hex[at+1]));
    require(sha256(bytes)==row["sha256"].asString(),"invalid-host-checkpoint","Checkpoint member digest differs"); return bytes;
}
inline std::map<std::string,std::string> dualboot_checkpoint_files(const Value& snapshot,const std::string& plan_hash,const std::string& session,std::uint64_t sequence) {
    require(snapshot["schema"]==1 && snapshot["format"]=="ure-adb-dualboot-checkpoint" && snapshot["plan_sha256"]==plan_hash &&
        snapshot["session"]==session && identifier(session) && sequence>0 && snapshot["sequence"].isUInt64() && snapshot["sequence"].asUInt64()==sequence &&
        snapshot["files"].isArray() && snapshot["files"].size()==12,"invalid-host-checkpoint","Checkpoint identity, order or coverage differs");
    std::map<std::string,std::string> files;
    for(const auto& row:snapshot["files"])require(files.emplace(row["name"].asString(),checkpoint_bytes(row)).second,"invalid-host-checkpoint","Duplicate checkpoint member");
    require(files.contains("plan.json") && files.contains("state.json"),"invalid-host-checkpoint","Missing checkpoint plan or state");
    const auto plan=parse_json(files.at("plan.json")),state=parse_json(files.at("state.json"));
    dualboot_validate_plan(plan);
    require(plan["plan_sha256"]==plan_hash && state["schema"]==1 && state["plan_sha256"]==plan_hash && state["format"]=="ure-live-dualboot-journal" &&
        state["phase"].isString() && state["phase"]==snapshot["phase"],"invalid-host-checkpoint","Stored plan and state differ");
    for(const auto* version:{"before","after"}) {
        require(plan["gpt"][version].isArray() && plan["gpt"][version].size()==5,"invalid-host-checkpoint","All five GPT regions are required");
        for(const auto& region:plan["gpt"][version]) {
            const auto name=std::string(version)+"-"+region["name"].asString()+".bin";
            require(files.contains(name) && files.at(name).size()==region["bytes"].asUInt64() && sha256(files.at(name))==region["sha256"].asString(),
                "invalid-host-checkpoint","Stored GPT bytes differ from the reviewed plan");
        }
    }
    return files;
}
inline Value dualboot_checkpoint(const Root& store,const Value& state,const std::string& session,std::uint64_t sequence) {
    Value result; result["schema"]=1; result["format"]="ure-adb-dualboot-checkpoint";
    result["session"]=session; result["sequence"]=Json::UInt64(sequence); result["plan_sha256"]=state["plan_sha256"];
    result["phase"]=state["phase"]; result["files"]=Value(Json::arrayValue);
    std::size_t total=0;
    for(const auto& entry:store.list(".",32)) {
        if(entry=="owner.lock")continue;
        if(entry=="transport.json") {
            const auto info=store.stat(entry);
            require(S_ISREG(info.st_mode) && info.st_uid==::geteuid() && info.st_nlink==1 && (info.st_mode&0777)==0600 &&
                info.st_size>0 && info.st_size<=static_cast<off_t>(dualboot_checkpoint_limit),"invalid-host-checkpoint","Invalid imported checkpoint receipt");
            const auto receipt=parse_json(store.read(entry,dualboot_checkpoint_limit));
            const auto files=dualboot_checkpoint_files(receipt,state["plan_sha256"].asString(),receipt["session"].asString(),receipt["sequence"].asUInt64());
            // A legacy receipt remains immutable evidence after the new session updates state.
            for(const auto& [name,bytes]:files)if(name!="state.json" || sequence==1)
                require(store.read(name,1024*1024)==bytes,"invalid-host-checkpoint","Imported checkpoint files differ from their receipt");
            continue;
        }
        require(dualboot_journal_name(entry),"invalid-host-checkpoint","Unexpected journal member");
        auto file=store.open(entry,O_RDONLY|O_NONBLOCK); struct stat st{};
        require(::fstat(file.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_uid==::geteuid() && st.st_nlink==1 &&
            (st.st_mode&0777)==0600 && st.st_size>0 && st.st_size<=1024*1024,"invalid-host-checkpoint","Invalid private journal member");
        const auto bytes=store.read(entry,1024*1024); total+=bytes.size();
        require(total<=dualboot_checkpoint_limit/3,"invalid-host-checkpoint","Journal snapshot exceeds its budget");
        constexpr char digits[]="0123456789abcdef"; std::string hex; hex.reserve(bytes.size()*2);
        for(char raw:bytes) { const auto c=static_cast<unsigned char>(raw); hex+=digits[c>>4];hex+=digits[c&15]; }
        Value row; row["name"]=entry; row["sha256"]=sha256(bytes); row["hex"]=hex; result["files"].append(row);
    }
    require(result["files"].size()==12,"invalid-host-checkpoint","A complete plan, state and ten GPT copies are required");
    return result;
}
class DualbootHostJournal {
    bool enabled_;
    std::string session_=operation_id();
    std::uint64_t sequence_=0;
public:
    explicit DualbootHostJournal(bool enabled):enabled_(enabled) {
        if(!enabled_)return;
        ::signal(SIGPIPE,SIG_IGN);
        for(const int fd:{STDIN_FILENO,STDOUT_FILENO}) { struct stat st{};
            require(::fstat(fd,&st)==0 && (S_ISFIFO(st.st_mode)||S_ISSOCK(st.st_mode)) && !::isatty(fd),
                "invalid-host-checkpoint","Host-assisted application requires inherited binary ADB pipes"); }
    }
    void validate_store(const Root& store) const {
        struct stat st{}; struct statfs filesystem{};
        require(enabled_ && ::fstat(store.fd(),&st)==0 && st.st_uid==0 && (st.st_mode&0777)==0700 &&
            ::fstatfs(store.fd(),&filesystem)==0 && (filesystem.f_type==TMPFS_MAGIC || filesystem.f_type==RAMFS_MAGIC),
            "invalid-host-checkpoint","The tablet staging journal must be a private RAM directory");
    }
    void checkpoint(const Root& store,const Value& state) {
        if(!enabled_)return;
        const auto data=json(dualboot_checkpoint(store,state,session_,++sequence_));
        require(data.size()<=dualboot_checkpoint_limit,"invalid-host-checkpoint","Oversized PC snapshot");
        const auto digest=sha256(data);
        dualboot_pipe_write(STDOUT_FILENO,"UKE-JOURNAL-1 "+std::to_string(data.size())+"\n"+data);
        const auto expected="UKE-SYNCED-1 "+session_+" "+std::to_string(sequence_)+" "+state["plan_sha256"].asString()+" "+digest;
        require(dualboot_pipe_line(STDIN_FILENO,256)==expected,"invalid-host-acknowledgement","PC checkpoint acknowledgement differs; no next write is authorized");
    }
};
}
