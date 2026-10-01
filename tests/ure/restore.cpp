// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <array>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace {
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
template<class F> void reject(F function,const std::string& code) {
    try { function(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected rejection: "+error.code+", wanted "+code); return; }
    throw std::runtime_error("Expected rejection: "+code);
}
void bytes(const ure::fs::path& path,std::string_view data) {
    std::ofstream file(path,std::ios::binary); file.write(data.data(),static_cast<std::streamsize>(data.size())); check(file.good(),"Cannot write fixture");
}
std::string hash(const ure::fs::path& path) { ure::Fd fd(::open(path.c_str(),O_RDONLY|O_NOFOLLOW)); return ure::sha256(fd.get()); }
void phase(const ure::fs::path& journal,ure::Value state,const std::string& next,const std::string& direction="restore") {
    state["state"]=next; state["direction"]=direction; state["completed_bytes"]=Json::UInt64(UINT64_MAX);
    state.removeMember("verified"); ure::save_json(journal/"journal.json",state,true);
}
void kill_at_boundary(const ure::fs::path& image,const ure::fs::path& journal,const ure::Value& plan,
                      bool rollback,bool preparing=false) {
    const auto child=::fork(); check(child>=0,"Cannot fork restore interruption fixture");
    if(child==0) {
        try {
            ure::Root system("/"); auto target=ure::storage_image(image,4096,true);
            if(rollback)ure::restore_rollback(system,target,journal,plan["plan_sha256"].asString());
            else ure::restore_execute(system,target,plan,journal,plan["plan_sha256"].asString());
            ::_exit(0);
        } catch(...) { ::_exit(2); }
    }
    bool observed=false;
    for(unsigned tries=0;tries<15000;++tries) {
        try {
            if(preparing)observed=ure::fs::exists(journal/"before/chunk-00000.bin") && !ure::fs::exists(journal/"before/chunk-00511.bin");
            else if(ure::fs::exists(journal/"journal.json")) {
                const auto state=ure::json_file(journal/"journal.json");
                observed=state["state"]==(rollback ? "ROLLBACK_REQUIRED" : "EXECUTING") &&
                    state["completed_bytes"].asUInt64()>0 && state["completed_bytes"].asUInt64()<32*1024*1024;
            }
        } catch(const ure::Error&) {}
        if(observed)break;
        ::usleep(1000);
    }
    ::kill(child,SIGSTOP); int status=0;
    check(::waitpid(child,&status,WUNTRACED)==child,"Cannot stop interruption fixture");
    if(WIFSTOPPED(status)) { ::kill(child,SIGKILL); check(::waitpid(child,&status,0)==child,"Cannot reap interruption fixture"); }
    check(observed && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Restore writer did not stop at the requested durable boundary");
}
}
int main() {
    std::array<char,40> temporary{}; const std::string pattern="/tmp/ure-restore-tests-XXXXXX";
    std::copy(pattern.begin(),pattern.end(),temporary.begin()); const auto raw=::mkdtemp(temporary.data()); if(!raw)return 1;
    const ure::fs::path work(raw); ure::Root system("/");
    try {
        const std::string desired=std::string(65536,'A')+std::string(65536,'B')+std::string(65536,'C')+std::string(65536,'D')+std::string(65536,'E');
        const std::string original(desired.size(),'X');
        for(const std::uint32_t sector:{512U,4096U}) {
            const auto image=work/("image-"+std::to_string(sector)),backup=work/("backup-"+std::to_string(sector)),journal=work/("journal-"+std::to_string(sector));
            bytes(image,desired); check(::setxattr(image.c_str(),"user.ure-test","preserved",9,0)==0,"Cannot create metadata fixture");
            auto target=ure::storage_image(image,sector); const auto manifest=ure::backup_storage_plan(system,target,"fixture",65536);
            ure::backup_capture(system,manifest,backup,false); bytes(image,original); target=ure::storage_image(image,sector,true);
            const auto plan=ure::restore_plan(system,target,backup,"fixture"); const auto confirmation=plan["plan_sha256"].asString();
            check(plan["target_sha256"]==ure::sha256(desired) && plan["before"]["sha256"]==ure::sha256(original),"Restore plan did not bind both content states");
            reject([&]{ure::restore_plan(system,target,backup,"other");},"wrong-profile");
            auto wrong_sector=ure::storage_image(image,sector==512 ? 4096U : 512U,true);
            reject([&]{ure::restore_plan(system,wrong_sector,backup,"fixture");},"wrong-target");
            auto readonly=ure::storage_image(image,sector);
            reject([&]{ure::restore_execute(system,readonly,plan,journal,confirmation);},"read-only-target");
            reject([&]{ure::restore_execute(system,target,plan,journal,"wrong");},"confirmation-required");
            check(!ure::fs::exists(journal) && hash(image)==ure::sha256(original),"Rejected execution created a journal or changed data");
            ure::Root source(backup); auto source_lock=source.open(".lock",O_RDWR);
            check(::flock(source_lock.get(),LOCK_EX|LOCK_NB)==0,"Cannot lock source fixture");
            reject([&]{ure::restore_plan(system,target,backup,"fixture");},"busy-journal"); ::flock(source_lock.get(),LOCK_UN);
            auto competing=ure::storage_image(image,sector,true); check(::flock(competing.descriptor.get(),LOCK_EX|LOCK_NB)==0,"Cannot lock target fixture");
            reject([&]{ure::restore_execute(system,target,plan,journal,confirmation);},"busy-target"); ::flock(competing.descriptor.get(),LOCK_UN);
            auto state=ure::restore_execute(system,target,plan,journal,confirmation);
            check(state["state"]=="COMMITTED" && state["verified"]==true && state["physical_test_record"]==false && hash(image)==ure::sha256(desired),"Raw restore failed");
            check(ure::restore_inspect(system,target,journal)["classification"]=="TARGET","Committed restore was not verified from data");
            reject([&]{ure::restore_resume(system,target,journal,confirmation);},"unsafe-resume");
            ure::Root stored(journal); auto lock=stored.open(".lock",O_RDWR); check(::flock(lock.get(),LOCK_EX|LOCK_NB)==0,"Cannot lock journal fixture");
            reject([&]{ure::restore_inspect(system,target,journal);},"busy-journal"); ::flock(lock.get(),LOCK_UN);
            // Journal completion may lag the already fsynced/read-back target.
            phase(journal,state,"VERIFYING"); ure::fs::rename(backup,work/("offline-backup-"+std::to_string(sector)));
            state=ure::restore_resume(system,target,journal,confirmation);
            check(state["state"]=="COMMITTED" && state["recovered_by_readback"]==true && state["written_chunks"].asUInt64()==0,"Readback-only completion rewrote data or needed the external source");
            state=ure::restore_rollback(system,target,journal,confirmation);
            check(state["state"]=="ROLLED_BACK" && hash(image)==ure::sha256(original),"Rollback did not restore pre-operation data");
            phase(journal,state,"READY");
            state=ure::restore_cancel(system,target,journal,confirmation);
            check(state["state"]=="CANCELLED_SAFE" && hash(image)==ure::sha256(original),"READY cancellation did not preserve original bytes");
            std::array<char,20> metadata{}; check(::getxattr(image.c_str(),"user.ure-test",metadata.data(),metadata.size())==9 && std::string(metadata.data(),9)=="preserved","Raw restore discarded xattrs");
            phase(journal,state,"EXECUTING"); auto partial=original; partial.replace(0,32769,desired.substr(0,32769)); bytes(image,partial);
            check(ure::restore_inspect(system,target,journal)["classification"]=="PARTIAL_EXPECTED_WRITE","Partial byte write was not distinguished");
            state=ure::restore_resume(system,target,journal,confirmation);
            check(state["state"]=="COMMITTED" && hash(image)==ure::sha256(desired) && state["written_chunks"].asUInt64()==5,"Confirmed partial-write resume failed");
            phase(journal,state,"EXECUTING"); auto unrelated=desired; unrelated[123]='Z'; bytes(image,unrelated);
            check(ure::restore_inspect(system,target,journal)["classification"]=="DIVERGED","Unrelated bytes were mistaken for a partial restore");
            reject([&]{ure::restore_resume(system,target,journal,confirmation);},"unsafe-resume");
            reject([&]{ure::restore_rollback(system,target,journal,confirmation);},"changed-target");
            reject([&]{ure::restore_cancel(system,target,journal,confirmation);},"unsafe-cancel");
            const auto other=work/("other-"+std::to_string(sector)); bytes(other,unrelated); auto other_target=ure::storage_image(other,sector,true);
            reject([&]{ure::restore_inspect(system,other_target,journal);},"wrong-target");
            bytes(journal/"after/chunk-00000.bin",std::string(65536,'F'));
            reject([&]{ure::restore_inspect(system,target,journal);},"backup-corrupt");
            ::chmod(journal.c_str(),0755); reject([&]{ure::restore_inspect(system,target,journal);},"unsafe-journal"); ::chmod(journal.c_str(),0700);
        }
        // Real process death during preparation and target writes. Parent and
        // child use distinct open descriptions, so death releases kernel locks.
        const auto image=work/"large-image",backup=work/"large-backup"; const std::string wanted(32*1024*1024,'D'),before(wanted.size(),'X');
        bytes(image,wanted); auto target=ure::storage_image(image,4096);
        auto manifest=ure::backup_storage_plan(system,target,"fixture",65536); ure::backup_capture(system,manifest,backup,false);
        bytes(image,before); target=ure::storage_image(image,4096,true); auto plan=ure::restore_plan(system,target,backup,"fixture");
        const auto confirmation=plan["plan_sha256"].asString();
        kill_at_boundary(image,work/"killed-prepare",plan,false,true);
        auto inspection=ure::restore_inspect(system,target,work/"killed-prepare");
        check(inspection["backup_verified"]==false && inspection["classification"]=="ORIGINAL","Preparation interruption changed target bytes");
        auto state=ure::restore_resume(system,target,work/"killed-prepare",confirmation);
        check(state["state"]=="COMMITTED" && hash(image)==ure::sha256(wanted),"Preparation resume after SIGKILL failed");
        ure::restore_rollback(system,target,work/"killed-prepare",confirmation);
        target=ure::storage_image(image,4096,true); plan=ure::restore_plan(system,target,backup,"fixture");
        kill_at_boundary(image,work/"killed-write",plan,false);
        inspection=ure::restore_inspect(system,target,work/"killed-write");
        check(inspection["classification"]=="PARTIAL_EXPECTED_WRITE" && inspection["backup_verified"]==true && inspection["chunks_truncated"]==true,"Write interruption was not classified from bounded data observations");
        state=ure::restore_resume(system,target,work/"killed-write",plan["plan_sha256"].asString());
        check(state["state"]=="COMMITTED" && hash(image)==ure::sha256(wanted),"Resume after target SIGKILL failed");
        kill_at_boundary(image,work/"killed-write",plan,true);
        inspection=ure::restore_inspect(system,target,work/"killed-write");
        check(inspection["direction"]=="rollback" && inspection["classification"]=="PARTIAL_EXPECTED_WRITE","Rollback interruption lost direction or byte classification");
        reject([&]{ure::restore_resume(system,target,work/"killed-write",plan["plan_sha256"].asString());},"unsafe-resume");
        state=ure::restore_rollback(system,target,work/"killed-write",plan["plan_sha256"].asString());
        check(state["state"]=="ROLLED_BACK" && hash(image)==ure::sha256(before),"Rollback continuation after SIGKILL failed");
        // Real EFBIG after the first pwrite must persist uncertainty. Existing
        // mirrors are read and private progress records remain below the limit.
        phase(work/"killed-write",state,"EXECUTING");
        const auto failing=::fork(); check(failing>=0,"Cannot fork write-error fixture");
        if(failing==0) {
            struct rlimit limit{};
            if(::getrlimit(RLIMIT_FSIZE,&limit)!=0)::_exit(3);
            limit.rlim_cur=65536; ::signal(SIGXFSZ,SIG_IGN);
            if(::setrlimit(RLIMIT_FSIZE,&limit)!=0)::_exit(3);
            try {
                auto writable=ure::storage_image(image,4096,true);
                ure::restore_resume(system,writable,work/"killed-write",plan["plan_sha256"].asString()); ::_exit(4);
            } catch(const ure::Error& error) { ::_exit(error.code=="io-error" ? 0 : 5); }
            catch(...) { ::_exit(6); }
        }
        int failure_status=0; check(::waitpid(failing,&failure_status,0)==failing && WIFEXITED(failure_status) && WEXITSTATUS(failure_status)==0,"Real partial-write error was not surfaced");
        state=ure::json_file(work/"killed-write/journal.json");
        check(state["state"]=="FAILED_UNCERTAIN" && state["verified"]==false && state["error_code"]=="io-error","Partial-write failure was mislabeled as safe");
        check(ure::restore_inspect(system,target,work/"killed-write")["classification"]=="PARTIAL_EXPECTED_WRITE","Write error lost byte-level recovery evidence");
        ure::restore_resume(system,target,work/"killed-write",plan["plan_sha256"].asString());
        check(hash(image)==ure::sha256(wanted),"Resume after EFBIG failed");
        ure::restore_rollback(system,target,work/"killed-write",plan["plan_sha256"].asString());
        target=ure::storage_image(image,4096,true); plan=ure::restore_plan(system,target,backup,"fixture");
        kill_at_boundary(image,work/"cancel-prepare",plan,false,true);
        state=ure::restore_cancel(system,target,work/"cancel-prepare",plan["plan_sha256"].asString());
        check(state["state"]=="CANCELLED_SAFE" && hash(image)==ure::sha256(before),"Pre-execution cancellation changed storage");
        reject([&]{ure::restore_resume(system,target,work/"cancel-prepare",plan["plan_sha256"].asString());},"unsafe-resume");
        std::cout<<"Raw restore tests: 512/4096 geometry, wrong target/profile, locks, readback, partial writes, divergence, private mirrors, metadata, source-independent recovery, preparation/write/rollback SIGKILL, EFBIG uncertainty/resume and cancellation passed; synthetic images only\n";
        ure::fs::remove_all(work); return 0;
    } catch(const std::exception& error) { std::cerr<<"Restore test failure: "<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
