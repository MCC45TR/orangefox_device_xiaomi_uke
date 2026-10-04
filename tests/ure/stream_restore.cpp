// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include "operation_lease.hpp"
#include <array>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace initial_stream_fault {
bool armed=false;
dev_t device=0;
ino_t inode=0;
int signal=-1;
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd) {
    const auto result=__real_fsync(fd);
    if(result==0 && initial_stream_fault::armed) {
        struct stat observed{};
        if(::fstat(fd,&observed)==0 && S_ISDIR(observed.st_mode) && observed.st_dev==initial_stream_fault::device && observed.st_ino==initial_stream_fault::inode) {
            const auto* domain=::getenv("URE_OPERATION_COORDINATOR");
            if(domain && ure::fs::exists(ure::fs::path(domain)/"owner.json") && ure::json_file(ure::fs::path(domain)/"owner.json")["phase"]=="stream-ready") {
                initial_stream_fault::armed=false;
                if(::write(initial_stream_fault::signal,"S",1)!=1)::_exit(3);
                for(;;)::pause();
            }
        }
    }
    return result;
}
namespace {
void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
template<class F> void reject(F function,const std::string& code) {
    try { function(); } catch(const ure::Error& error) { check(error.code==code,"Unexpected refusal: "+error.code+", expected "+code); return; }
    throw std::runtime_error("Expected refusal: "+code);
}
void bytes(const ure::fs::path& path,std::string_view data) {
    std::ofstream out(path,std::ios::binary); out.write(data.data(),static_cast<std::streamsize>(data.size())); check(out.good(),"Cannot write fixture");
}
std::string hash(const ure::fs::path& path) { ure::Fd fd(::open(path.c_str(),O_RDONLY)); return ure::sha256(fd.get()); }
ure::Fd packet(const ure::fs::path& file,const ure::fs::path& before,const ure::fs::path& after,std::uint64_t index) {
    ure::Fd output(::open(file.c_str(),O_RDWR|O_CREAT|O_TRUNC,0600)); check(output.get()>=0,"Cannot create stream packet");
    ure::backup_store_export(before,index,output.get()); ure::backup_store_export(after,index,output.get());
    check(::lseek(output.get(),0,SEEK_SET)==0,"Cannot rewind packet"); return output;
}
void stream_all(const ure::Root& system,ure::StorageTarget& target,const ure::fs::path& journal,const ure::Value& plan,
                const ure::fs::path& before,const ure::fs::path& after,const ure::fs::path& temporary) {
    while(true) {
        const auto state=ure::restore_stream_status(system,target,journal); const auto next=state["next_chunk"].asUInt64();
        if(next==state["chunk_count"].asUInt64())break;
        auto input=packet(temporary,before,after,next); ure::restore_stream_chunk(system,target,journal,next,input.get(),plan["plan_sha256"].asString());
    }
}
void interrupted_begin(const ure::Root& system,const ure::StorageTarget& target,const ure::Value& plan,const ure::Value& receipt,const ure::fs::path& journal) {
    int pipe[2]{}; check(::pipe2(pipe,O_CLOEXEC)==0,"Cannot create stream admission barrier"); ure::Fd input(pipe[0]),output(pipe[1]);
    const auto child=::fork(); check(child>=0,"Cannot fork initial stream interruption");
    if(child==0) {
        try {
            input=ure::Fd(); const auto* domain=::getenv("URE_OPERATION_COORDINATOR"); struct stat identity{};
            check(domain && ::stat(domain,&identity)==0,"Cannot select shared stream ownership domain");
            initial_stream_fault::device=identity.st_dev; initial_stream_fault::inode=identity.st_ino;
            initial_stream_fault::signal=output.get(); initial_stream_fault::armed=true;
            ure::restore_stream_begin(system,target,plan,receipt,journal,plan["plan_sha256"].asString()); ::_exit(2);
        } catch(...) { ::_exit(4); }
    }
    output=ure::Fd(); pollfd barrier{input.get(),POLLIN,0}; char signal=0;
    const bool observed=::poll(&barrier,1,5000)>0 && (barrier.revents&POLLIN) && ::read(input.get(),&signal,1)==1 && signal=='S';
    const auto killed=::kill(child,SIGKILL); int status=0; const auto waited=::waitpid(child,&status,0);
    check(observed && killed==0 && waited==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Initial stream fixture did not stop after durable intent and before READY");
}
}
int main() {
    std::array<char,48> pattern{}; const std::string text="/tmp/ure-stream-restore-tests-XXXXXX"; std::copy(text.begin(),text.end(),pattern.begin());
    const auto raw=::mkdtemp(pattern.data()); if(!raw)return 1; const ure::fs::path work(raw); ure::Root system("/");
    try {
        const std::string original(3*65536,'X'),desired=std::string(65536,'A')+std::string(65536,'B')+std::string(65536,'C');
        for(const std::uint32_t sector:{512U,4096U}) {
            const auto prefix=work/std::to_string(sector); ure::fs::create_directory(prefix);
            const auto image=prefix/"image",before=prefix/"before",after=prefix/"after",journal=prefix/"journal",input=prefix/"packet";
            bytes(image,desired); auto target=ure::storage_image(image,sector);
            const auto manifest=ure::backup_storage_plan(system,target,"fixture",65536); ure::backup_capture(system,manifest,after,false);
            bytes(image,original); target=ure::storage_image(image,sector,true);
            const auto plan=ure::restore_stream_plan(system,target,manifest,"fixture"); const auto confirm=plan["plan_sha256"].asString();
            check(plan["estimated_journal_bytes"].asUInt64()==4*65536+16*1024*1024 && plan["host_streamed_restore"]==true,"Cache space boundary differs");
            reject([&]{ure::restore_stream_plan(system,target,manifest,"other");},"wrong-profile");
            auto wrong=ure::storage_image(image,sector==512 ? 4096U : 512U); reject([&]{ure::restore_stream_plan(system,wrong,manifest,"fixture");},"wrong-target");
            ure::backup_capture(system,ure::restore_stream_backup_plan(plan),before,false);
            auto receipt=ure::restore_host_receipt(plan,before,after);
            check(receipt["trust"]=="HOST_ATTESTED" && receipt["device_verified_host_persistence"]==false,"Host trust is overstated");
            ure::Root store(after); auto store_lock=store.open(".lock",O_RDWR); check(::flock(store_lock.get(),LOCK_EX|LOCK_NB)==0,"Cannot lock host store");
            reject([&]{ure::restore_host_receipt(plan,before,after);},"busy-journal"); ::flock(store_lock.get(),LOCK_UN);
            auto bad_receipt=receipt; bad_receipt["before"]["verified"]=false;
            reject([&]{ure::restore_stream_begin(system,target,plan,bad_receipt,journal,confirm);},"invalid-host-receipt");
            reject([&]{ure::restore_stream_begin(system,target,plan,receipt,journal,"wrong");},"confirmation-required");
            auto readonly=ure::storage_image(image,sector);
            reject([&]{ure::restore_stream_begin(system,readonly,plan,receipt,journal,confirm);},"read-only-target");
            check(!ure::fs::exists(journal),"Rejected stream start created journal");
            const auto early=prefix/"early-admission";
            interrupted_begin(system,target,plan,receipt,early);
            check(ure::json_file(early/"journal.json")["state"]=="VALIDATED" && hash(image)==ure::sha256(original),
                "Initial stream interruption lost its durable recovery state or changed target bytes");
            check(ure::operation_lease_status()["retained_owner"]==true,"Initial stream SIGKILL lost retained ownership");
            reject([&]{ure::LifecycleLease::acquire("reboot");},"operation-recovery-required");
            reject([&]{ure::backup_capture(system,plan["before"],prefix/"blocked-before",false);},"operation-recovery-required");
            check(!ure::fs::exists(prefix/"blocked-before"),"Competing backup created a store through retained stream ownership");
            check(ure::restore_stream_status(system,target,early)["classification"]=="ORIGINAL","VALIDATED stream readback was not recoverable");
            const auto early_cancel=ure::restore_stream_cancel(system,target,early,confirm);
            check(early_cancel["state"]=="CANCELLED_SAFE" && early_cancel["operation_owner_released"]==true && hash(image)==ure::sha256(original),
                "Exact initial stream cancellation did not verify and release ownership");
            ure::restore_stream_begin(system,target,plan,receipt,journal,confirm);
            auto status=ure::restore_stream_status(system,target,journal); check(status["classification"]=="ORIGINAL" && status["next_chunk"].asUInt64()==0,"Fresh stream classification differs");
            auto valid=packet(input,before,after,0);
            reject([&]{ure::restore_stream_chunk(system,target,journal,1,valid.get(),confirm);},"invalid-chunk");
            reject([&]{ure::restore_stream_chunk(system,target,journal,0,valid.get(),"wrong");},"confirmation-required");
            bytes(input,original.substr(0,65536)+desired.substr(0,65536)+"extra"); valid=ure::Fd(::open(input.c_str(),O_RDONLY));
            reject([&]{ure::restore_stream_chunk(system,target,journal,0,valid.get(),confirm);},"extra-stream-data");
            check(hash(image)==ure::sha256(original),"Extra packet data caused a target write");
            bytes(input,original.substr(0,65536)+desired.substr(0,65535)); valid=ure::Fd(::open(input.c_str(),O_RDONLY));
            reject([&]{ure::restore_stream_chunk(system,target,journal,0,valid.get(),confirm);},"truncated-stream");
            bytes(input,std::string(2*65536,'Z')); valid=ure::Fd(::open(input.c_str(),O_RDONLY));
            reject([&]{ure::restore_stream_chunk(system,target,journal,0,valid.get(),confirm);},"stream-corrupt");
            check(hash(image)==ure::sha256(original),"Invalid packet changed the target");
            valid=packet(input,before,after,0); status=ure::restore_stream_chunk(system,target,journal,0,valid.get(),confirm);
            check(status["next_chunk"].asUInt64()==1 && status["verification_scope"]=="unchanged-image-metadata-and-per-chunk-readback" && status["current_sha256"].isNull(),
                "Verified chunk did not advance bounded readback proof or overclaimed a full scan");
            reject([&]{ure::restore_stream_cancel(system,target,journal,confirm);},"unsafe-cancel");
            reject([&]{ure::restore_stream_finish(system,target,journal,confirm);},"unsafe-finish");
            // Journal counters are advisory; readback must derive the next chunk.
            auto state=ure::json_file(journal/"journal.json"); state["last_verified_chunk"]=Json::UInt64(UINT64_MAX); ure::save_json(journal/"journal.json",state,true);
            check(ure::restore_stream_status(system,target,journal)["next_chunk"].asUInt64()==1,"Journal counter bypassed readback");
            // A change outside the incoming range invalidates the metadata cache
            // even when its mtime is restored. A full scan must find divergence.
            auto unrelated=desired.substr(0,65536)+original.substr(65536); unrelated.back()='Z'; bytes(image,unrelated);
            const auto& identity=state["readback_proof"]["image_identity"];
            const std::array<struct timespec,2> times{{{0,UTIME_OMIT},{static_cast<time_t>(identity["mtime_seconds"].asInt64()),static_cast<long>(identity["mtime_nanoseconds"].asInt64())}}};
            check(::utimensat(AT_FDCWD,image.c_str(),times.data(),0)==0,"Cannot restore fixture mtime");
            valid=packet(input,before,after,1);
            reject([&]{ure::restore_stream_chunk(system,target,journal,1,valid.get(),confirm);},"unsafe-resume");
            check(hash(image)==ure::sha256(unrelated),"Unrelated changes were overwritten through cached proof");
            bytes(image,desired.substr(0,65536)+original.substr(65536));
            state["readback_proof"]["chunk_classes"]="TTT"; ure::save_json(journal/"journal.json",state,true);
            // A corrupted cache proof falls back to current bytes instead of
            // claiming all desired chunks have been written.
            stream_all(system,target,journal,plan,before,after,input);
            check(ure::restore_stream_finish(system,target,journal,confirm)["state"]=="COMMITTED" && hash(image)==ure::sha256(desired),"Full stream restore failed");
            reject([&]{ure::restore_stream_chunk(system,target,journal,0,valid.get(),confirm);},"unsafe-resume");
            ure::restore_stream_rollback(system,target,journal,confirm); stream_all(system,target,journal,plan,before,after,input);
            check(ure::restore_stream_finish(system,target,journal,confirm)["state"]=="ROLLED_BACK" && hash(image)==ure::sha256(original),"Host-assisted rollback failed");
            for(const auto& entry:ure::fs::directory_iterator(journal))check(!entry.path().filename().string().starts_with(".stream-"),"Completed journal retained raw chunks");
            check(!ure::fs::exists(journal/"before") && !ure::fs::exists(journal/"after"),"Stream restore copied full local mirrors");
            const auto cancelled=prefix/"cancelled"; target=ure::storage_image(image,sector,true);
            const auto cancel_plan=ure::restore_stream_plan(system,target,manifest,"fixture"); const auto cancel_before=prefix/"cancel-before";
            ure::backup_capture(system,cancel_plan["before"],cancel_before,false); const auto cancel_receipt=ure::restore_host_receipt(cancel_plan,cancel_before,after);
            ure::restore_stream_begin(system,target,cancel_plan,cancel_receipt,cancelled,cancel_plan["plan_sha256"].asString());
            check(ure::restore_stream_cancel(system,target,cancelled,cancel_plan["plan_sha256"].asString())["state"]=="CANCELLED_SAFE","Safe cancellation failed");
            // A real write failure retains durable verified caches for partial
            // byte classification and explicit rollback, without the host present.
            const auto interrupted=prefix/"interrupted"; target=ure::storage_image(image,sector,true);
            ure::restore_stream_begin(system,target,cancel_plan,cancel_receipt,interrupted,cancel_plan["plan_sha256"].asString());
            auto failing=packet(input,cancel_before,after,0);
            ure::restore_stream_chunk(system,target,interrupted,0,failing.get(),cancel_plan["plan_sha256"].asString());
            failing=packet(input,cancel_before,after,1); const auto child=::fork(); check(child>=0,"Cannot fork failed writer");
            if(child==0) {
                ::signal(SIGXFSZ,SIG_IGN); const struct rlimit limit{65536,65536}; if(::setrlimit(RLIMIT_FSIZE,&limit)!=0)::_exit(3);
                try { ure::restore_stream_chunk(system,target,interrupted,1,failing.get(),cancel_plan["plan_sha256"].asString()); ::_exit(4); }
                catch(const ure::Error& error) { ::_exit(error.code=="io-error" ? 0 : 5); }
            }
            int exit_status=0; check(::waitpid(child,&exit_status,0)==child && WIFEXITED(exit_status) && WEXITSTATUS(exit_status)==0,"Write-failure fixture did not reject pwrite");
            state=ure::json_file(interrupted/"journal.json");
            check(state["state"]=="FAILED_UNCERTAIN" && state["active"]["index"].asUInt64()==1,"Failed writer did not preserve durable cache proof");
            auto mixed=original; mixed.replace(0,65536,desired.substr(0,65536)); mixed.replace(65536,32769,desired.substr(65536,32769)); bytes(image,mixed);
            status=ure::restore_stream_status(system,target,interrupted);
            check(status["classification"]=="PARTIAL_EXPECTED_WRITE" && status["next_chunk"].asUInt64()==1,"Interrupted chunk lost expected-byte proof");
            bytes(input,original.substr(65536,32768)); auto truncated=ure::Fd(::open(input.c_str(),O_RDONLY));
            reject([&]{ure::restore_stream_chunk(system,target,interrupted,1,truncated.get(),cancel_plan["plan_sha256"].asString());},"truncated-stream");
            check(ure::restore_stream_status(system,target,interrupted)["classification"]=="PARTIAL_EXPECTED_WRITE", "Failed reconnect discarded the existing partial-chunk proof");
            ure::restore_stream_rollback(system,target,interrupted,cancel_plan["plan_sha256"].asString()); stream_all(system,target,interrupted,cancel_plan,cancel_before,after,input);
            check(ure::restore_stream_finish(system,target,interrupted,cancel_plan["plan_sha256"].asString())["state"]=="ROLLED_BACK" && hash(image)==ure::sha256(original),"Partial rollback failed");
            // A partial chunk without its durable cache proof is unrelated data.
            state["active"]=ure::Value(); state["direction"]="restore"; ure::save_json(interrupted/"journal.json",state,true); bytes(image,mixed);
            check(ure::restore_stream_status(system,target,interrupted)["classification"]=="DIVERGED","Unproven partial bytes were accepted");
            reject([&]{ure::restore_stream_rollback(system,target,interrupted,cancel_plan["plan_sha256"].asString());},"unsafe-rollback");
            bytes(image,original);
            // Corrupt host data cannot create a new host receipt or binary export.
            bytes(after/"chunk-00000.bin",std::string(65536,'Z'));
            reject([&]{ure::restore_host_receipt(plan,before,after);},"backup-corrupt");
            reject([&]{ure::backup_store_export(after,0,STDOUT_FILENO);},"backup-corrupt");
        }
        // Kill a receiver blocked mid-packet. No write intent exists, and orphan
        // caches are discarded before an explicit retry of the same chunk.
        const auto image=work/"killed-image",before=work/"killed-before",after=work/"killed-after",journal=work/"killed-journal";
        bytes(image,desired); auto target=ure::storage_image(image,4096); const auto manifest=ure::backup_storage_plan(system,target,"fixture",65536);
        ure::backup_capture(system,manifest,after,false); bytes(image,original); target=ure::storage_image(image,4096,true);
        const auto plan=ure::restore_stream_plan(system,target,manifest,"fixture"); ure::backup_capture(system,plan["before"],before,false);
        ure::restore_stream_begin(system,target,plan,ure::restore_host_receipt(plan,before,after),journal,plan["plan_sha256"].asString());
        int pipe_fds[2]{}; check(::pipe(pipe_fds)==0,"Cannot create receive pipe"); const auto child=::fork(); check(child>=0,"Cannot fork receiver");
        if(child==0) { ::close(pipe_fds[1]); try { ure::restore_stream_chunk(system,target,journal,0,pipe_fds[0],plan["plan_sha256"].asString()); ::_exit(0); } catch(...) { ::_exit(2); } }
        ::close(pipe_fds[0]); check(::write(pipe_fds[1],"X",1)==1,"Cannot feed partial packet"); bool observed=false;
        for(unsigned tries=0;tries<2000 && !observed;++tries) { for(const auto& file:ure::fs::directory_iterator(journal))if(file.path().filename().string().starts_with(".stream-before-"))observed=true; ::usleep(1000); }
        ::kill(child,SIGKILL); int status=0; check(::waitpid(child,&status,0)==child && WIFSIGNALED(status) && observed,"Receiver did not stop at partial input"); ::close(pipe_fds[1]);
        check(hash(image)==ure::sha256(original) && ure::restore_stream_status(system,target,journal)["classification"]=="ORIGINAL","Killed receiver changed data");
        stream_all(system,target,journal,plan,before,after,work/"retry-packet"); ure::restore_stream_finish(system,target,journal,plan["plan_sha256"].asString());
        check(hash(image)==ure::sha256(desired),"Killed receive retry failed");
        ure::fs::remove_all(work); std::cout<<"Host-streamed restore, rollback, bounded caches and interruption fixtures passed.\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\nFixture retained at "<<work<<'\n'; return 1; }
}
