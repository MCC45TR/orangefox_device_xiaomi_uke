// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <array>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

// Host-only fault boundary: the old publication's second link becomes visible
// before unlink. Freeze that child so SIGKILL deterministically tests the gap.
static bool pause_chunk_link=false;
extern "C" int __real_linkat(int,const char*,int,const char*,int);
extern "C" int __wrap_linkat(int oldfd,const char* oldpath,int newfd,const char* newpath,int flags) {
    const auto result=__real_linkat(oldfd,oldpath,newfd,newpath,flags);
    if(result==0 && pause_chunk_link && std::string_view(newpath).starts_with("chunk-"))::raise(SIGSTOP);
    return result;
}

namespace {
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
template<class F> void reject(F function,const std::string& code) {
    try { function(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected rejection: "+error.code+", wanted "+code); return; }
    throw std::runtime_error("Expected rejection: "+code);
}
void bytes(const ure::fs::path& path,std::string_view data) {
    std::ofstream file(path,std::ios::binary); file.write(data.data(),static_cast<std::streamsize>(data.size())); check(file.good(),"Cannot write fixture");
}
void interrupted(const ure::Root& root,const ure::Value& plan,const ure::fs::path& path,const std::string& phase) {
    auto journal=ure::private_directory(path,true); journal.save_record("plan.json",plan);
    auto backup=ure::backup_file(root,plan["path"].asString(),path/"backup.bin"); journal.save_record("backup.json",backup);
    ure::Value state; state["schema"]=1; state["operation_id"]=plan["operation_id"]; state["path"]=plan["path"];
    state["plan_sha256"]=plan["plan_sha256"]; state["expected_sha256"]=plan["payload_sha256"];
    state["source_state"]=plan["source_state"]; state["root_identity"]=plan["root_identity"]; state["state"]=phase;
    journal.save_record("journal.json",state);
}
}
int main() {
    std::array<char,40> temporary{}; const std::string pattern="/tmp/ure-recovery-tests-XXXXXX";
    std::copy(pattern.begin(),pattern.end(),temporary.begin()); const auto raw=::mkdtemp(temporary.data()); if(!raw)return 1;
    const ure::fs::path work(raw);
    try {
        ure::fs::create_directory(work/"root"); ure::Root root(work/"root"); bytes(work/"root/config","original\n");
        auto plan=ure::transaction_plan(root,"config","replacement\n","fixture");
        reject([&]{ure::transaction_plan(root,"config",std::string("\xc0\x80",2),"fixture");},"binary-file");
        interrupted(root,plan,work/"ready","READY");
        auto inspection=ure::transaction_inspect(root,work/"ready");
        check(inspection["incomplete"]==true && inspection["current_state"]=="ORIGINAL_UNCHANGED" && inspection["backup_verified"]==true,"READY recovery classification failed");
        reject([&]{ure::transaction_resume(root,work/"ready","wrong");},"confirmation-required");
        auto state=ure::transaction_resume(root,work/"ready",plan["plan_sha256"].asString());
        check(state["state"]=="COMMITTED" && root.read("config")=="replacement\n","READY resume failed");
        // Simulate interruption after durable rename but before the commit record.
        state["state"]="VERIFYING"; state.removeMember("result_identity"); state.removeMember("verified");
        ure::save_json(work/"ready/journal.json",state,true); const auto before=root.stat("config");
        state=ure::transaction_resume(root,work/"ready",plan["plan_sha256"].asString());
        check(state["recovered_by_readback"]==true && root.stat("config").st_ino==before.st_ino,"Readback recovery unexpectedly rewrote the target");
        ure::transaction_rollback(root,work/"ready",plan["plan_sha256"].asString());
        check(ure::transaction_inspect(root,work/"ready")["current_state"]=="ORIGINAL_CONTENT_VERIFIED","Rollback recovery classification failed");
        plan=ure::transaction_plan(root,"config","next\n","fixture"); interrupted(root,plan,work/"cancel","EXECUTING");
        state=ure::transaction_cancel(root,work/"cancel",plan["plan_sha256"].asString());
        check(state["state"]=="CANCELLED_SAFE" && root.read("config")=="original\n","Cancellation changed the target");
        reject([&]{ure::transaction_resume(root,work/"cancel",plan["plan_sha256"].asString());},"unsafe-resume");
        interrupted(root,plan,work/"corrupt","READY"); bytes(work/"corrupt/backup.bin","bad");
        reject([&]{ure::transaction_resume(root,work/"corrupt",plan["plan_sha256"].asString());},"backup-corrupt");
        interrupted(root,plan,work/"changed","EXECUTING"); bytes(work/"root/config","unrelated\n");
        check(ure::transaction_inspect(root,work/"changed")["current_state"]=="DIVERGED","Unrelated target change was not distinguished");
        reject([&]{ure::transaction_resume(root,work/"changed",plan["plan_sha256"].asString());},"unsafe-resume");
        reject([&]{ure::transaction_rollback(root,work/"changed",plan["plan_sha256"].asString());},"changed-target");
        ure::fs::create_directory(work/"other-root"); ure::Root other(work/"other-root");
        reject([&]{ure::transaction_inspect(other,work/"ready");},"wrong-root");
        ::chmod((work/"cancel").c_str(),0755);
        reject([&]{ure::transaction_inspect(root,work/"cancel");},"unsafe-journal");
        ::chmod((work/"cancel").c_str(),0700);
        const auto listed=ure::transaction_list(root,work); check(listed.size()>=4,"Journal discovery failed");
        ure::Root locked(work/"ready"); auto lock=locked.open(".lock",O_RDWR);
        check(::flock(lock.get(),LOCK_EX|LOCK_NB)==0,"Cannot lock fixture journal");
        reject([&]{ure::transaction_resume(root,work/"ready",plan["plan_sha256"].asString());},"busy-journal");
        ::flock(lock.get(),LOCK_UN);

        const std::string data=std::string(65536,'A')+std::string(65536,'B')+std::string(27001,'C');
        bytes(work/"root/image",data);
        auto backup_plan=ure::backup_plan(root,"image","fixture",65536);
        check(backup_plan["chunks"].size()==3 && backup_plan["sha256"]==ure::sha256(data),"Chunk plan geometry or full hash failed");
        backup_plan=ure::parse_json(ure::json(backup_plan));
        reject([&]{ure::backup_plan(root,"image","fixture",1);},"invalid-chunk-size");
        auto backup=ure::backup_capture(root,backup_plan,work/"backup",false);
        check(backup["state"]=="COMPLETE" && backup["verified"]==true && backup["completed_bytes"].asUInt64()==data.size(),"Streaming backup failed");
        check(::link((work/"backup/chunk-00000.bin").c_str(),(work/"foreign-alias").c_str())==0,"Cannot create foreign chunk alias");
        reject([&]{ure::backup_verify(work/"backup");},"backup-corrupt");
        check(::unlink((work/"foreign-alias").c_str())==0,"Cannot remove foreign chunk alias");
        // Test a published chunk with an older progress record, as after a crash.
        ure::fs::remove(work/"backup/chunk-00001.bin"); ure::fs::remove(work/"backup/chunk-00002.bin");
        check(ure::backup_verify(work/"backup")["next_chunk"].asUInt64()==1,"Backup resume trusted stale progress instead of verifying data");
        backup=ure::backup_capture(root,backup_plan,work/"backup",true);
        check(backup["sha256"]==ure::sha256(data),"Resume did not reconstruct the complete backup");
        auto export_fd=root.open("export",O_RDWR|O_CREAT|O_EXCL,0600);
        ure::backup_export(root,backup_plan,1,export_fd.get());
        check(ure::sha256(export_fd.get())==ure::sha256(std::string(65536,'B')),"Binary export emitted framing or incorrect bytes");
        reject([&]{ure::backup_export(root,backup_plan,3,export_fd.get());},"invalid-chunk");
        reject([&]{ure::backup_capture(other,backup_plan,work/"wrong-root",false);},"wrong-root");
        bytes(work/"backup/chunk-00001.bin",std::string(65536,'X'));
        reject([&]{ure::backup_verify(work/"backup");},"backup-corrupt");
        reject([&]{ure::backup_capture(root,backup_plan,work/"backup",true);},"backup-corrupt");
        bytes(work/"root/image",data+"changed");
        reject([&]{ure::backup_capture(root,backup_plan,work/"new-backup",false);},"stale-source");
        auto forged=backup_plan; forged["chunks"][0]["sha256"]=ure::sha256("forged");
        reject([&]{ure::backup_capture(root,forged,work/"forged",false);},"invalid-backup");
        bytes(work/"root/empty",""); const auto empty=ure::backup_plan(root,"empty","fixture",65536);
        check(ure::backup_capture(root,empty,work/"empty-backup",false)["verified"]==true,"Empty backup failed");
        // Actual process interruption: kill the writer after a durable first chunk.
        bytes(work/"root/interrupted",std::string(16*1024*1024,'Q'));
        const auto interrupted_plan=ure::backup_plan(root,"interrupted","fixture",65536);
        const auto child=::fork(); check(child>=0,"Cannot fork interruption fixture");
        if(child==0) {
            pause_chunk_link=true;
            try { ure::backup_capture(root,interrupted_plan,work/"killed-backup",false); ::_exit(0); }
            catch(...) { ::_exit(2); }
        }
        bool observed=false;
        for(unsigned tries=0;tries<5000;++tries) {
            if(ure::fs::exists(work/"killed-backup/chunk-00000.bin")) { observed=true; break; }
            ::usleep(1000);
        }
        ::kill(child,SIGKILL); int status=0; check(::waitpid(child,&status,0)==child,"Cannot reap interruption fixture");
        check(observed && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Writer was not interrupted at a durable boundary");
        const auto partial=ure::backup_verify(work/"killed-backup");
        check(partial["state"]=="PARTIAL" && partial["verified_chunks"].asUInt64()>=1,"Killed writer lost a published chunk");
        for(const auto& file:ure::fs::directory_iterator(work/"killed-backup")) {
            if(file.path().filename().string().starts_with("chunk-")) {
                struct stat published{};
                check(::lstat(file.path().c_str(),&published)==0 && published.st_nlink==1,
                    "Interrupted publication left an extra chunk alias");
            }
        }
        check(ure::backup_capture(root,interrupted_plan,work/"killed-backup",true)["verified"]==true,"Resume after SIGKILL failed");
        std::cout<<"URE recovery tests: persisted phases, safe resume/cancel, divergence, locks, streaming hashes, corrupt backups, wrong roots, binary export and SIGKILL/resume passed\n";
        ure::fs::remove_all(work); return 0;
    } catch(const std::exception& error) { std::cerr<<"Recovery test failure: "<<error.what()<<'\n'; ure::fs::remove_all(work); return 1; }
}
