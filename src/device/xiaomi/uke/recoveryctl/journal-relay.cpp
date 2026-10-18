// SPDX-License-Identifier: Apache-2.0
// Host-only ADB checkpoint receiver. It never opens a block device.
#include "libuke/dualboot_host_journal.hpp"
#include <charconv>
#include <csignal>
#include <iostream>

namespace ure {
void persist_checkpoint(const Root& destination,const Value& snapshot,const std::string& data,const std::string& plan_hash,const std::string& session,std::uint64_t sequence) {
    const auto files=dualboot_checkpoint_files(snapshot,plan_hash,session,sequence);
    auto checkpoint=private_subdirectory(destination,"checkpoint-"+std::to_string(sequence),true);
    for(const auto& [name,bytes]:files) {
        auto file=checkpoint.open(name,O_WRONLY|O_CREAT|O_EXCL,0600);
        dualboot_pipe_write(file.get(),bytes);
        require(::fsync(file.get())==0,"host-journal-sync-failed","PC checkpoint file synchronization failed");
    }
    destination.save_record("checkpoint-"+std::to_string(sequence)+"-transport.json",parse_json(data));
    require(::fsync(checkpoint.fd())==0 && ::fsync(destination.fd())==0,"host-journal-sync-failed","PC checkpoint directory synchronization failed");
}
int journal_relay(const fs::path& directory,const std::string& plan_hash) {
    require(hash_valid(plan_hash),"invalid-host-checkpoint","An exact reviewed plan digest is required");
    auto destination=private_directory(directory,true); struct statfs filesystem{};
    require(::fstatfs(destination.fd(),&filesystem)==0 && (filesystem.f_type==EXT4_SUPER_MAGIC || filesystem.f_type==BTRFS_SUPER_MAGIC || filesystem.f_type==XFS_SUPER_MAGIC),
        "unsafe-host-journal","The PC journal requires a local ext4, Btrfs or XFS filesystem");
    std::uint64_t sequence=0; std::string session,phase;
    while(true) {
        const auto header=dualboot_pipe_line(STDIN_FILENO,128,600000);
        if(header=="UKE-JOURNAL-DONE-1")break;
        constexpr std::string_view prefix="UKE-JOURNAL-1 ";
        require(header.starts_with(prefix),"invalid-host-checkpoint","Unexpected ADB checkpoint frame");
        std::uint64_t bytes=0; const auto number=std::string_view(header).substr(prefix.size());
        const auto parsed=std::from_chars(number.data(),number.data()+number.size(),bytes);
        require(parsed.ec==std::errc() && parsed.ptr==number.data()+number.size() && bytes>0 && bytes<=dualboot_checkpoint_limit,
            "invalid-host-checkpoint","Invalid ADB checkpoint length");
        const auto data=dualboot_pipe_read(STDIN_FILENO,static_cast<std::size_t>(bytes)); const auto snapshot=parse_json(data);
        if(sequence==0)session=snapshot["session"].asString();
        persist_checkpoint(destination,snapshot,data,plan_hash,session,++sequence);
        phase=snapshot["phase"].asString();
        dualboot_pipe_write(STDOUT_FILENO,"UKE-SYNCED-1 "+session+" "+std::to_string(sequence)+" "+plan_hash+" "+sha256(data)+"\n");
    }
    const auto completion=parse_json(dualboot_pipe_completion(STDIN_FILENO));
    require(sequence>0 && (phase=="COMMITTED_REBOOT_REQUIRED" || phase=="GPT_RESTORED_REBOOT_REQUIRED") &&
        completion["result"]=="ok" && completion["data"]["plan_sha256"]==plan_hash && completion["data"]["state"]==phase,
        "incomplete-host-operation","The device did not report the synchronized terminal state");
    destination.save_record("completion.json",completion);
    std::cerr<<"PC journal synchronized "<<sequence<<" checkpoints.\n"; return 0;
}
}
#ifndef UKE_JOURNAL_RELAY_TEST
int main(int argc,char** argv) {
    ::signal(SIGPIPE,SIG_IGN);
    try { ure::require(argc==3,"invalid-options","Usage: uke-journal-relay NEW_PRIVATE_DIRECTORY PLAN_SHA256"); return ure::journal_relay(argv[1],argv[2]); }
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
#endif
